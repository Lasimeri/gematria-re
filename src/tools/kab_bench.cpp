// kab_bench.cpp - gem-kab bench: each unconventional kernel of stage K
// against the plain form it replaces, on the same data, interleaved.
//
//   bench [ROUNDS]     default 5 rounds; per variant: median (min - max)
//
//   sums     word sums of every token: one packed add per letter (four
//            systems in four 16-bit lanes) vs four table lookups and four
//            accumulators per letter
//   agree    agreement mask of every pair of types: agree_swar vs
//            agree_plain (four lane compares), summed over all pairs
//   index    value index of the four systems: unordered_map<int,
//            vector<string>> vs the CSR counting sort of ValueGraph
//   cosine   one query against every vector: scan_avx2 vs scan_plain
// Each pair of variants must produce the same result, or the bench fails.
#include "kab_cmd.hpp"

#include "gem/kab/graph.hpp"
#include "gem/kab/vectors.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace kabtool {

using namespace gem::kab;

namespace {

struct Letters {
    std::vector<uint8_t> code;    // letter codes of every type, concatenated
    std::vector<uint32_t> start;  // start[t] .. start[t + 1]
};

// Every letter of U+05D0..U+05EA is the two bytes D7, 90 + code.
Letters letters_of(const Corpus &c)
{
    Letters l;
    l.start.push_back(0);
    for (uint32_t t = 0; t < c.types(); t++) {
        const std::string &text = c.text(t);
        for (size_t i = 1; i < text.size(); i += 2)
            l.code.push_back(static_cast<uint8_t>(static_cast<unsigned char>(text[i]) - 0x90u));
        l.start.push_back(static_cast<uint32_t>(l.code.size()));
    }
    return l;
}

[[gnu::noinline]] uint64_t sums_packed(const Letters &l, const std::vector<uint32_t> &tokens)
{
    uint64_t total = 0;
    for (uint32_t t : tokens) {
        uint64_t key = 0;
        for (uint32_t i = l.start[t]; i < l.start[t + 1]; i++)
            key += kPacked[l.code[i]];
        total += key;
    }
    return total;
}

[[gnu::noinline]] uint64_t sums_plain(const Letters &l, const std::vector<uint32_t> &tokens)
{
    uint64_t total = 0;
    for (uint32_t t : tokens) {
        uint32_t h = 0, g = 0, k = 0, s = 0;
        for (uint32_t i = l.start[t]; i < l.start[t + 1]; i++) {
            const LetterRow &r = kLetters[l.code[i]];
            h += r.value[kHechrachi];
            g += r.value[kGadol];
            k += r.value[kKatan];
            s += r.value[kSiduri];
        }
        total += static_cast<uint64_t>(h) | static_cast<uint64_t>(g) << 16 | static_cast<uint64_t>(k) << 32 |
                 static_cast<uint64_t>(s) << 48;
    }
    return total;
}

uint64_t index_map(const Corpus &c)
{
    uint64_t edges = 0;
    for (unsigned s = 0; s < kSystems; s++)
        for (const auto &[value, members] : c.word_index(static_cast<System>(s)))
            edges += static_cast<uint64_t>(members.size()) * (members.size() - 1) / 2;
    return edges;
}

uint64_t index_csr(const Corpus &c)
{
    ValueGraph graph(c);
    uint64_t edges = 0;
    for (unsigned s = 0; s < kSystems; s++)
        edges += graph.edges(static_cast<System>(s));
    return edges;
}

struct Timing {
    std::vector<double> seconds;
    double median() const
    {
        std::vector<double> v = seconds;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    }
    double min() const { return *std::min_element(seconds.begin(), seconds.end()); }
    double max() const { return *std::max_element(seconds.begin(), seconds.end()); }
};

template <typename F>
auto timed(Timing &t, F f)
{
    auto t0 = std::chrono::steady_clock::now();
    auto result = f();
    t.seconds.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    return result;
}

// Two ratios: of the medians, and of the fastest rounds. The fastest round
// is the one least disturbed by other load on the machine, so its ratio is
// the stable one when the rounds spread widely.
void report(const char *name, const char *plain_name, const Timing &plain, const char *other_name, const Timing &other,
            bool same)
{
    std::printf("  %-7s %-34s %9.3f ms (%.3f - %.3f)\n", name, plain_name, plain.median() * 1e3, plain.min() * 1e3,
                plain.max() * 1e3);
    std::printf("  %-7s %-34s %9.3f ms (%.3f - %.3f)   median %.2fx, fastest %.2fx, results %s\n", "", other_name,
                other.median() * 1e3, other.min() * 1e3, other.max() * 1e3, plain.median() / other.median(),
                plain.min() / other.min(), same ? "identical" : "DIFFER");
}

}  // namespace

