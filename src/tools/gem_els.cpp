// gem_els - ELS tools (PREREG-004).
//
// Usage:
//   gem-els CORPUS find WORD BOOK [CH1 V1 CH2 V2] [DMAX]   list occurrences, 2 <= |d| <= DMAX (default 20)
//   gem-els CORPUS bench [DMAX] [ROUNDS]                   A/B of kernels P, S, R, H, A on Genesis targets
//   gem-els CORPUS export OUT.bin [REPS] [ref|noref|gen]   card A/B input; gen = replicates made on the device
//   gem-els CORPUS rambsel                                 PREREG-004 part A
//   gem-els CORPUS design [REPS] [KERNEL]                  PREREG-004 part B (KERNEL S, R, H or A)
//   gem-els CORPUS control TEXT.mc [REPS]                  EXPLORATORY: the Genesis targets in another letter stream
#include "gem/els.hpp"
#include "gem/stats.hpp"
#include "../stats/rng.hpp"

extern "C" {
#include "gem/hebrew.h"
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>
#include <thread>

namespace {

using Clock = std::chrono::steady_clock;

double since(Clock::time_point t0)
{
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

gem::Word word_of(const char *text)
{
    uint8_t codes[64];
    unsigned words = 0;
    long n = gem_parse_text(text, codes, sizeof codes, &words);
    if (n <= 0)
        throw std::runtime_error(std::string("not Hebrew: ") + text);
    gem::Word w;
    for (long i = 0; i < n; i++)
        w.push_back(gem_letter_base(codes[i]));
    return w;
}

std::string hebrew(const gem::Word &w)
{
    std::string s;
    char b[2];
    for (uint8_t x : w) {
        gem_letter_utf8(gem_base_code(x), b);
        s.append(b, 2);
    }
    return s;
}

uint32_t book_index(const gem_corpus &c, const std::string &osis)
{
    for (uint32_t b = 0; b < c.n_books; b++)
        if (osis == c.books[b].osis)
            return b;
    throw std::runtime_error("no book " + osis);
}

// Reference of corpus letter index i.
std::string letter_ref(const gem::Corpus &corpus, uint32_t i)
{
    const gem_corpus &c = corpus.raw();
    uint32_t lo = 0, hi = c.n_words;
    while (hi - lo > 1) {
        uint32_t mid = (lo + hi) / 2;
        (c.words[mid].first_letter <= i ? lo : hi) = mid;
    }
    return corpus.ref(lo);
}

// PREREG-004 B targets: the text's word types, loose, folded, 5 letters, >= 5 tokens.
std::vector<gem::Word> book_targets(const gem_corpus &c, uint32_t b)
{
    std::map<gem::Word, unsigned> count;
    const gem_book &bk = c.books[b];
    for (uint32_t w = bk.first_word; w < bk.first_word + bk.n_words; w++) {
        const gem_word &word = c.words[w];
        if (word.n_letters != 5)
            continue;
        gem::Word x;
        for (uint32_t i = 0; i < 5; i++)
            x.push_back(gem_letter_base(c.letters[word.first_letter + i]));
        count[x]++;
    }
    std::vector<gem::Word> out;
    for (const auto &[w, n] : count)
        if (n >= 5)
            out.push_back(w);
    return out;
}

std::vector<uint8_t> book_stream(const gem_corpus &c, uint32_t b)
{
    return gem::letter_stream(c, c.books[b].first_letter, c.books[b].first_letter + c.books[b].n_letters);
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

void shuffle(std::vector<uint8_t> &v, gem::Rng &rng)
{
    for (size_t i = v.size() - 1; i > 0; i--)
        std::swap(v[i], v[rng.below(i + 1)]);
}

int cmd_find(const gem::Corpus &corpus, int argc, char **argv)
{
    const gem_corpus &c = corpus.raw();
    const gem::Word w = word_of(argv[3]);
    gem::LetterRange r{};
    unsigned dmax = 20;
    if (argc >= 9) {
        r = gem::verse_letters(c, argv[4], static_cast<unsigned>(std::stoul(argv[5])), static_cast<unsigned>(std::stoul(argv[6])),
                               static_cast<unsigned>(std::stoul(argv[7])), static_cast<unsigned>(std::stoul(argv[8])));
        if (argc >= 10)
            dmax = static_cast<unsigned>(std::stoul(argv[9]));
    } else {
        const uint32_t b = book_index(c, argv[4]);
        r = {c.books[b].first_letter, c.books[b].first_letter + c.books[b].n_letters};
        if (argc >= 6)
            dmax = static_cast<unsigned>(std::stoul(argv[5]));
    }
    const auto L = gem::letter_stream(c, r.lo, r.hi);
    const auto hits = gem::els_find(L, w, 2, dmax);
    std::printf("%s: %zu occurrence(s) with 2 <= |d| <= %u in %u letters\n", hebrew(w).c_str(), hits.size(), dmax,
                r.hi - r.lo);
    for (size_t i = 0; i < std::min<size_t>(hits.size(), 40); i++)
        std::printf("  start %s (letter %u), skip %+d\n", letter_ref(corpus, r.lo + hits[i].start).c_str(),
                    r.lo + hits[i].start, hits[i].skip);
    return 0;
}

int cmd_bench(const gem::Corpus &corpus, unsigned dmax, unsigned rounds)
{
    const gem_corpus &c = corpus.raw();
    const auto L = book_stream(c, 0);
    const auto targets = book_targets(c, 0);
    const gem::Els5 table(targets);
    std::printf("bench: Genesis %zu letters, %zu targets (x2 directions), d = 2..%u, %u interleaved rounds, 1 thread\n",
                L.size(), targets.size(), dmax, rounds);
    struct Kernel {
        const char *name;
        std::function<gem::ElsCount()> run;
        std::vector<double> t;
        gem::ElsCount count;
    };
    std::vector<Kernel> kernels = {
        {"P plain per-word scan", [&] { return gem::els_count_plain(L, targets, 2, dmax); }, {}, {}},
        {"S multi-target, 5 streams", [&] { return table.count_streams(L, 2, dmax); }, {}, {}},
        {"R rolling key, 4 MB bitmap", [&] { return table.count_rolling(L, 2, dmax); }, {}, {}},
        {"H rolling key, 8 KB prefilter", [&] { return table.count_hashed(L, 2, dmax); }, {}, {}},
    };
    if (gem::Els5::has_avx2())
        kernels.push_back({"A AVX2, 8 starts per step", [&] { return table.count_avx2(L, 2, dmax); }, {}, {}});
    for (unsigned k = 0; k < rounds; k++)
        for (Kernel &kern : kernels) {
            const auto t0 = Clock::now();
            kern.count = kern.run();
            kern.t.push_back(since(t0));
        }
    bool same = true;
    for (Kernel &kern : kernels) {
        std::sort(kern.t.begin(), kern.t.end());
        std::printf("  %-30s total %8llu  min %8.4f s  median %8.4f s  max %8.4f s\n", kern.name,
                    static_cast<unsigned long long>(kern.count.total), kern.t.front(), kern.t[kern.t.size() / 2],
                    kern.t.back());
        const gem::ElsCount &ref = kernels[0].count;
        same &= kern.count.total == ref.total && kern.count.band[0] == ref.band[0] &&
                kern.count.band[1] == ref.band[1] && kern.count.band[2] == ref.band[2];
    }
    std::printf("  counts identical across kernels: %s\n", same ? "YES" : "NO");
    return same ? 0 : 1;
}

// Input of card/els_card.c: Genesis + REPS permuted replicates (PREREG-004 B
// seeds, analysis 8) + sorted (key, multiplicity) of the targets. Modes:
// "ref" prints the kernel-H count of every text (the A/B gate), "noref"
// skips it, "gen" stores only the observed text and asks the device to
// generate the replicates itself (header word 6 = analysis << 24 | REPS).
int cmd_export(const gem::Corpus &corpus, const char *path, unsigned reps, const std::string &mode)
{
    const bool reference = mode == "ref", generate = mode == "gen";
    const gem_corpus &c = corpus.raw();
    const auto L = book_stream(c, 0);
    const auto targets = book_targets(c, 0);
    const gem::Els5 table(targets);
    std::vector<uint32_t> keys;
    for (const gem::Word &w : targets)
        for (const gem::Word &x : {w, gem::Word(w.rbegin(), w.rend())}) {
            uint32_t k = 0;
            for (uint8_t letter : x)
                k = (k << 5) | letter;
            keys.push_back(k);
        }
    std::sort(keys.begin(), keys.end());
    std::vector<uint32_t> uniq, mult;
    for (size_t i = 0; i < keys.size();) {
        size_t j = i;
        while (j < keys.size() && keys[j] == keys[i])
            j++;
        uniq.push_back(keys[i]);
        mult.push_back(static_cast<uint32_t>(j - i));
        i = j;
    }
    FILE *f = std::fopen(path, "wb");
    if (!f)
        throw std::runtime_error(std::string("cannot write ") + path);
    uint32_t hdr[7] = {0, static_cast<uint32_t>(L.size()), generate ? 1u : 1u + reps,
                       static_cast<uint32_t>(uniq.size()), 2u, 1000u, generate ? (8u << 24) | reps : 0u};
    std::memcpy(hdr, "ELSB", 4);
    std::fwrite(hdr, sizeof hdr, 1, f);
    for (unsigned t = 0; t <= (generate ? 0u : reps); t++) {
        std::vector<uint8_t> text = L;
        if (t > 0) {
            gem::Rng rng(gem::replicate_seed(gem::kSeed, 8, t - 1));
            shuffle(text, rng);
        }
        std::fwrite(text.data(), 1, text.size(), f);
        if (!reference)
            continue;
        const gem::ElsCount x = table.count_hashed(text, 2, 1000);
        std::printf("text %u total %llu bands %llu %llu %llu\n", t, static_cast<unsigned long long>(x.total),
                    static_cast<unsigned long long>(x.band[0]), static_cast<unsigned long long>(x.band[1]),
                    static_cast<unsigned long long>(x.band[2]));
    }
    std::fwrite(uniq.data(), sizeof(uint32_t), uniq.size(), f);
    std::fwrite(mult.data(), sizeof(uint32_t), mult.size(), f);
    std::fclose(f);
    return 0;
}

int cmd_rambsel(const gem::Corpus &corpus, unsigned threads)
{
    const gem_corpus &c = corpus.raw();
    const auto tanakh = gem::letter_stream(c, 0, c.n_letters);
    double freq[GEM_BASE_LETTERS] = {};
    for (uint8_t x : tanakh)
        freq[x] += 1.0;
    for (double &f : freq)
        f /= static_cast<double>(tanakh.size());
    auto prob = [&](const gem::Word &w) {
        double p = 1;
        for (uint8_t x : w)
            p *= freq[x];
        return p;
    };
    // Expected occurrences, both directions, 2 <= d <= dmax, n letters.
    auto expected = [&](const gem::Word &w, size_t n, unsigned dmax) {
        double e = 0;
        for (unsigned d = 2; d <= dmax; d++) {
            const double starts = static_cast<double>(n) - static_cast<double>((w.size() - 1) * d);
            if (starts > 0)
                e += 2.0 * starts * prob(w);
        }
        return e;
    };
    const gem::Word yeshua = word_of("ישוע"), claim = word_of("ישועשמי");

    std::printf("PREREG-004 part A: Rambsel/Jeffrey, Isaiah 52:13-53:12\n");
    // A1: second yod of the word yaarikh in Isa 53:10, read back by 20.
    const gem::LetterRange v = gem::verse_letters(c, "Isa", 53, 10, 53, 10);
    const gem::Word yaarikh = word_of("יאריך");
    int64_t yod = -1;
    for (uint32_t w = 0; w < c.n_words; w++) {
        const gem_word &word = c.words[w];
        if (word.first_letter < v.lo || word.first_letter >= v.hi || word.n_letters != 5)
            continue;
        gem::Word x;
        for (uint32_t i = 0; i < 5; i++)
            x.push_back(gem_letter_base(c.letters[word.first_letter + i]));
        if (x == yaarikh)
            yod = word.first_letter + 3;
    }
    if (yod < 0)
        throw std::runtime_error("yaarikh not found in Isa 53:10");
    gem::Word read;
    for (int j = 0; j < 7; j++)
        read.push_back(tanakh[static_cast<size_t>(yod - 20 * j)]);
    std::printf("A1 from the second yod of %s (letter %lld, %s) back by 20: %s  -> %s\n", hebrew(yaarikh).c_str(),
                static_cast<long long>(yod), letter_ref(corpus, static_cast<uint32_t>(yod)).c_str(), hebrew(read).c_str(),
                read == claim ? "PRESENT (claim's ELS exists in WLC)" : "ABSENT");

    // A2: Yeshua in the passage.
    const gem::LetterRange pr = gem::verse_letters(c, "Isa", 52, 13, 53, 12);
    const auto passage = gem::letter_stream(c, pr.lo, pr.hi);
    const size_t obs = gem::els_find(passage, yeshua, 2, 20).size();
    const double e4 = expected(yeshua, passage.size(), 20);
    constexpr unsigned kReps = 1000;
    std::vector<size_t> reps(kReps);
    parallel_for(kReps, threads, [&](unsigned r) {
        gem::Rng rng(gem::replicate_seed(gem::kSeed, 7, r));
        auto perm = passage;
        shuffle(perm, rng);
        reps[r] = gem::els_find(perm, yeshua, 2, 20).size();
    });
    double m = 0, ss = 0;
    unsigned ge = 0;
    for (size_t x : reps) {
        m += static_cast<double>(x);
        ss += static_cast<double>(x) * static_cast<double>(x);
        ge += x >= obs;
    }
    m /= kReps;
    const double sd = std::sqrt(std::max(0.0, (ss - m * m * kReps) / (kReps - 1)));
    std::printf("A2 %s, 2<=|d|<=20, passage %zu letters: observed %zu, expected (iid) %.3f, "
                "M1 mean %.3f sd %.3f, p = %.4f\n",
                hebrew(yeshua).c_str(), passage.size(), obs, e4, m, sd, (1.0 + ge) / (1.0 + kReps));

    // A3: the 7-letter claim.
    const double e7 = expected(claim, passage.size(), 20);
    const double e7t = expected(claim, tanakh.size(), 20);
    const size_t o7t = gem::els_find(tanakh, claim, 2, 20).size();
    std::printf("A3 %s, 2<=|d|<=20: passage expected %.3e, P(at least one) %.3e = 1 in %.0f "
                "(claimed: 1 in 5e16)\n",
                hebrew(claim).c_str(), e7, 1.0 - std::exp(-e7), 1.0 / (1.0 - std::exp(-e7)));
    std::printf("   whole Tanakh (%zu letters): expected %.3f, observed %zu\n", tanakh.size(), e7t, o7t);
    return 0;
}

int cmd_design(const gem::Corpus &corpus, unsigned reps, char kernel, unsigned threads)
{
    const gem_corpus &c = corpus.raw();
    std::printf("PREREG-004 part B: own 5-letter words as ELS, 2 <= d <= 1000 both directions, kernel %c, "
                "%u M1 replicates\n", kernel, reps);
    const struct {
        const char *osis;
        unsigned analysis;
    } texts[] = {{"Gen", 8}, {"Ps", 9}};
    for (const auto &tx : texts) {
        const uint32_t b = book_index(c, tx.osis);
        const auto L = book_stream(c, b);
        const auto targets = book_targets(c, b);
        const gem::Els5 table(targets);
        if (kernel == 'A' && !gem::Els5::has_avx2())
            throw std::runtime_error("kernel A needs a CPU with AVX2");
        auto run = [&](const std::vector<uint8_t> &s) {
            return kernel == 'S'   ? table.count_streams(s, 2, 1000)
                   : kernel == 'H' ? table.count_hashed(s, 2, 1000)
                   : kernel == 'A' ? table.count_avx2(s, 2, 1000)
                                   : table.count_rolling(s, 2, 1000);
        };
        const auto t0 = Clock::now();
        const gem::ElsCount obs = run(L);
        std::vector<uint64_t> rep(reps);
        parallel_for(reps, threads, [&](unsigned r) {
            gem::Rng rng(gem::replicate_seed(gem::kSeed, tx.analysis, r));
            auto perm = L;
            shuffle(perm, rng);
            rep[r] = run(perm).total;
        });
        double m = 0, ss = 0;
        unsigned ge = 0;
        for (uint64_t x : rep) {
            m += static_cast<double>(x);
            ss += static_cast<double>(x) * static_cast<double>(x);
            ge += x >= obs.total;
        }
        m /= reps;
        const double sd = std::sqrt(std::max(0.0, (ss - m * m * reps) / (reps - 1)));
        std::printf("%-4s %6zu letters, %4zu targets: T = %llu (bands %llu / %llu / %llu), M1 mean %.1f sd %.1f, "
                    "z %+.2f, p = %.4f%s  [%.1f s]\n",
                    tx.osis, L.size(), targets.size(), static_cast<unsigned long long>(obs.total),
                    static_cast<unsigned long long>(obs.band[0]), static_cast<unsigned long long>(obs.band[1]),
                    static_cast<unsigned long long>(obs.band[2]), m, sd, sd > 0 ? (obs.total - m) / sd : 0.0,
                    (1.0 + ge) / (1.0 + reps), b == 0 ? "  <- PRIMARY" : "  (control)", since(t0));
    }
    return 0;
}

// EXPLORATORY control: the Genesis targets of part B counted as ELS in any
// letter stream given in Michigan-Claremont transliteration (everything that
// is not one of its 22 letters is skipped), against letter permutations of
// that stream. Used with the first 78,064 letters of the Hebrew War and
// Peace and with the Koren Genesis.
int cmd_control(const gem::Corpus &corpus, const char *path, unsigned reps, unsigned threads)
{
    static const char mc[] = ")BGDHWZX+YKLMNS(PCQR$T";
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error(std::string("cannot read ") + path);
    std::vector<uint8_t> L;
    for (char ch; in.get(ch);)
        if (const char *at = ch ? std::strchr(mc, ch) : nullptr)
            L.push_back(static_cast<uint8_t>(at - mc));

    const gem_corpus &c = corpus.raw();
    const auto targets = book_targets(c, 0);
    const gem::Els5 table(targets);
    const bool avx2 = gem::Els5::has_avx2();
    auto run = [&](const std::vector<uint8_t> &s) {
        return avx2 ? table.count_avx2(s, 2, 1000) : table.count_hashed(s, 2, 1000);
    };
    const auto t0 = Clock::now();
    const gem::ElsCount obs = run(L);
    std::vector<uint64_t> rep(reps);
    parallel_for(reps, threads, [&](unsigned r) {
        gem::Rng rng(gem::replicate_seed(gem::kSeed, 10, r));
        auto perm = L;
        shuffle(perm, rng);
        rep[r] = run(perm).total;
    });
    double m = 0, ss = 0;
    unsigned ge = 0;
    for (uint64_t x : rep) {
        m += static_cast<double>(x);
        ss += static_cast<double>(x) * static_cast<double>(x);
        ge += x >= obs.total;
    }
    m /= reps;
    const double sd = std::sqrt(std::max(0.0, (ss - m * m * reps) / (reps - 1)));
    std::printf("CONTROL EXPLORATORY (not preregistered): the %zu Genesis targets of part B as ELS in %s, "
                "2 <= d <= 1000 both directions, kernel %c, %u letter permutations\n",
                targets.size(), path, avx2 ? 'A' : 'H', reps);
    std::printf("%zu letters: T = %llu (bands %llu / %llu / %llu), permuted mean %.1f sd %.1f, z %+.2f, p = %.4f  "
                "[%.1f s]\n",
                L.size(), static_cast<unsigned long long>(obs.total), static_cast<unsigned long long>(obs.band[0]),
                static_cast<unsigned long long>(obs.band[1]), static_cast<unsigned long long>(obs.band[2]), m, sd,
                sd > 0 ? (obs.total - m) / sd : 0.0, (1.0 + ge) / (1.0 + reps), since(t0));
    return 0;
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s CORPUS find|bench|rambsel|design|control ...\n", argv[0]);
        return 2;
    }
    try {
        std::printf("frozen: %s\n", gem::frozen_status(argv[1], nullptr).c_str());
        gem::Corpus corpus(argv[1]);
        const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
        const std::string cmd = argv[2];
        if (cmd == "find" && argc >= 5)
            return cmd_find(corpus, argc, argv);
        if (cmd == "bench")
            return cmd_bench(corpus, argc > 3 ? static_cast<unsigned>(std::stoul(argv[3])) : 200u,
                             argc > 4 ? static_cast<unsigned>(std::stoul(argv[4])) : 5u);
        if (cmd == "export" && argc >= 4)
            return cmd_export(corpus, argv[3], argc > 4 ? static_cast<unsigned>(std::stoul(argv[4])) : 7u,
                              argc > 5 ? std::string(argv[5]) : std::string("ref"));
        if (cmd == "rambsel")
            return cmd_rambsel(corpus, threads);
        if (cmd == "design")
            return cmd_design(corpus, argc > 3 ? static_cast<unsigned>(std::stoul(argv[3])) : 1000u,
                              argc > 4 ? argv[4][0] : 'R', threads);
        if (cmd == "control" && argc >= 4)
            return cmd_control(corpus, argv[3], argc > 4 ? static_cast<unsigned>(std::stoul(argv[4])) : 1000u,
                               threads);
        std::fprintf(stderr, "bad command\n");
        return 2;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
