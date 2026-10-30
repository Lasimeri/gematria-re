// cluster.cpp - PREREG-003: do equal-valued words cluster in the text?
//
// A qualifying pair is two tokens with equal standard value (K = 0) or
// values within 1 (K = 1), different lemma keys and different letter
// multisets (finals folded); see pairs.hpp. S counts qualifying pairs
// inside the same window (verse or chapter). Under the M3 null (tokens
// permuted within each book) every pair of positions in a window holds a
// uniformly random pair of the book's tokens, so the null mean is exact:
// E = sum_book Q_book * sum_w C(n_w, 2), Q_book = qualifying pairs among
// the book's tokens / C(N_book, 2). Q1 compares S with M3 replicates (whose
// mean also cross-checks E); Q2 compares R = S / E of the standard table
// with R of random permutations of the 22 values over the 22 letters.
#include "gem/stats.hpp"
#include "pairs.hpp"
#include "rng.hpp"

extern "C" {
#include "gem/hebrew.h"
}

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numeric>
#include <thread>
#include <unordered_map>

namespace gem {

namespace {

using detail::c2;
using detail::count_pairs;
using detail::Item;

// Token data shared by every table and replicate.
struct Tokens {
    std::vector<uint32_t> first, len;        // letter span
    std::vector<uint32_t> lemma, mset;       // interned keys
    std::vector<uint32_t> book_lo, book_hi;  // token range of each book
    std::vector<std::vector<uint32_t>> verse_sizes, chapter_sizes;  // per book, in token order
};

// Lemma key (PREREG-003): the '/'-segments of the OSHB lemma that contain a
// digit, rejoined with '/'; prefix codes (b, c, d, l, k, m) drop out.
std::string lemma_key(const char *lemma)
{
    std::string key, seg;
    for (const char *p = lemma;; p++) {
        if (*p == '/' || *p == '\0') {
            if (seg.find_first_of("0123456789") != std::string::npos)
                key += (key.empty() ? "" : "/") + seg;
            seg.clear();
            if (*p == '\0')
                return key;
        } else {
            seg += *p;
        }
    }
}

Tokens build_tokens(const gem_corpus &c)
{
    Tokens t;
    std::unordered_map<std::string, uint32_t> lemmas, msets;
    for (uint32_t w = 0; w < c.n_words; w++) {
        const gem_word &word = c.words[w];
        t.first.push_back(word.first_letter);
        t.len.push_back(word.n_letters);
        const std::string key = lemma_key(c.strpool + word.lemma);
        t.lemma.push_back(lemmas.try_emplace(key, static_cast<uint32_t>(lemmas.size())).first->second);
        std::string ms;
        for (uint32_t i = 0; i < word.n_letters; i++)
            ms += static_cast<char>(gem_letter_base(c.letters[word.first_letter + i]));
        std::sort(ms.begin(), ms.end());
        t.mset.push_back(msets.try_emplace(ms, static_cast<uint32_t>(msets.size())).first->second);
    }
    for (uint32_t b = 0; b < c.n_books; b++) {
        const gem_book &bk = c.books[b];
        t.book_lo.push_back(bk.first_word);
        t.book_hi.push_back(bk.first_word + bk.n_words);
        std::vector<uint32_t> vs, cs;
        for (uint32_t v = bk.first_verse; v < bk.first_verse + bk.n_verses; v++) {
            uint32_t end = v + 1 < c.n_verses ? c.verses[v + 1].first_word : c.n_words;
            uint32_t n = std::min(end, bk.first_word + bk.n_words) - c.verses[v].first_word;
            vs.push_back(n);
            if (v == bk.first_verse || c.verses[v].chapter != c.verses[v - 1].chapter)
                cs.push_back(0);
            cs.back() += n;
        }
        t.verse_sizes.push_back(std::move(vs));
        t.chapter_sizes.push_back(std::move(cs));
    }
    return t;
}

std::vector<uint64_t> token_values(const gem_corpus &c, const Tokens &t, const uint32_t base_value[GEM_BASE_LETTERS])
{
    std::vector<uint64_t> v(t.first.size());
    for (size_t i = 0; i < v.size(); i++) {
        uint64_t s = 0;
        for (uint32_t k = 0; k < t.len[i]; k++)
            s += base_value[gem_letter_base(c.letters[t.first[i] + k])];
        v[i] = s;
    }
    return v;
}

struct SE {
    double s = 0, e = 0;
};

// S (and E if wanted) over books [0, books) for one window kind and K;
// order[i] = token at position i (identity for the observed text).
SE measure(const Tokens &t, const std::vector<uint64_t> &val, const std::vector<uint32_t> &order, uint32_t books,
           bool chapter, int kolel, bool want_e)
{
    SE r;
    std::vector<Item> items;
    std::vector<uint32_t> scratch;
    for (uint32_t b = 0; b < books; b++) {
        uint32_t pos = t.book_lo[b];
        uint64_t window_pairs = 0;
        for (uint32_t n : chapter ? t.chapter_sizes[b] : t.verse_sizes[b]) {
            items.clear();
            for (uint32_t i = pos; i < pos + n; i++) {
                const uint32_t tok = order[i];
                items.push_back({val[tok], t.lemma[tok], t.mset[tok]});
            }
            r.s += static_cast<double>(count_pairs(items, kolel, scratch));
            window_pairs += c2(n);
            pos += n;
        }
        if (want_e) {
            items.clear();
            for (uint32_t i = t.book_lo[b]; i < t.book_hi[b]; i++)
                items.push_back({val[i], t.lemma[i], t.mset[i]});
            const uint64_t nb = t.book_hi[b] - t.book_lo[b];
            const double q = static_cast<double>(count_pairs(items, kolel, scratch)) / static_cast<double>(c2(nb));
            r.e += q * static_cast<double>(window_pairs);
        }
    }
    return r;
}

template <class F>
void parallel_for(unsigned n, unsigned threads, F &&body)
{
    std::atomic<unsigned> next{0};
    std::vector<std::jthread> pool;
    for (unsigned th = 0; th < std::max(1u, threads); th++)
        pool.emplace_back([&] {
            for (unsigned i; (i = next.fetch_add(1, std::memory_order_relaxed)) < n;)
                body(i);
        });
}

void moments(const std::vector<double> &xs, double &mean, double &sd)
{
    double m = 0, ss = 0;
    for (double x : xs) {
        m += x;
        ss += x * x;
    }
    const double n = static_cast<double>(xs.size());
    mean = xs.empty() ? 0.0 : m / n;
    sd = xs.size() > 1 ? std::sqrt(std::max(0.0, (ss - m * m / n) / (n - 1))) : 0.0;
}

constexpr uint32_t kStandard[GEM_BASE_LETTERS] = {1,  2,  3,  4,  5,  6,  7,  8,   9,   10,  20,
                                                  30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400};

}  // namespace

std::vector<ClusterCell> run_cluster(const gem_corpus &c, unsigned tables, unsigned reps, unsigned threads)
{
    const Tokens t = build_tokens(c);
    const std::vector<uint64_t> std_val = token_values(c, t, kStandard);
    std::vector<uint32_t> identity(t.first.size());
    std::iota(identity.begin(), identity.end(), 0u);

    struct Combo {
        const char *scope;
        uint32_t books;
        const char *window;
        bool chapter;
        int kolel;
    };
    const Combo combos[] = {
        {"Pentateuch", 5, "verse", false, 0},          {"Pentateuch", 5, "chapter", true, 0},
        {"Tanakh", c.n_books, "verse", false, 0},      {"Tanakh", c.n_books, "chapter", true, 0},
        {"Pentateuch", 5, "verse", false, 1},          {"Pentateuch", 5, "chapter", true, 1},
        {"Tanakh", c.n_books, "verse", false, 1},      {"Tanakh", c.n_books, "chapter", true, 1},
    };
    constexpr size_t kCombos = sizeof combos / sizeof combos[0];

    std::vector<ClusterCell> cells(kCombos);
    for (size_t k = 0; k < kCombos; k++) {
        const SE obs = measure(t, std_val, identity, combos[k].books, combos[k].chapter, combos[k].kolel, true);
        cells[k].scope = combos[k].scope;
        cells[k].window = combos[k].window;
        cells[k].kolel = combos[k].kolel;
        cells[k].s = obs.s;
        cells[k].e = obs.e;
        cells[k].r = obs.e > 0 ? obs.s / obs.e : 0.0;
    }

    // Q1: M3 replicates under the standard table, all combos per replicate.
    std::vector<std::vector<double>> rep_s(kCombos, std::vector<double>(reps));
    parallel_for(reps, threads, [&](unsigned r) {
        Rng rng(replicate_seed(kSeed, 5, r));
        std::vector<uint32_t> order = identity;
        for (size_t b = 0; b < t.book_lo.size(); b++)
            for (uint32_t i = t.book_hi[b] - 1; i > t.book_lo[b]; i--)
                std::swap(order[i], order[t.book_lo[b] + rng.below(i - t.book_lo[b] + 1)]);
        for (size_t k = 0; k < kCombos; k++)
            rep_s[k][r] = measure(t, std_val, order, combos[k].books, combos[k].chapter, combos[k].kolel, false).s;
    });

    // Q2: placebo tables, exact R each.
    std::vector<std::vector<double>> tab_r(kCombos, std::vector<double>(tables));
    parallel_for(tables, threads, [&](unsigned i) {
        Rng rng(replicate_seed(kSeed, 4, i));
        uint32_t table[GEM_BASE_LETTERS];
        std::copy(std::begin(kStandard), std::end(kStandard), table);
        for (unsigned j = GEM_BASE_LETTERS - 1; j > 0; j--)
            std::swap(table[j], table[rng.below(j + 1)]);
        const std::vector<uint64_t> val = token_values(c, t, table);
        for (size_t k = 0; k < kCombos; k++) {
            const SE se = measure(t, val, identity, combos[k].books, combos[k].chapter, combos[k].kolel, true);
            tab_r[k][i] = se.e > 0 ? se.s / se.e : 0.0;
        }
    });

    for (size_t k = 0; k < kCombos; k++) {
        ClusterCell &cell = cells[k];
        cell.reps = reps;
        moments(rep_s[k], cell.rep_mean, cell.rep_sd);
        cell.q1_z = cell.rep_sd > 0 ? (cell.s - cell.rep_mean) / cell.rep_sd : 0.0;
        const auto ge_s = std::count_if(rep_s[k].begin(), rep_s[k].end(), [&](double x) { return x >= cell.s; });
        cell.q1_p = (1.0 + static_cast<double>(ge_s)) / (1.0 + reps);
        cell.tables = tables;
        moments(tab_r[k], cell.placebo_mean_r, cell.placebo_sd_r);
        const auto ge_r = std::count_if(tab_r[k].begin(), tab_r[k].end(), [&](double x) { return x >= cell.r; });
        cell.q2_p = (1.0 + static_cast<double>(ge_r)) / (1.0 + tables);
    }
    return cells;
}

}  // namespace gem
