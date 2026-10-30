// pairs_test - the PREREG-003 pair counter against an O(n^2) brute force,
// on random item sets built to hit every branch: equal values, values one
// apart, shared lemmas, shared multisets (always with equal values, as in
// real data where a multiset fixes the value), and both.
#include "../src/stats/pairs.hpp"
#include "../src/stats/rng.hpp"

#include <cstdio>

int main()
{
    gem::Rng rng(20261030u);
    std::vector<uint32_t> scratch;
    unsigned cases = 0, bad = 0;
    for (unsigned trial = 0; trial < 20000; trial++) {
        const unsigned n = static_cast<unsigned>(rng.below(40));
        const unsigned values = 1u + static_cast<unsigned>(rng.below(6));
        const unsigned lemmas = 1u + static_cast<unsigned>(rng.below(5));
        std::vector<gem::detail::Item> items;
        for (unsigned i = 0; i < n; i++) {
            const uint64_t v = rng.below(values);
            // multiset id determines the value: mset = v * 3 + small variant
            const uint32_t mset = static_cast<uint32_t>(v * 3u + rng.below(3));
            items.push_back({v, static_cast<uint32_t>(rng.below(lemmas)), mset});
        }
        for (int kolel = 0; kolel <= 1; kolel++) {
            const uint64_t want = gem::detail::count_pairs_brute(items, kolel);
            std::vector<gem::detail::Item> copy = items;
            const uint64_t got = gem::detail::count_pairs(copy, kolel, scratch);
            cases++;
            if (got != want && bad++ < 5)
                std::printf("MISMATCH trial %u kolel %d: got %llu want %llu\n", trial, kolel,
                            static_cast<unsigned long long>(got), static_cast<unsigned long long>(want));
        }
    }
    std::printf("pairs_test: %u cases, %u mismatches\n", cases, bad);
    return bad ? 1 : 0;
}
