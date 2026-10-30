// replicate.cpp - PREREG-001 analyses R1 (dictionary-style collision
// figures over WLC types) and R2 (Pentateuch coverage of values 1..1000
// with the M1 permutation and M2 Markov nulls).
//
// Replicates run on a pool of threads that pull replicate indexes from an
// atomic counter; every replicate seeds its own generator from its index
// (rng.hpp), so results do not depend on the thread count.
#include "gem/stats.hpp"
#include "rng.hpp"

extern "C" {
#include "gem/hebrew.h"
}

#include <algorithm>
#include <atomic>
#include <bitset>
#include <chrono>
#include <cmath>
#include <thread>

namespace gem {

namespace {

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point t0)
{
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

template <class F>
void parallel_for(unsigned n, unsigned threads, F &&body)
{
    std::atomic<unsigned> next{0};
    std::vector<std::jthread> pool;
    for (unsigned t = 0; t < std::max(1u, threads); t++)
        pool.emplace_back([&] {
            for (unsigned i; (i = next.fetch_add(1, std::memory_order_relaxed)) < n;)
                body(i);
        });
}

double median(std::vector<double> v)
{
    if (v.empty())
        return 0.0;
    auto mid = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    double hi = *mid;
    if (v.size() % 2)
        return hi;
    return (hi + *std::max_element(v.begin(), mid)) / 2.0;
}

// Match share of every type against all others under (mask, kolel), by
// stamping: each thread marks the types it has counted for the current t.
std::vector<double> all_shares(const Lexicon &lex, uint32_t mask, int kolel, unsigned threads)
{
    const uint32_t n = lex.size();
    std::vector<double> share(n);
    const unsigned chunks = threads * 8u;
    parallel_for(chunks, threads, [&](unsigned chunk) {
        std::vector<uint32_t> stamp(n, UINT32_MAX);
        for (uint32_t t = chunk; t < n; t += chunks) {
            const Values &v = lex.values(t);
            uint32_t count = 0;
            for (unsigned m = 0; m < kMethods; m++) {
                if (!(mask >> m & 1u))
                    continue;
                for (int k = -kolel; k <= kolel; k++) {
                    if (k < 0 && v[m] < static_cast<uint64_t>(-k))
                        continue;
                    const auto *b = lex.bucket(m, v[m] + static_cast<uint64_t>(static_cast<int64_t>(k)));
                    if (!b)
                        continue;
                    for (uint32_t u : *b) {
                        count += stamp[u] != t;
                        stamp[u] = t;
                    }
                }
            }
            share[t] = n > 1 ? static_cast<double>(count - 1u) / static_cast<double>(n - 1u) : 0.0;
        }
    });
    return share;
}

}  // namespace

R1 run_r1(const Lexicon &lex, unsigned threads)
{
    auto t0 = Clock::now();
    R1 r;
    const uint32_t n = r.types = lex.size();

    std::unordered_map<uint64_t, uint64_t> per_value;
    for (uint32_t t = 0; t < n; t++)
        per_value[lex.values(t)[GEM_M_HECHRACHI]]++;
    double pairs = 0;
    for (auto &[value, k] : per_value)
        pairs += static_cast<double>(k) * static_cast<double>(k - 1u) / 2.0;
    r.pair_collision = pairs / (static_cast<double>(n) * static_cast<double>(n - 1u) / 2.0);

    constexpr unsigned kDraws = 100000, kPick = 30;
    std::vector<uint8_t> shared(kDraws);
    parallel_for(kDraws, threads, [&](unsigned d) {
        Rng rng(replicate_seed(kSeed, 1, d));
        uint32_t pick[kPick];
        uint64_t vals[kPick];
        for (unsigned i = 0; i < kPick; i++) {
            bool dup;
            do {
                pick[i] = static_cast<uint32_t>(rng.below(n));
                dup = std::find(pick, pick + i, pick[i]) != pick + i;
            } while (dup);
            vals[i] = lex.values(pick[i])[GEM_M_HECHRACHI];
        }
        std::sort(vals, vals + kPick);
        shared[d] = std::adjacent_find(vals, vals + kPick) != vals + kPick;
    });
    r.birthday30 = static_cast<double>(std::count(shared.begin(), shared.end(), 1)) / kDraws;

    r.median_share_std = median(all_shares(lex, 1u << GEM_M_HECHRACHI, 0, threads));
    r.median_share_all = median(all_shares(lex, kAllMethods, 1, threads));
    r.seconds = seconds_since(t0);
    return r;
}

namespace {

// A Pentateuch word for coverage: the strict rule takes letters
// [prefix, len - suffix) of content words.
struct PWord {
    uint32_t first, len, prefix, suffix;
    bool content;
};

std::vector<PWord> pentateuch_words(const gem_corpus &c, StrictVariant v)
{
    const uint32_t w_hi = c.books[4].first_word + c.books[4].n_words;
    std::vector<PWord> words;
    words.reserve(w_hi);
    for (uint32_t w = 0; w < w_hi; w++) {
        WordShape s = word_shape(c, w);
        words.push_back({c.words[w].first_letter, c.words[w].n_letters, s.prefix_letters,
                         v.strip_suffix ? s.suffix_letters : 0u, s.content && !(v.exclude_proper && s.proper)});
    }
    return words;
}

using Cover = std::bitset<1001>;

// Hechrachi coverage of values 1..1000 over the Pentateuch words.
void coverage(const std::vector<uint8_t> &letters, const std::vector<PWord> &words, const uint32_t *table,
              Cover &loose, Cover &strict)
{
    loose.reset();
    strict.reset();
    for (const PWord &w : words) {
        uint64_t pre = 0, core = 0, post = 0;
        const uint32_t core_end = w.len - w.suffix;
        for (uint32_t i = 0; i < w.prefix; i++)
            pre += table[letters[w.first + i]];
        for (uint32_t i = w.prefix; i < core_end; i++)
            core += table[letters[w.first + i]];
        for (uint32_t i = core_end; i < w.len; i++)
            post += table[letters[w.first + i]];
        if (pre + core + post <= 1000)
            loose.set(pre + core + post);
        if (w.content && w.prefix < core_end && core <= 1000)
            strict.set(core);
    }
}

unsigned covered(const Cover &c)
{
    return static_cast<unsigned>(c.count() - c.test(0));
}

std::vector<unsigned> missing_upto(const Cover &c, unsigned hi)
{
    std::vector<unsigned> out;
    for (unsigned v = 1; v <= hi; v++)
        if (!c.test(v))
            out.push_back(v);
    return out;
}

NullRun summarize(const std::vector<unsigned> &reps, unsigned observed, double secs)
{
    NullRun r;
    r.replicates = static_cast<unsigned>(reps.size());
    r.seconds = secs;
    if (reps.empty())
        return r;
    double s = 0, ss = 0;
    for (unsigned x : reps) {
        s += x;
        ss += static_cast<double>(x) * x;
        r.at_or_below += x <= observed;
    }
    r.mean = s / static_cast<double>(reps.size());
    double var = reps.size() > 1 ? (ss - s * s / static_cast<double>(reps.size())) / static_cast<double>(reps.size() - 1) : 0.0;
    r.sd = std::sqrt(std::max(var, 0.0));
    r.z = r.sd > 0 ? (observed - r.mean) / r.sd : 0.0;
    return r;
}

}  // namespace

R2 run_r2(const gem_corpus &c, StrictVariant rule, unsigned m1_reps, unsigned m2_reps, unsigned threads)
{
    const uint32_t *table = gem_letter_table(GEM_M_HECHRACHI);
    const uint32_t l_hi = c.books[4].first_letter + c.books[4].n_letters;
    std::vector<uint8_t> letters(c.letters, c.letters + l_hi);
    const std::vector<PWord> words = pentateuch_words(c, rule);

    R2 r;
    Cover loose, strict;
    coverage(letters, words, table, loose, strict);
    r.loose = covered(loose);
    r.strict = covered(strict);
    r.loose_missing_861 = missing_upto(loose, 861);
    r.strict_missing_861 = missing_upto(strict, 861);

    // M1: permute every Pentateuch letter; word lengths, parts and tags stay.
    std::vector<unsigned> m1s(m1_reps), m1l(m1_reps);
    auto t0 = Clock::now();
    parallel_for(m1_reps, threads, [&](unsigned rep) {
        Rng rng(replicate_seed(kSeed, 2, rep));
        std::vector<uint8_t> perm = letters;
        for (size_t i = perm.size() - 1; i > 0; i--)
            std::swap(perm[i], perm[rng.below(i + 1)]);
        Cover cl, cs;
        coverage(perm, words, table, cl, cs);
        m1s[rep] = covered(cs);
        m1l[rep] = covered(cl);
    });
    double m1_secs = seconds_since(t0);
    r.m1_strict = summarize(m1s, r.strict, m1_secs);
    r.m1_loose = summarize(m1l, r.loose, m1_secs);

    // M2: first-order Markov chain over base letters (finals folded: equal
    // hechrachi values), fitted on all Pentateuch words; each word is
    // regenerated at its own length.
    uint64_t init[GEM_BASE_LETTERS] = {}, trans[GEM_BASE_LETTERS][GEM_BASE_LETTERS] = {};
    for (const PWord &w : words) {
        uint8_t prev = gem_letter_base(letters[w.first]);
        init[prev]++;
        for (uint32_t i = 1; i < w.len; i++) {
            uint8_t cur = gem_letter_base(letters[w.first + i]);
            trans[prev][cur]++;
            prev = cur;
        }
    }
    auto draw = [](Rng &rng, const uint64_t *counts) {
        uint64_t total = 0;
        for (unsigned k = 0; k < GEM_BASE_LETTERS; k++)
            total += counts[k];
        uint64_t x = rng.below(total);
        unsigned k = 0;
        while (x >= counts[k])
            x -= counts[k++];
        return static_cast<uint8_t>(k);
    };
    std::vector<unsigned> m2s(m2_reps), m2l(m2_reps);
    t0 = Clock::now();
    parallel_for(m2_reps, threads, [&](unsigned rep) {
        Rng rng(replicate_seed(kSeed, 3, rep));
        std::vector<uint8_t> gen(letters.size());
        for (const PWord &w : words) {
            uint8_t b = draw(rng, init);
            gen[w.first] = gem_base_code(b);
            for (uint32_t i = 1; i < w.len; i++) {
                b = draw(rng, trans[b]);
                gen[w.first + i] = gem_base_code(b);
            }
        }
        Cover cl, cs;
        coverage(gen, words, table, cl, cs);
        m2s[rep] = covered(cs);
        m2l[rep] = covered(cl);
    });
    double m2_secs = seconds_since(t0);
    r.m2_strict = summarize(m2s, r.strict, m2_secs);
    r.m2_loose = summarize(m2l, r.loose, m2_secs);
    return r;
}

VariantCoverage strict_variant(const gem_corpus &c, StrictVariant v)
{
    const uint32_t l_hi = c.books[4].first_letter + c.books[4].n_letters;
    std::vector<uint8_t> letters(c.letters, c.letters + l_hi);
    Cover loose, strict;
    coverage(letters, pentateuch_words(c, v), gem_letter_table(GEM_M_HECHRACHI), loose, strict);
    return {covered(strict), missing_upto(strict, 861)};
}

}  // namespace gem