int cmd_bench(const Corpus &c, Args args)
{
    unsigned rounds = args.empty() ? 5u : static_cast<unsigned>(std::atoi(args[0]));
    if (rounds == 0 || !cpu_has_avx2_fma()) {
        std::fprintf(stderr, "bench: needs ROUNDS >= 1 and a CPU with AVX2 and FMA\n");
        return 2;
    }
    const unsigned sum_passes = 20, queries = 64;
    Letters letters = letters_of(c);
    Vectors vectors(c);
    std::vector<float> out_plain(vectors.count()), out_avx2(vectors.count());

    Timing sum_p, sum_k, agree_p, agree_s, index_m, index_c, cos_p, cos_a;
    bool sums_same = true, agree_same = true, index_same = true;
    double cos_diff = 0;
    for (unsigned r = 0; r < rounds; r++) {
        uint64_t a = timed(sum_p, [&] {
            uint64_t x = 0;
            for (unsigned p = 0; p < sum_passes; p++)
                x += sums_plain(letters, c.token_types());
            return x;
        });
        uint64_t b = timed(sum_k, [&] {
            uint64_t x = 0;
            for (unsigned p = 0; p < sum_passes; p++)
                x += sums_packed(letters, c.token_types());
            return x;
        });
        sums_same &= a == b;
        agree_same &= timed(agree_p, [&] { return agreement_checksum(c.keys(), false); }) ==
                      timed(agree_s, [&] { return agreement_checksum(c.keys(), true); });
        index_same &= timed(index_m, [&] { return index_map(c); }) == timed(index_c, [&] { return index_csr(c); });
        timed(cos_p, [&] {
            for (unsigned q = 0; q < queries; q++)
                scan_plain(vectors.data(), vectors.count(), vectors.row(q * (vectors.count() / queries)),
                           out_plain.data());
            return 0;
        });
        timed(cos_a, [&] {
            for (unsigned q = 0; q < queries; q++)
                scan_avx2(vectors.data(), vectors.count(), vectors.row(q * (vectors.count() / queries)),
                          out_avx2.data());
            return 0;
        });
        for (uint32_t t = 0; t < vectors.count(); t++)
            cos_diff = std::max(cos_diff, static_cast<double>(std::fabs(out_plain[t] - out_avx2[t])));
    }

    std::printf("bench: %u interleaved rounds, one thread, median (min - max); %s types, %s tokens, %s pairs\n", rounds,
                commas(c.types()).c_str(), commas(c.token_types().size()).c_str(),
                commas(static_cast<uint64_t>(c.types()) * (c.types() - 1u) / 2u).c_str());
    report("sums", "4 lookups + 4 accumulators, x20", sum_p, "1 packed add per letter, x20", sum_k, sums_same);
    report("agree", "agree_plain, all pairs", agree_p, "agree_swar, all pairs", agree_s, agree_same);
    report("index", "unordered_map<int, vector<string>>", index_m, "CSR counting sort", index_c, index_same);
    report("cosine", "scan_plain, 64 queries", cos_p, "scan_avx2, 64 queries", cos_a, cos_diff < 1e-5);
    std::printf("  cosine: largest |plain - avx2| over the last query = %.2e\n", cos_diff);
    return sums_same && agree_same && index_same && cos_diff < 1e-5 ? 0 : 1;
}

}  // namespace kabtool
