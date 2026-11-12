// graph.hpp - stage K, hash equalities and the shared-value graph of word
// types.
//
// Nodes are word types. Two types are adjacent in system s iff they carry
// the same value in s. The graph is stored in clique form, one CSR per
// system with the types grouped by value (a counting sort): the neighbours
// of a type are the other members of its bucket. Explicit edge lists would
// hold sum C(n, 2) entries per system; the buckets hold the same graph in
// one entry per type.
//
// Pair census: for each of the 16 subsets M of the four systems, the number
// of unordered type pairs that agree in exactly the systems of M. Two
// independent computations:
//   census_by_grouping  for every non-empty subset S the keys are projected
//                       onto the lanes of S and grouped; the pairs inside the
//                       groups agree in at least S; Moebius inversion over
//                       the supersets gives the exact-mask counts
//   census_brute        every pair through the lane-agreement kernel
// Anagram pairs (equal letter multisets) are counted per mask as well: the
// values of hechrachi, katan and siduri ignore letter order, so those pairs
// agree there by construction and say nothing.
#pragma once

#include "gem/kab/corpus.hpp"

#include <array>
#include <span>
#include <vector>

namespace gem::kab {

inline constexpr unsigned kMasks = 1u << kSystems;
inline constexpr unsigned kAllSystems = kMasks - 1u;

// The key bits of the systems in mask.
constexpr uint64_t lane_mask(unsigned mask)
{
    uint64_t bits = 0;
    for (unsigned s = 0; s < kSystems; s++)
        if (mask >> s & 1u)
            bits |= uint64_t{0xFFFF} << (kLaneBits * s);
    return bits;
}

struct PairCensus {
    uint64_t pairs = 0;                      // all unordered pairs of types
    std::array<uint64_t, kMasks> exact{};    // agreeing in exactly the mask's systems
    std::array<uint64_t, kMasks> anagram{};  // of those, pairs with equal letter multisets
    // Pairs agreeing in at least the systems of mask.
    uint64_t at_least(unsigned mask) const;
    uint64_t anagrams_at_least(unsigned mask) const;
    bool operator==(const PairCensus &) const = default;
};

PairCensus census_by_grouping(const Corpus &c);
PairCensus census_brute(const Corpus &c, bool swar);

// Sum of the agreement masks of all pairs (= sum of M x exact[M]), with the
// SWAR or the plain kernel: the timing target of the kernel A/B.
uint64_t agreement_checksum(std::span<const uint64_t> keys, bool swar);

class ValueGraph {
public:
    explicit ValueGraph(const Corpus &c);
    // Types whose value in system s is `value` (empty if none).
    std::span<const uint32_t> bucket(System s, unsigned value) const;
    // Neighbours of type t in system s.
    uint32_t degree(uint32_t t, System s) const;
    uint64_t edges(System s) const { return csr_[s].edges; }
    uint32_t values(System s) const { return csr_[s].values; }
    uint32_t largest_bucket(System s) const { return csr_[s].largest; }

private:
    struct Csr {
        std::vector<uint32_t> start;   // start[v] .. start[v + 1] index member
        std::vector<uint32_t> member;  // type ids, grouped by value
        uint64_t edges = 0;
        uint32_t values = 0, largest = 0;
    };
    const Corpus *corpus_;
    std::array<Csr, kSystems> csr_;
};

// Hash equalities: the types grouped by the lanes of mask of their packed
// key, so one bucket = one set of types equal in every system of mask. Only
// types occurring in every corpus of require_corpora (a bit mask, 0 = no
// restriction) are entered.
using KeyIndex = std::unordered_map<uint64_t, std::vector<uint32_t>>;
KeyIndex hash_equalities(const Corpus &c, unsigned mask, unsigned require_corpora);

struct Match {
    uint32_t a, b;  // type ids, a < b
};

// The pairs inside the buckets of hash_equalities(c, mask, require_corpora)
// whose letter multisets differ. Ordered by (a, b).
std::vector<Match> matches(const Corpus &c, unsigned mask, unsigned require_corpora);

}  // namespace gem::kab
