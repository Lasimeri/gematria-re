// stats.hpp - statistics over a GEMC corpus (layers L1 and L5 of the
// framework): word rules, the type lexicon with per-method value indexes,
// match shares (the claim auditor's statistic) and the PREREG-001 analyses
// R1 and R2. Definitions are fixed by prereg/PREREG-001.md.
#pragma once

extern "C" {
#include "gem/corpus.h"
#include "gem/methods.h"
}

#include "gem/kab/tables.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace gem {

// One-line check of the corpus and every preregistration against their
// frozen sha256; *ok is true only if all match.
std::string frozen_status(const std::string &corpus_path, bool *ok);

constexpr unsigned kMethods = GEM_M_COUNT;
constexpr uint32_t kAllMethods = (1u << kMethods) - 1u;
constexpr uint64_t kSeed = 20261030u;

using Values = std::array<uint64_t, kMethods>;

Values all_values(std::span<const uint8_t> codes);

// RAII owner of a corpus read from a GEMC file; throws on failure.
class Corpus {
public:
    explicit Corpus(const std::string &path);
    ~Corpus();
    Corpus(const Corpus &) = delete;
    Corpus &operator=(const Corpus &) = delete;
    const gem_corpus &raw() const { return c_; }
    // "Gen 1:1" style reference of word w.
    std::string ref(uint32_t w) const;

private:
    gem_corpus c_{};
};

enum class Rule { Loose, Strict };

// Strict-rule shape of a word (PREREG-001, R2): letters in prefix parts,
// letters in trailing suffix parts, whether the head's part of speech is
// N, V or A, and whether the head is a proper noun (Np).
struct WordShape {
    uint32_t prefix_letters, suffix_letters;
    bool content, proper;
};
WordShape word_shape(const gem_corpus &c, uint32_t w);

// Letters a rule takes from word w: [first, first + len), or nothing if the
// rule excludes the word.
struct WordSpan {
    uint32_t first, len;
};
std::optional<WordSpan> rule_span(const gem_corpus &c, uint32_t w, Rule r);

// Distinct word types of books [book_lo, book_hi] under a rule.
class Lexicon {
public:
    Lexicon(const gem_corpus &c, uint32_t book_lo, uint32_t book_hi, Rule rule);
    uint32_t size() const { return static_cast<uint32_t>(off_.size()); }
    std::span<const uint8_t> letters(uint32_t t) const { return {pool_.data() + off_[t], len_[t]}; }
    uint32_t tokens(uint32_t t) const { return tokens_[t]; }
    uint64_t total_tokens() const { return total_tokens_; }
    uint32_t first_word(uint32_t t) const { return first_word_[t]; }
    const Values &values(uint32_t t) const { return values_[t]; }
    const std::vector<uint32_t> *bucket(unsigned m, uint64_t v) const;
    int64_t find(std::span<const uint8_t> codes) const;

private:
    std::vector<uint8_t> pool_;
    std::vector<uint32_t> off_;
    std::vector<uint8_t> len_;
    std::vector<uint32_t> tokens_, first_word_;
    std::vector<Values> values_;
    uint64_t total_tokens_ = 0;
    std::unordered_map<std::string, uint32_t> by_letters_;
    std::array<std::unordered_map<uint64_t, std::vector<uint32_t>>, kMethods> index_;
};

// Types T != exclude with v_m(T) = v_m(word) + k for some m in mask, k in
// [-kolel, +kolel]. hits (optional) receives the matching type indexes.
struct Share {
    uint32_t types = 0;
    uint64_t tokens = 0;
};
Share match_share(const Lexicon &lex, const Values &word, uint32_t mask, int kolel, int64_t exclude,
                  std::vector<uint32_t> *hits = nullptr);

// Claim auditor (PREREG-001): does B match A under (mask, kolel), and how
// often does a random corpus type do the same.
struct ClaimVerdict {
    bool holds = false;
    uint32_t matching_methods = 0;  // methods in mask under which B matches A
    Share share;                    // over types != A
    double p_types = 0, p_tokens = 0;
    unsigned dof = 0;               // |mask| x |K|
    bool rare = false;              // p_types < alpha
};
constexpr double kAlpha = 0.01;
ClaimVerdict audit_claim(const Lexicon &lex, std::span<const uint8_t> a, std::span<const uint8_t> b,
                         uint32_t mask, int kolel);

