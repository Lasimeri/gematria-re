// graph.cpp - stage K, hash equalities, the pair census and the
// shared-value graph. Definitions: graph.hpp.
#include "gem/kab/graph.hpp"

#include <algorithm>
#include <bit>
#include <numeric>

namespace gem::kab {

namespace {

constexpr uint64_t choose2(uint64_t n)
{
    return n * (n - (n != 0)) / 2;
}

// Types sorted by letter multiset: every run of equal multisets is one set
// of anagrams, and each pair inside it is counted under its agreement mask.
void count_anagrams(const Corpus &c, PairCensus &r)
{
    std::vector<uint32_t> order(c.types());
    std::iota(order.begin(), order.end(), 0u);
    auto letters = [&](uint32_t t) { return std::pair{c.multiset(t).lo, c.multiset(t).hi}; };
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return letters(a) < letters(b); });
    for (size_t first = 0; first < order.size();) {
        size_t last = first + 1;
        while (last < order.size() && letters(order[last]) == letters(order[first]))
            last++;
        for (size_t i = first; i < last; i++)
            for (size_t j = i + 1; j < last; j++)
                r.anagram[agree_swar(c.key(order[i]), c.key(order[j]))]++;
        first = last;
    }
}

template <typename Agree>
PairCensus brute(const Corpus &c, Agree agree)
{
    // Equal multisets force agreement in the three order-blind systems, so
    // the multiset compare runs only for pairs that agree there.
    constexpr unsigned order_blind = 1u << kHechrachi | 1u << kKatan | 1u << kSiduri;
    PairCensus r;
    std::span<const uint64_t> keys = c.keys();
    r.pairs = choose2(keys.size());
    for (size_t i = 0; i < keys.size(); i++) {
        const uint64_t ki = keys[i];
        const Multiset mi = c.multiset(static_cast<uint32_t>(i));
        for (size_t j = i + 1; j < keys.size(); j++) {
            unsigned m = agree(ki, keys[j]);
            r.exact[m]++;
            if ((m & order_blind) == order_blind && mi == c.multiset(static_cast<uint32_t>(j)))
                r.anagram[m]++;
        }
    }
    return r;
}

template <typename Agree>
uint64_t checksum(std::span<const uint64_t> keys, Agree agree)
{
    uint64_t sum = 0;
    for (size_t i = 0; i < keys.size(); i++) {
        const uint64_t ki = keys[i];
        for (size_t j = i + 1; j < keys.size(); j++)
            sum += agree(ki, keys[j]);
    }
    return sum;
}

}  // namespace

uint64_t PairCensus::at_least(unsigned mask) const
{
    uint64_t n = 0;
    for (unsigned s = 0; s < kMasks; s++)
        if ((s & mask) == mask)
            n += exact[s];
    return n;
}

uint64_t PairCensus::anagrams_at_least(unsigned mask) const
{
    uint64_t n = 0;
    for (unsigned s = 0; s < kMasks; s++)
        if ((s & mask) == mask)
            n += anagram[s];
    return n;
}

PairCensus census_by_grouping(const Corpus &c)
{
    PairCensus r;
    r.pairs = choose2(c.types());
    std::array<uint64_t, kMasks> at_least{};
    at_least[0] = r.pairs;
    std::vector<uint64_t> projected(c.types());
    for (unsigned subset = 1; subset < kMasks; subset++) {
        const uint64_t bits = lane_mask(subset);
        for (uint32_t t = 0; t < c.types(); t++)
            projected[t] = c.key(t) & bits;
        std::sort(projected.begin(), projected.end());
        for (size_t i = 0; i < projected.size();) {
            size_t j = i + 1;
            while (j < projected.size() && projected[j] == projected[i])
                j++;
            at_least[subset] += choose2(j - i);
            i = j;
        }
    }
    // exact[M] = sum over S containing M of (-1)^|S \ M| at_least[S].
    for (unsigned mask = 0; mask < kMasks; mask++) {
        int64_t n = 0;
        for (unsigned subset = 0; subset < kMasks; subset++)
            if ((subset & mask) == mask) {
                auto term = static_cast<int64_t>(at_least[subset]);
                n += std::popcount(subset ^ mask) & 1 ? -term : term;
            }
        r.exact[mask] = static_cast<uint64_t>(n);
    }
    count_anagrams(c, r);
    return r;
}

PairCensus census_brute(const Corpus &c, bool swar)
{
    return swar ? brute(c, [](uint64_t a, uint64_t b) { return agree_swar(a, b); })
                : brute(c, [](uint64_t a, uint64_t b) { return agree_plain(a, b); });
}

uint64_t agreement_checksum(std::span<const uint64_t> keys, bool swar)
{
    return swar ? checksum(keys, [](uint64_t a, uint64_t b) { return agree_swar(a, b); })
                : checksum(keys, [](uint64_t a, uint64_t b) { return agree_plain(a, b); });
}

ValueGraph::ValueGraph(const Corpus &c) : corpus_(&c)
{
    for (unsigned s = 0; s < kSystems; s++) {
        Csr &g = csr_[s];
        unsigned max_value = 0;
        for (uint32_t t = 0; t < c.types(); t++)
            max_value = std::max(max_value, lane(c.key(t), static_cast<System>(s)));
        // Counting sort by value: count, prefix-sum, place.
        g.start.assign(max_value + 2u, 0);
        for (uint32_t t = 0; t < c.types(); t++)
            g.start[lane(c.key(t), static_cast<System>(s)) + 1u]++;
        for (unsigned v = 0; v <= max_value; v++) {
            uint32_t n = g.start[v + 1u];
            g.edges += choose2(n);
            g.values += n != 0;
            g.largest = std::max(g.largest, n);
            g.start[v + 1u] += g.start[v];
        }
        g.member.resize(c.types());
        std::vector<uint32_t> next(g.start.begin(), g.start.end() - 1);
        for (uint32_t t = 0; t < c.types(); t++)
            g.member[next[lane(c.key(t), static_cast<System>(s))]++] = t;
    }
}

std::span<const uint32_t> ValueGraph::bucket(System s, unsigned value) const
{
    const Csr &g = csr_[s];
    if (value + 1u >= g.start.size())
        return {};
    return {g.member.data() + g.start[value], g.start[value + 1u] - g.start[value]};
}

uint32_t ValueGraph::degree(uint32_t t, System s) const
{
    return static_cast<uint32_t>(bucket(s, lane(corpus_->key(t), s)).size()) - 1u;
}

KeyIndex hash_equalities(const Corpus &c, unsigned mask, unsigned require_corpora)
{
    const uint64_t bits = lane_mask(mask);
    KeyIndex index;
    for (uint32_t t = 0; t < c.types(); t++)
        if ((c.in_corpora(t) & require_corpora) == require_corpora)
            index[c.key(t) & bits].push_back(t);
    return index;
}

std::vector<Match> matches(const Corpus &c, unsigned mask, unsigned require_corpora)
{
    std::vector<Match> out;
    for (const auto &[key, members] : hash_equalities(c, mask, require_corpora))
        for (size_t i = 0; i < members.size(); i++)
            for (size_t j = i + 1; j < members.size(); j++)
                if (c.multiset(members[i]) != c.multiset(members[j]))
                    out.push_back({members[i], members[j]});  // ids ascend inside a bucket
    std::sort(out.begin(), out.end(), [](const Match &x, const Match &y) { return x.a != y.a ? x.a < y.a : x.b < y.b; });
    return out;
}

}  // namespace gem::kab
