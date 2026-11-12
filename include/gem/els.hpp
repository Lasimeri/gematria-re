// els.hpp - equidistant letter sequences (PREREG-004).
//
// Letter streams are base letters 0..21 (finals folded). An ELS of word w
// with skip d >= 2 starts at i with L[i + j*d] = w_j; a backward ELS is a
// forward ELS of the reversed word; |d| = 1 is never counted.
#pragma once

extern "C" {
#include "gem/corpus.h"
}

#include <cstdint>
#include <string>
#include <vector>

namespace gem {

using Word = std::vector<uint8_t>;  // base letters 0..21

// Folded letter stream of corpus letters [lo, hi).
std::vector<uint8_t> letter_stream(const gem_corpus &c, uint32_t lo, uint32_t hi);

// Letter range [lo, hi) of verses book ch1:v1 .. ch2:v2 inclusive; throws if absent.
struct LetterRange {
    uint32_t lo, hi;
};
LetterRange verse_letters(const gem_corpus &c, const std::string &book, unsigned ch1, unsigned v1, unsigned ch2,
                          unsigned v2);

struct ElsHit {
    uint32_t start;  // index in the stream of the word's first letter
    int32_t skip;    // negative = read backward
};

// Reference: every occurrence of w with dmin <= |d| <= dmax, both directions.
std::vector<ElsHit> els_find(const std::vector<uint8_t> &L, const Word &w, unsigned dmin, unsigned dmax);

// Counts over skip bands 2-10, 11-100, 101-1000 (bands beyond dmax stay 0).
struct ElsCount {
    uint64_t total = 0, band[3] = {0, 0, 0};
};

// Kernel P (plain): per target, per direction, per skip, per start.
ElsCount els_count_plain(const std::vector<uint8_t> &L, const std::vector<Word> &targets, unsigned dmin,
                         unsigned dmax);

// Multi-target counters for 5-letter targets: all targets (forward and
// reversed) live in one 2^25-bit presence bitmap keyed by five 5-bit letters.
class Els5 {
public:
    explicit Els5(const std::vector<Word> &targets);
    // Kernel S: five sequential load streams per skip.
    ElsCount count_streams(const std::vector<uint8_t> &L, unsigned dmin, unsigned dmax) const;
    // Kernel R: rolling key along each residue class mod d, one load per step.
    ElsCount count_rolling(const std::vector<uint8_t> &L, unsigned dmin, unsigned dmax) const;
    // Kernel H: kernel R with an 8 KB L1-resident hashed prefilter in front of
    // the 4 MB bitmap (the bitmap is touched only on filter positives).
    ElsCount count_hashed(const std::vector<uint8_t> &L, unsigned dmin, unsigned dmax) const;
    // Kernel A (AVX2): kernel S over 8 consecutive starts per step. Five
    // contiguous 8-byte loads give the 8 keys, one multiply hashes them, one
    // gather reads the 8 prefilter words; only prefilter positives reach the
    // 4 MB bitmap. Needs a CPU with AVX2 (has_avx2()).
    ElsCount count_avx2(const std::vector<uint8_t> &L, unsigned dmin, unsigned dmax) const;
    static bool has_avx2();

    // Multiplicative hash of a 25-bit key to the prefilter's 16 bits.
    static uint32_t hash16(uint32_t key) { return (key * 0x9E3779B1u) >> 16; }

private:
    uint32_t multiplicity(uint32_t key) const;
    std::vector<uint64_t> bits_;
    std::vector<uint64_t> filter_;    // 2^16 bits
    std::vector<uint32_t> filter32_;  // the same bits as 32-bit words, for the gather
    std::vector<std::pair<uint32_t, uint32_t>> mult_;  // sorted (key, multiplicity)
};

}  // namespace gem
