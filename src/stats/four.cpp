// four.cpp - the four systems of stage K (hechrachi, gadol, katan, siduri)
// over the claim auditor's lexicon: the packed key of a word, and the census
// of type pairs with different letter multisets by the systems they agree
// in. Exploratory: base rates for the terminal front end, no test. The same
// counts come out of gem-kab's census of the exported text
// (src/kab/graph.cpp), an independent implementation.
#include "gem/stats.hpp"

#include <algorithm>
#include <stdexcept>
#include <tuple>

namespace gem {

namespace {

constexpr uint64_t choose2(uint64_t n)
{
    return n * (n - (n != 0)) / 2;
}

// The key bits of the systems in mask.
constexpr uint64_t lane_bits(unsigned mask)
{
    uint64_t bits = 0;
    for (unsigned s = 0; s < kab::kSystems; s++)
        if (mask >> s & 1u)
            bits |= uint64_t{0xFFFF} << (kab::kLaneBits * s);
    return bits;
}

}  // namespace

FourKey four_key(std::span<const uint8_t> codes)
{
    if (codes.size() > kab::kMaxWordLetters)
        throw std::length_error("word longer than the packed key holds");
    FourKey k;
    for (uint8_t code : codes) {
        k.key += kab::kPacked[code];
        k.letters += kab::kMultiset[code];
    }
    return k;
}

FourCensus four_census(const Lexicon &lex)
{
    std::vector<FourKey> keys(lex.size());
    for (uint32_t t = 0; t < lex.size(); t++)
        keys[t] = four_key(lex.letters(t));

    using Projected = std::tuple<uint64_t, uint64_t, uint64_t>;  // key lanes of the mask, multiset
    std::vector<Projected> rows(keys.size());
    FourCensus census;
    for (unsigned mask = 0; mask < census.at_least.size(); mask++) {
        const uint64_t bits = lane_bits(mask);
        for (size_t t = 0; t < keys.size(); t++)
            rows[t] = {keys[t].key & bits, keys[t].letters.lo, keys[t].letters.hi};
        std::sort(rows.begin(), rows.end());
        // Pairs equal in the mask's lanes, minus those that are also anagrams.
        uint64_t same_lanes = 0, same_lanes_and_letters = 0;
        for (size_t i = 0; i < rows.size();) {
            size_t j = i + 1;
            while (j < rows.size() && std::get<0>(rows[j]) == std::get<0>(rows[i]))
                j++;
            same_lanes += choose2(j - i);
            for (size_t a = i; a < j;) {
                size_t b = a + 1;
                while (b < j && rows[b] == rows[a])
                    b++;
                same_lanes_and_letters += choose2(b - a);
                a = b;
            }
            i = j;
        }
        census.at_least[mask] = same_lanes - same_lanes_and_letters;
    }
    census.pairs = census.at_least[0];
    return census;
}

}  // namespace gem