// Match share of every type against all other types under (mask, kolel):
// the auditor's p for a claim whose A is that type. The p depends on A
// alone; B only decides whether the claim holds.
std::vector<double> type_shares(const Lexicon &lex, uint32_t mask, int kolel, unsigned threads);

// R1 (dictionary-style collision figures, WLC analogue).
struct R1 {
    uint32_t types = 0;
    double pair_collision = 0;      // hechrachi
    double birthday30 = 0;          // P(30 random types share a hechrachi value)
    double median_share_std = 0;    // hechrachi, K = {0}
    double median_share_all = 0;    // 16 methods, K = {-1,0,+1}
    double seconds = 0;
};
R1 run_r1(const Lexicon &lex, unsigned threads);

// R2 (Pentateuch coverage of values 1..1000 under hechrachi).
struct NullRun {
    unsigned replicates = 0;
    double mean = 0, sd = 0, z = 0;
    unsigned at_or_below = 0;
    double seconds = 0;
};
struct R2 {
    unsigned loose = 0, strict = 0;
    std::vector<unsigned> loose_missing_861, strict_missing_861;
    NullRun m1_strict, m2_strict, m1_loose, m2_loose;
};
// Strict-rule variants. PREREG-001 = {false, false}; PREREG-002 = {false, true}.
struct StrictVariant {
    bool exclude_proper, strip_suffix;
};
constexpr StrictVariant kPrereg001Strict{false, false};
constexpr StrictVariant kPrereg002Strict{false, true};

R2 run_r2(const gem_corpus &c, StrictVariant rule, unsigned m1_reps, unsigned m2_reps, unsigned threads);

// PREREG-003: clustering of equal-valued words. One cell per scope x window
// x K: observed S, exact null mean E, R = S / E; Q1 against M3 replicates;
// Q2 against random letter-value tables.
struct ClusterCell {
    const char *scope = "", *window = "";
    int kolel = 0;
    double s = 0, e = 0, r = 0;
    unsigned reps = 0;
    double rep_mean = 0, rep_sd = 0, q1_z = 0, q1_p = 0;
    unsigned tables = 0;
    double placebo_mean_r = 0, placebo_sd_r = 0, q2_p = 0;
};
std::vector<ClusterCell> run_cluster(const gem_corpus &c, unsigned tables, unsigned reps, unsigned threads);

// PREREG-005: pairs of types (different letter multisets) agreeing in all
// four systems of stage K (T), in hechrachi (T_H), and T / T_H (C), under the
// traditional letter order and under random letter orders.
struct OrderStat {
    const char *name = "";
    double observed = 0, mean = 0, sd = 0, min = 0, max = 0;
    double p_hi = 0, p_lo = 0, p = 0;  // p = min(1, 2 min(p_hi, p_lo))
};
struct OrderResult {
    uint32_t types = 0;
    unsigned orders = 0;
    std::array<OrderStat, 3> stats;  // T, T_H, C
    double seconds = 0;
};
OrderResult run_order(const gem_corpus &c, unsigned orders);

// EXPLORATORY: the four systems of stage K over the auditor's lexicon.
// FourKey: a word's packed key (kab/tables.hpp) and its letter multiset.
// FourCensus: the pairs of types with different letter multisets
// (pairs), and of those the pairs agreeing in at least the systems of each
// 4-bit mask (bit 0 hechrachi, 1 gadol, 2 katan, 3 siduri).
struct FourKey {
    uint64_t key = 0;
    kab::Multiset letters;
};
FourKey four_key(std::span<const uint8_t> codes);  // throws std::length_error above 31 letters
struct FourCensus {
    uint64_t pairs = 0;
    std::array<uint64_t, 1u << kab::kSystems> at_least{};
};
FourCensus four_census(const Lexicon &lex);

// EXPLORATORY (not preregistered): strict coverage of any variant, to locate
// the cause of a mismatch with the source; never a replacement result.
struct VariantCoverage {
    unsigned covered = 0;
    std::vector<unsigned> missing_861;
};
VariantCoverage strict_variant(const gem_corpus &c, StrictVariant v);

}  // namespace gem
