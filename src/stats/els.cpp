// els.cpp - ELS reference scan and the two multi-target kernels
// (PREREG-004, "Kernel rule": production kernels must match the plain scan
// exactly; the faster is chosen by measured A/B).
#include "gem/els.hpp"

extern "C" {
#include "gem/hebrew.h"
}

#include <algorithm>
#include <stdexcept>

namespace gem {

std::vector<uint8_t> letter_stream(const gem_corpus &c, uint32_t lo, uint32_t hi)
{
    std::vector<uint8_t> L(hi - lo);
    for (uint32_t i = lo; i < hi; i++)
        L[i - lo] = gem_letter_base(c.letters[i]);
    return L;
}

LetterRange verse_letters(const gem_corpus &c, const std::string &book, unsigned ch1, unsigned v1, unsigned ch2,
                          unsigned v2)
{
    int64_t lo = -1, hi = -1;
    for (uint32_t v = 0; v < c.n_verses; v++) {
        const gem_verse &vs = c.verses[v];
        if (book != c.books[vs.book].osis)
            continue;
        const uint32_t first_word = vs.first_word;
        const uint32_t end_word = v + 1 < c.n_verses ? c.verses[v + 1].first_word : c.n_words;
        if (vs.chapter == ch1 && vs.verse == v1)
            lo = c.words[first_word].first_letter;
        if (vs.chapter == ch2 && vs.verse == v2)
            hi = c.words[end_word - 1].first_letter + c.words[end_word - 1].n_letters;
    }
    if (lo < 0 || hi < 0 || hi <= lo)
        throw std::runtime_error("verse range not found: " + book);
    return {static_cast<uint32_t>(lo), static_cast<uint32_t>(hi)};
}

namespace {

unsigned band_of(unsigned d)
{
    return d <= 10 ? 0u : d <= 100 ? 1u : 2u;
}

uint64_t count_forward(const std::vector<uint8_t> &L, const Word &w, unsigned d)
{
    const size_t k = w.size(), n = L.size();
    const size_t span = (k - 1) * d;
    if (span >= n)
        return 0;
    uint64_t hits = 0;
    for (size_t i = 0; i + span < n; i++) {
        size_t j = 0;
        while (j < k && L[i + j * d] == w[j])
            j++;
        hits += j == k;
    }
    return hits;
}

}  // namespace

std::vector<ElsHit> els_find(const std::vector<uint8_t> &L, const Word &w, unsigned dmin, unsigned dmax)
{
    std::vector<ElsHit> out;
    const size_t k = w.size(), n = L.size();
    for (unsigned d = std::max(2u, dmin); d <= dmax; d++) {
        const size_t span = (k - 1) * d;
        for (size_t i = 0; i + span < n; i++) {
            bool fwd = true, bwd = true;
            for (size_t j = 0; j < k && (fwd || bwd); j++) {
                fwd &= L[i + j * d] == w[j];
                bwd &= L[i + span - j * d] == w[j];
            }
            if (fwd)
                out.push_back({static_cast<uint32_t>(i), static_cast<int32_t>(d)});
            if (bwd)
                out.push_back({static_cast<uint32_t>(i + span), -static_cast<int32_t>(d)});
        }
    }
    return out;
}

ElsCount els_count_plain(const std::vector<uint8_t> &L, const std::vector<Word> &targets, unsigned dmin,
                         unsigned dmax)
{
    ElsCount c;
    for (const Word &w : targets) {
        const Word r(w.rbegin(), w.rend());
        for (unsigned d = std::max(2u, dmin); d <= dmax; d++) {
            const uint64_t h = count_forward(L, w, d) + count_forward(L, r, d);
            c.total += h;
            c.band[band_of(d)] += h;
        }
    }
    return c;
}

namespace {

constexpr uint32_t kKeyBits = 25, kKeyMask = (1u << kKeyBits) - 1u;

uint32_t key_of(const Word &w)
{
    uint32_t k = 0;
    for (uint8_t x : w)
        k = (k << 5) | x;
    return k;
}

}  // namespace

Els5::Els5(const std::vector<Word> &targets) : bits_((1u << kKeyBits) / 64u, 0), filter_((1u << 16) / 64u, 0)
{
    std::vector<uint32_t> keys;
    for (const Word &w : targets) {
        if (w.size() != 5)
            throw std::runtime_error("Els5 takes 5-letter targets");
        keys.push_back(key_of(w));
        keys.push_back(key_of(Word(w.rbegin(), w.rend())));
    }
    std::sort(keys.begin(), keys.end());
    for (size_t i = 0; i < keys.size();) {
        size_t j = i;
        while (j < keys.size() && keys[j] == keys[i])
            j++;
        bits_[keys[i] >> 6] |= 1ull << (keys[i] & 63u);
        const uint32_t h = hash16(keys[i]);
        filter_[h >> 6] |= 1ull << (h & 63u);
        mult_.emplace_back(keys[i], static_cast<uint32_t>(j - i));
        i = j;
    }
}

uint32_t Els5::multiplicity(uint32_t key) const
{
    auto it = std::lower_bound(mult_.begin(), mult_.end(), std::make_pair(key, 0u));
    return it != mult_.end() && it->first == key ? it->second : 0u;
}

ElsCount Els5::count_streams(const std::vector<uint8_t> &L, unsigned dmin, unsigned dmax) const
{
    ElsCount c;
    const size_t n = L.size();
    const uint64_t *bits = bits_.data();
    for (unsigned d = std::max(2u, dmin); d <= dmax; d++) {
        const size_t span = 4u * d;
        if (span >= n)
            break;
        uint64_t h = 0;
        const uint8_t *a = L.data(), *b = a + d, *e = a + 2 * d, *f = a + 3 * d, *g = a + 4 * d;
        for (size_t i = 0; i + span < n; i++) {
            const uint32_t key = static_cast<uint32_t>(a[i]) << 20 | static_cast<uint32_t>(b[i]) << 15 |
                                 static_cast<uint32_t>(e[i]) << 10 | static_cast<uint32_t>(f[i]) << 5 | g[i];
            if (bits[key >> 6] >> (key & 63u) & 1u)
                h += multiplicity(key);
        }
        c.total += h;
        c.band[band_of(d)] += h;
    }
    return c;
}

ElsCount Els5::count_rolling(const std::vector<uint8_t> &L, unsigned dmin, unsigned dmax) const
{
    ElsCount c;
    const size_t n = L.size();
    const uint64_t *bits = bits_.data();
    for (unsigned d = std::max(2u, dmin); d <= dmax; d++) {
        if (4u * d >= n)
            break;
        uint64_t h = 0;
        for (size_t r = 0; r < d; r++) {
            uint32_t key = 0;
            unsigned have = 0;
            for (size_t p = r; p < n; p += d) {
                key = ((key << 5) | L[p]) & kKeyMask;
                if (++have >= 5 && (bits[key >> 6] >> (key & 63u) & 1u))
                    h += multiplicity(key);
            }
        }
        c.total += h;
        c.band[band_of(d)] += h;
    }
    return c;
}

ElsCount Els5::count_hashed(const std::vector<uint8_t> &L, unsigned dmin, unsigned dmax) const
{
    ElsCount c;
    const size_t n = L.size();
    const uint64_t *bits = bits_.data(), *filter = filter_.data();
    for (unsigned d = std::max(2u, dmin); d <= dmax; d++) {
        if (4u * d >= n)
            break;
        uint64_t h = 0;
        for (size_t r = 0; r < d; r++) {
            uint32_t key = 0;
            unsigned have = 0;
            for (size_t p = r; p < n; p += d) {
                key = ((key << 5) | L[p]) & kKeyMask;
                const uint32_t f = hash16(key);
                if (++have >= 5 && (filter[f >> 6] >> (f & 63u) & 1u) && (bits[key >> 6] >> (key & 63u) & 1u))
                    h += multiplicity(key);
            }
        }
        c.total += h;
        c.band[band_of(d)] += h;
    }
    return c;
}

}  // namespace gem
