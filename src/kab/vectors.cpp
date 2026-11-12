// vectors.cpp - stage K, offline numeric vectors and cosine similarity.
// Layout and file format: vectors.hpp.
#include "gem/kab/vectors.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <immintrin.h>
#include <stdexcept>

namespace gem::kab {

namespace {

// Mean table value of the 22 base letters, per system.
constexpr std::array<float, kSystems> make_letter_equivalent()
{
    std::array<unsigned, kSystems> sum{};
    for (const LetterRow &r : kLetters)
        if (r.value[kGadol] == r.value[kHechrachi])  // not a final form
            for (unsigned s = 0; s < kSystems; s++)
                sum[s] += r.value[s];
    std::array<float, kSystems> mean{};
    for (unsigned s = 0; s < kSystems; s++)
        mean[s] = static_cast<float>(sum[s]) / static_cast<float>(kBaseLetters);
    return mean;
}

constexpr std::array<float, kSystems> kLetterEquivalent = make_letter_equivalent();

constexpr uint64_t kFnvOffset = 0xCBF29CE484222325ull, kFnvPrime = 0x100000001B3ull;

uint64_t fnv1a(const void *data, size_t n, uint64_t h = kFnvOffset)
{
    const auto *p = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < n; i++)
        h = (h ^ p[i]) * kFnvPrime;
    return h;
}

struct Header {
    char magic[8];
    uint32_t dim, count;
    uint64_t payload_fnv, lexicon_fnv;
    uint8_t reserved[32];
};
static_assert(sizeof(Header) == 64);

constexpr char kMagic[8] = {'G', 'E', 'M', 'K', 'V', 'E', 'C', '1'};

}  // namespace

void unit_vector(const Multiset &letters, uint64_t key, float *out)
{
    double norm2 = 0;
    for (unsigned b = 0; b < kBaseLetters; b++) {
        out[b] = static_cast<float>(letters.count(b));
        norm2 += static_cast<double>(out[b]) * out[b];
    }
    for (unsigned s = 0; s < kSystems; s++) {
        out[kValueDim + s] = static_cast<float>(lane(key, static_cast<System>(s))) / kLetterEquivalent[s];
        norm2 += static_cast<double>(out[kValueDim + s]) * out[kValueDim + s];
    }
    for (unsigned d = kValueDim + kSystems; d < kDim; d++)
        out[d] = 0.0f;
    float scale = norm2 > 0 ? static_cast<float>(1.0 / std::sqrt(norm2)) : 0.0f;
    for (unsigned d = 0; d < kValueDim + kSystems; d++)
        out[d] *= scale;
}

uint64_t lexicon_hash(const Corpus &c)
{
    uint64_t h = kFnvOffset;
    for (uint32_t t = 0; t < c.types(); t++) {
        uint64_t record[3] = {c.key(t), c.multiset(t).lo, c.multiset(t).hi};
        h = fnv1a(record, sizeof record, h);
    }
    return h;
}

void Vectors::allocate(uint32_t count)
{
    count_ = count;
    size_t bytes = std::max<size_t>(this->bytes(), 64);
    rows_.reset(static_cast<float *>(std::aligned_alloc(64, bytes)));
    if (!rows_)
        throw std::bad_alloc();
}

Vectors::Vectors(const Corpus &c)
{
    allocate(c.types());
    lexicon_ = lexicon_hash(c);
    for (uint32_t t = 0; t < count_; t++)
        unit_vector(c.multiset(t), c.key(t), rows_.get() + static_cast<size_t>(t) * kDim);
}

uint64_t Vectors::payload_hash() const
{
    return fnv1a(rows_.get(), bytes());
}

void Vectors::write(const std::string &path) const
{
    Header h{};
    std::memcpy(h.magic, kMagic, sizeof kMagic);
    h.dim = kDim;
    h.count = count_;
    h.payload_fnv = payload_hash();
    h.lexicon_fnv = lexicon_;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(&h), sizeof h);
    out.write(reinterpret_cast<const char *>(rows_.get()), static_cast<std::streamsize>(bytes()));
    out.close();
    if (!out)
        throw std::runtime_error("cannot write " + path);
}

Vectors::Vectors(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    Header h{};
    in.read(reinterpret_cast<char *>(&h), sizeof h);
    if (!in || std::memcmp(h.magic, kMagic, sizeof kMagic) != 0 || h.dim != kDim)
        throw std::runtime_error(path + ": not a GEMKVEC1 file of dimension " + std::to_string(kDim));
    allocate(h.count);
    lexicon_ = h.lexicon_fnv;
    in.read(reinterpret_cast<char *>(rows_.get()), static_cast<std::streamsize>(bytes()));
    if (!in || in.peek() != std::ifstream::traits_type::eof())
        throw std::runtime_error(path + ": payload size differs from the header's count");
    if (payload_hash() != h.payload_fnv)
        throw std::runtime_error(path + ": payload hash differs from the header");
}

float dot_plain(const float *a, const float *b)
{
    float s = 0.0f;
    for (unsigned d = 0; d < kDim; d++)
        s += a[d] * b[d];
    return s;
}

void scan_plain(const float *rows, uint32_t n, const float *q, float *out)
{
    for (uint32_t i = 0; i < n; i++)
        out[i] = dot_plain(rows + static_cast<size_t>(i) * kDim, q);
}

// The query sits in four registers for the whole scan; each row is four
// aligned loads, two multiply-add chains, one horizontal sum.
[[gnu::target("avx2,fma")]] void scan_avx2(const float *rows, uint32_t n, const float *q, float *out)
{
    const __m256 q0 = _mm256_load_ps(q), q1 = _mm256_load_ps(q + 8);
    const __m256 q2 = _mm256_load_ps(q + 16), q3 = _mm256_load_ps(q + 24);
    for (uint32_t i = 0; i < n; i++, rows += kDim) {
        __m256 a = _mm256_fmadd_ps(_mm256_load_ps(rows + 8), q1, _mm256_mul_ps(_mm256_load_ps(rows), q0));
        __m256 b = _mm256_fmadd_ps(_mm256_load_ps(rows + 24), q3, _mm256_mul_ps(_mm256_load_ps(rows + 16), q2));
        __m256 s = _mm256_add_ps(a, b);
        __m128 h = _mm_add_ps(_mm256_castps256_ps128(s), _mm256_extractf128_ps(s, 1));
        h = _mm_add_ps(h, _mm_movehl_ps(h, h));
        h = _mm_add_ss(h, _mm_shuffle_ps(h, h, 1));
        out[i] = _mm_cvtss_f32(h);
    }
}

bool cpu_has_avx2_fma()
{
    return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
}

std::vector<Neighbor> nearest(const Vectors &v, const float *q, unsigned k)
{
    std::vector<float> cosine(v.count());
    (cpu_has_avx2_fma() ? scan_avx2 : scan_plain)(v.data(), v.count(), q, cosine.data());
    std::vector<Neighbor> all(v.count());
    for (uint32_t t = 0; t < v.count(); t++)
        all[t] = {cosine[t], t};
    size_t keep = std::min<size_t>(k, all.size());
    std::partial_sort(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(keep), all.end(),
                      [](const Neighbor &a, const Neighbor &b) {
                          return a.cosine != b.cosine ? a.cosine > b.cosine : a.type < b.type;
                      });
    all.resize(keep);
    return all;
}

}  // namespace gem::kab
