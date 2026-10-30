// rng.hpp - xoshiro256** seeded through splitmix64 (PREREG-001 "RNG").
// Reference: Blackman and Vigna, https://prng.di.unimi.it/xoshiro256starstar.c
// and https://prng.di.unimi.it/splitmix64.c
#pragma once

#include <cstdint>

namespace gem {

// GCC/Clang 128-bit integer; __extension__ marks the non-ISO type for -Wpedantic.
__extension__ typedef unsigned __int128 u128;

class Rng {
public:
    explicit Rng(uint64_t seed)
    {
        for (auto &w : s_) {
            seed += 0x9E3779B97F4A7C15ull;
            uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            w = z ^ (z >> 31);
        }
    }

    uint64_t next()
    {
        const uint64_t result = rotl(s_[1] * 5u, 7) * 9u;
        const uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }

    // Uniform in [0, n) by the multiply-high reduction (bias < n / 2^64).
    uint64_t below(uint64_t n) { return static_cast<uint64_t>((static_cast<u128>(next()) * n) >> 64); }

private:
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t s_[4];
};

// Seed of replicate r of analysis a (PREREG-001).
constexpr uint64_t replicate_seed(uint64_t base, unsigned analysis, unsigned r)
{
    return base + 1000003ull * analysis + r;
}

}  // namespace gem
