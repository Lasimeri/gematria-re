// kab_graph.cpp - gem-kab graph / verify / pair: the shared-value graph and
// the pair census.
//
//   graph        the CSR graph per system, the census of pairs by agreement
//                mask, and how much agreement in one system raises the rate
//                of agreement in another
//   verify       the census by grouping against the brute-force census over
//                every pair, and the graph's edge counts against both
//   pair A B     one claimed equality: values, agreement mask, bucket sizes
//                and the corpus base rate of that much agreement (its price)
#include "kab_cmd.hpp"

#include "gem/kab/graph.hpp"
#include "gem/kab/vectors.hpp"

#include <bit>
#include <chrono>
#include <cstdio>

namespace kabtool {

using namespace gem::kab;

namespace {

double seconds_since(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

// Pairs with different letter multisets agreeing in at least mask.
uint64_t others_at_least(const PairCensus &census, unsigned mask)
{
    return census.at_least(mask) - census.anagrams_at_least(mask);
}

void print_census(const PairCensus &census)
{
    uint64_t others = others_at_least(census, 0);
    std::printf("  %-5s %15s %10s %15s  %s\n", "mask", "pairs (exact)", "anagrams", "other pairs",
                "1 in N of the other pairs");
    for (unsigned mask = 0; mask < kMasks; mask++) {
        uint64_t other = census.exact[mask] - census.anagram[mask];
        std::printf("  %-5s %15s %10s %15s  ", mask_name(mask).c_str(), commas(census.exact[mask]).c_str(),
                    commas(census.anagram[mask]).c_str(), commas(other).c_str());
        if (other)
            std::printf("%.1f\n", static_cast<double>(others) / static_cast<double>(other));
        else
            std::printf("-\n");
    }
    std::printf("  %-5s %15s %10s %15s\n", "all", commas(census.pairs).c_str(),
                commas(census.anagrams_at_least(0)).c_str(), commas(others).c_str());
}

// Row r, column s: among pairs (different multisets) agreeing in system r,
// the share that also agree in system s; first row: the unconditional share.
void print_dependence(const PairCensus &census)
{
    double others = static_cast<double>(others_at_least(census, 0));
    std::printf("  %-22s", "share agreeing in:");
    for (unsigned s = 0; s < kSystems; s++)
        std::printf(" %12s", kSystemName[s]);
    std::printf("\n  %-22s", "all pairs");
    for (unsigned s = 0; s < kSystems; s++)
        std::printf(" %11.4f%%", 100.0 * static_cast<double>(others_at_least(census, 1u << s)) / others);
    for (unsigned r = 0; r < kSystems; r++) {
        std::printf("\n  given %-16s", kSystemName[r]);
        double given = static_cast<double>(others_at_least(census, 1u << r));
        for (unsigned s = 0; s < kSystems; s++)
            std::printf(" %11.4f%%", 100.0 * static_cast<double>(others_at_least(census, 1u << r | 1u << s)) / given);
    }
    // What four independent systems with these single-system rates would give.
    double independent = others;
    for (unsigned s = 0; s < kSystems; s++)
        independent *= static_cast<double>(others_at_least(census, 1u << s)) / others;
    uint64_t observed = others_at_least(census, kAllSystems);
    std::printf("\n  agreeing in all four: %s pairs observed; %.3f expected if the systems were independent "
                "(arithmetic: product of the four shares x pairs); ratio %.0f\n",
                commas(observed).c_str(), independent, static_cast<double>(observed) / independent);
}

}  // namespace

int cmd_graph(const Corpus &c, Args)
{
    auto t0 = std::chrono::steady_clock::now();
    ValueGraph graph(c);
    double graph_s = seconds_since(t0);
    std::printf("shared-value graph, clique form (CSR per system), %s types, built in %.2f ms:\n",
                commas(c.types()).c_str(), graph_s * 1e3);
    std::printf("  %-9s %7s %15s %15s\n", "system", "values", "largest bucket", "edges");
    for (unsigned s = 0; s < kSystems; s++)
        std::printf("  %-9s %7u %15u %15s\n", kSystemName[s], graph.values(static_cast<System>(s)),
                    graph.largest_bucket(static_cast<System>(s)), commas(graph.edges(static_cast<System>(s))).c_str());

    t0 = std::chrono::steady_clock::now();
    PairCensus census = census_by_grouping(c);
    std::printf("pair census by grouping (15 projections + Moebius inversion), %.1f ms;\n"
                "mask letters: H hechrachi, G gadol, K katan, S siduri; a pair is counted under the exact set it "
                "agrees in:\n",
                seconds_since(t0) * 1e3);
    print_census(census);
    std::printf("dependence between the systems (pairs with different letter multisets):\n");
    print_dependence(census);
    return 0;
}

int cmd_verify(const Corpus &c, Args)
{
    auto t0 = std::chrono::steady_clock::now();
    PairCensus grouped = census_by_grouping(c);
    double grouped_s = seconds_since(t0);
    t0 = std::chrono::steady_clock::now();
    PairCensus brute = census_brute(c, true);
    double brute_s = seconds_since(t0);

    unsigned bad = 0;
    for (unsigned mask = 0; mask < kMasks; mask++)
        bad += grouped.exact[mask] != brute.exact[mask] || grouped.anagram[mask] != brute.anagram[mask];
    bad += grouped.pairs != brute.pairs;
    std::printf("census by grouping (%.1f ms) against brute force over %s pairs (%.2f s): %u of 33 counts differ\n",
                grouped_s * 1e3, commas(brute.pairs).c_str(), brute_s, bad);

    ValueGraph graph(c);
    for (unsigned s = 0; s < kSystems; s++) {
        bool same = graph.edges(static_cast<System>(s)) == brute.at_least(1u << s);
        bad += !same;
        std::printf("  %-9s graph edges %15s  brute-force pairs %15s  %s\n", kSystemName[s],
                    commas(graph.edges(static_cast<System>(s))).c_str(), commas(brute.at_least(1u << s)).c_str(),
                    same ? "equal" : "DIFFER");
    }
    uint64_t weighted = 0;
    for (unsigned mask = 0; mask < kMasks; mask++)
        weighted += mask * brute.exact[mask];
    uint64_t checksum = agreement_checksum(c.keys(), false);
    bad += checksum != weighted;
    std::printf("  plain-kernel mask checksum %s, census-weighted sum %s  %s\n", commas(checksum).c_str(),
                commas(weighted).c_str(), checksum == weighted ? "equal" : "DIFFER");
    std::printf("verify: %s\n", bad ? "FAILED" : "all counts agree");
    return bad ? 1 : 0;
}

int cmd_pair(const Corpus &c, Args args)
{
    std::vector<Word> a = args.size() == 2 ? split_words(args[0]) : std::vector<Word>{};
    std::vector<Word> b = args.size() == 2 ? split_words(args[1]) : std::vector<Word>{};
    if (a.size() != 1 || b.size() != 1 || a[0].letters > kMaxWordLetters || b[0].letters > kMaxWordLetters) {
        std::fprintf(stderr, "usage: pair A B   (each one Hebrew word of at most %u letters)\n", kMaxWordLetters);
        return 2;
    }
    const Word &x = a[0], &y = b[0];
    ValueGraph graph(c);
    PairCensus census = census_by_grouping(c);
    unsigned mask = agree_swar(x.key, y.key);
    bool anagram = x.multiset == y.multiset;
    alignas(64) float vx[kDim], vy[kDim];
    unit_vector(x.multiset, x.key, vx);
    unit_vector(y.multiset, y.key, vy);

    std::printf("pair %s / %s: agreement %s (%d of %u systems)%s, cosine %.4f\n", x.text.c_str(), y.text.c_str(),
                mask_name(mask).c_str(), std::popcount(mask), kSystems, anagram ? ", ANAGRAMS" : "",
                static_cast<double>(dot_plain(vx, vy)));
    std::printf("  %-9s %8s %8s  %s\n", "system", "A", "B", "types carrying A's value / B's value");
    for (unsigned s = 0; s < kSystems; s++) {
        System sys = static_cast<System>(s);
        std::printf("  %-9s %8u %8u  %zu / %zu%s\n", kSystemName[s], lane(x.key, sys), lane(y.key, sys),
                    graph.bucket(sys, lane(x.key, sys)).size(), graph.bucket(sys, lane(y.key, sys)).size(),
                    mask >> s & 1u ? "   (equal)" : "");
    }
    uint64_t others = others_at_least(census, 0), as_much = others_at_least(census, mask);
    std::printf("  price: %s of %s type pairs with different letter multisets agree in at least %s = 1 in %.1f\n",
                commas(as_much).c_str(), commas(others).c_str(), mask_name(mask).c_str(),
                as_much ? static_cast<double>(others) / static_cast<double>(as_much) : 0.0);
    std::printf("  (4 systems declared, no kolel: 4 degrees of freedom; a base rate, not a significance test)\n");
    return 0;
}

}  // namespace kabtool
