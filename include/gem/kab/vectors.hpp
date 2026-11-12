// vectors.hpp - stage K, offline numeric vectors of word types and cosine
// similarity over them.
//
// One vector per word type, kDim = 32 floats (128 bytes, two cache lines,
// 64-byte aligned):
//   [0, 22)   letter counts, finals folded (alef .. tav)
//   [22, 26)  the four values in "letter equivalents": the value divided by
//             the mean table value of the 22 base letters in that system
//             (hechrachi and gadol 1495/22, katan 100/22, siduri 253/22), so
//             the block is on the scale of the count block and depends on no
//             corpus
//   [26, 32)  zero
// Every vector is scaled to unit length when it is built, so the cosine of
// two vectors is their dot product. The layout is a declared choice, not a
// derived one: cosine here ranks candidates for reading, it tests nothing.
//
// File format "GEMKVEC1" (native little-endian): a 64-byte header (magic,
// dim, count, FNV-1a 64 of the payload, FNV-1a 64 of the lexicon the vectors
// were built from, zero padding), then count x dim floats. A reader refuses
// a file whose payload hash differs; lexicon_hash() lets a caller refuse
// vectors built from other corpora.
#pragma once

#include "gem/kab/corpus.hpp"

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace gem::kab {

inline constexpr unsigned kDim = 32;
inline constexpr unsigned kValueDim = kBaseLetters;  // first of the four value dims

// Unit vector of a word given its letter multiset and packed key. out must
// hold kDim floats.
void unit_vector(const Multiset &letters, uint64_t key, float *out);

// FNV-1a 64 over every type's key and multiset, in type order.
uint64_t lexicon_hash(const Corpus &c);

class Vectors {
public:
    explicit Vectors(const Corpus &c);          // build
    explicit Vectors(const std::string &path);  // read; throws std::runtime_error on any mismatch
    void write(const std::string &path) const;

    uint32_t count() const { return count_; }
    const float *row(uint32_t t) const { return rows_.get() + static_cast<size_t>(t) * kDim; }
    const float *data() const { return rows_.get(); }
    size_t bytes() const { return static_cast<size_t>(count_) * kDim * sizeof(float); }
    uint64_t lexicon() const { return lexicon_; }
    uint64_t payload_hash() const;

private:
    struct Free {
        void operator()(float *p) const { std::free(p); }
    };
    void allocate(uint32_t count);

    std::unique_ptr<float[], Free> rows_;
    uint32_t count_ = 0;
    uint64_t lexicon_ = 0;
};

// Dot product of two kDim vectors: the plain loop.
float dot_plain(const float *a, const float *b);

// out[i] = dot(rows + i * kDim, q) for i in [0, n). rows and q must be
// 32-byte aligned for scan_avx2, which needs a CPU with AVX2 and FMA
// (cpu_has_avx2_fma()).
void scan_plain(const float *rows, uint32_t n, const float *q, float *out);
void scan_avx2(const float *rows, uint32_t n, const float *q, float *out);
bool cpu_has_avx2_fma();

struct Neighbor {
    float cosine;
    uint32_t type;
};

// The k types with the largest cosine to q (a 32-byte aligned unit
// vector), best first; ties broken by type index.
std::vector<Neighbor> nearest(const Vectors &v, const float *q, unsigned k);

}  // namespace gem::kab
