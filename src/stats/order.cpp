// order.cpp - PREREG-005: the four-system agreement of the lexicon under
// the traditional letter order and under random letter orders. Every
// definition is fixed by prereg/PREREG-005.md.
//
// A letter order assigns the 22 base letters to the ordinals 1..22; the four
// values of a letter follow from its ordinal. Per order, every word type gets
// one packed key (include/gem/kab/tables.hpp: four 16-bit lanes), and
//   T    = pairs with equal keys   - pairs with equal keys and multisets
//   T_H  = pairs with equal lane H - pairs with equal multisets
// (equal multisets force equal hechrachi, katan and siduri under any order).
#include "gem/kab/tables.hpp"
#include "gem/stats.hpp"
#include "rng.hpp"

extern "C" {
#include "gem/hebrew.h"
}

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace gem {

namespace {

using kab::kCodes;
using kab::Multiset;

constexpr unsigned kBase = kab::kBaseLetters;
constexpr unsigned kOrderAnalysis = 6;  // replicate_seed analysis number (PREREG-005)

using Order = std::array<uint8_t, kBase>;         // alphabet position -> ordinal - 1
using PackedTable = std::array<uint64_t, kCodes>;  // letter code -> packed four values

constexpr uint64_t choose2(uint64_t n)
{
    return n * (n - (n != 0)) / 2;
}

// The packed values of the 27 letter codes under an order.
PackedTable table_for(const Order &order)
{
    auto hechrachi = [](unsigned o) { return o <= 9u ? o : o <= 18u ? 10u * (o - 9u) : 100u * (o - 18u); };
    // The base letters that have final forms, sorted by their ordinal under
    // this order: their finals take 500, 600, 700, 800, 900 in that order.
    std::array<std::pair<unsigned, unsigned>, 5> finals{};  // (ordinal, base letter)
    unsigned n_finals = 0;
    for (uint8_t code = 0; code < kCodes; code++)
        if (gem_letter_is_final(code)) {
            unsigned base = gem_letter_base(code);
            finals[n_finals++] = {order[base] + 1u, base};
        }
    std::sort(finals.begin(), finals.end());

    PackedTable table{};
    for (uint8_t code = 0; code < kCodes; code++) {
        unsigned base = gem_letter_base(code);
        unsigned o = order[base] + 1u;
        unsigned h = hechrachi(o);
        unsigned k = h >= 100u ? h / 100u : h >= 10u ? h / 10u : h;
        unsigned g = h;
        if (gem_letter_is_final(code))
            for (unsigned rank = 0; rank < 5; rank++)
                if (finals[rank].second == base)
                    g = 500u + 100u * rank;
        table[code] = static_cast<uint64_t>(h) | static_cast<uint64_t>(g) << kab::kLaneBits |
                      static_cast<uint64_t>(k) << (2 * kab::kLaneBits) | static_cast<uint64_t>(o) << (3 * kab::kLaneBits);
    }
    return table;
}

struct TypeKey {
    uint64_t key;
    Multiset letters;
    bool operator<(const TypeKey &o) const
    {
        return std::tie(key, letters.lo, letters.hi) < std::tie(o.key, o.letters.lo, o.letters.hi);
    }
};

struct Counts {
    uint64_t t = 0, t_h = 0;
};

class Census {
public:
    explicit Census(const Lexicon &lex) : lex_(lex), multiset_(lex.size()), scratch_(lex.size())
    {
        std::vector<Multiset> sorted(lex.size());
        for (uint32_t t = 0; t < lex.size(); t++) {
            if (lex.letters(t).size() > kab::kMaxWordLetters)
                throw std::runtime_error("a word type is longer than the packed key holds");
            for (uint8_t code : lex.letters(t))
                multiset_[t] += kab::kMultiset[code];
            sorted[t] = multiset_[t];
        }
        std::sort(sorted.begin(), sorted.end(),
                  [](const Multiset &a, const Multiset &b) { return std::tie(a.lo, a.hi) < std::tie(b.lo, b.hi); });
        for (size_t i = 0; i < sorted.size();) {
            size_t j = i + 1;
            while (j < sorted.size() && sorted[j] == sorted[i])
                j++;
            anagram_pairs_ += choose2(j - i);
            i = j;
        }
    }

    Counts count(const Order &order)
    {
        const PackedTable table = table_for(order);
        std::array<uint32_t, 1u << kab::kLaneBits> by_h{};
        for (uint32_t t = 0; t < lex_.size(); t++) {
            uint64_t key = 0;
            for (uint8_t code : lex_.letters(t))
                key += table[code];
            scratch_[t] = {key, multiset_[t]};
            by_h[kab::lane(key, kab::kHechrachi)]++;
        }
        std::sort(scratch_.begin(), scratch_.end());
        uint64_t same_key = 0, same_key_and_letters = 0;
        for (size_t i = 0; i < scratch_.size();) {
            size_t j = i + 1;
            while (j < scratch_.size() && scratch_[j].key == scratch_[i].key)
                j++;
            same_key += choose2(j - i);
            for (size_t a = i; a < j;) {
                size_t b = a + 1;
                while (b < j && scratch_[b].letters == scratch_[a].letters)
                    b++;
                same_key_and_letters += choose2(b - a);
                a = b;
            }
            i = j;
        }
        uint64_t same_h = 0;
        for (uint32_t n : by_h)
            same_h += choose2(n);
        return {same_key - same_key_and_letters, same_h - anagram_pairs_};
    }

private:
    const Lexicon &lex_;
    std::vector<Multiset> multiset_;
    std::vector<TypeKey> scratch_;
    uint64_t anagram_pairs_ = 0;
};

OrderStat summarize(const char *name, double observed, const std::vector<double> &null)
{
    OrderStat s;
    s.name = name;
    s.observed = observed;
    double n = static_cast<double>(null.size()), sum = 0, sq = 0;
    unsigned at_or_above = 0, at_or_below = 0;
    s.min = s.max = null.front();
    for (double x : null) {
        sum += x;
        s.min = std::min(s.min, x);
        s.max = std::max(s.max, x);
        at_or_above += x >= observed;
        at_or_below += x <= observed;
    }
    s.mean = sum / n;
    for (double x : null)
        sq += (x - s.mean) * (x - s.mean);
    s.sd = std::sqrt(sq / (n - 1.0));
    s.p_hi = (1.0 + at_or_above) / (n + 1.0);
    s.p_lo = (1.0 + at_or_below) / (n + 1.0);
    s.p = std::min(1.0, 2.0 * std::min(s.p_hi, s.p_lo));
    return s;
}

}  // namespace

OrderResult run_order(const gem_corpus &c, unsigned orders)
{
    auto t0 = std::chrono::steady_clock::now();
    Lexicon lex(c, 0, c.n_books - 1, Rule::Loose);
    Census census(lex);

    Order traditional{};
    for (uint8_t i = 0; i < kBase; i++)
        traditional[i] = i;
    if (table_for(traditional) != kab::kPacked)
        throw std::runtime_error("the traditional order does not reproduce the hardcoded tables");
    const Counts observed = census.count(traditional);

    std::vector<double> t(orders), t_h(orders), share(orders);
    for (unsigned r = 0; r < orders; r++) {
        Rng rng(replicate_seed(kSeed, kOrderAnalysis, r));
        Order p = traditional;
        for (unsigned i = kBase - 1; i >= 1; i--)
            std::swap(p[i], p[rng.below(i + 1u)]);
        const Counts n = census.count(p);
        t[r] = static_cast<double>(n.t);
        t_h[r] = static_cast<double>(n.t_h);
        share[r] = t[r] / t_h[r];
    }

    OrderResult out;
    out.types = lex.size();
    out.orders = orders;
    out.stats[0] = summarize("T", static_cast<double>(observed.t), t);
    out.stats[1] = summarize("T_H", static_cast<double>(observed.t_h), t_h);
    out.stats[2] = summarize("C", static_cast<double>(observed.t) / static_cast<double>(observed.t_h), share);
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return out;
}

}  // namespace gem
