// tables.hpp - stage K, the four letter-value systems (pure C++).
//
// One hardcoded const row per Hebrew letter code point U+05D0..U+05EA (22
// letters + 5 final forms), keyed by wchar_t. Definitions: Wikipedia
// "Gematria" (fetched 2026-10-30), the source src/core/methods.c also uses;
// tests/kab_test.cpp runs that C core as an independent oracle.
//   hechrachi  standard value 1-9, 10-90, 100-400; a final = its base letter
//   gadol      finals 500, 600, 700, 800, 900
//   katan      standard value with its zeros dropped (1-9)
//   siduri     ordinal 1-22; a final = its base letter
//
// Derived at compile time from the rows:
//   kPacked    the four values of a letter in four 16-bit lanes of one
//              uint64_t. Adding the constants of a word's letters sums all
//              four systems at once, and the sum is a collision-free key:
//              two words agree in system s iff lane s of their keys agrees.
//   kMultiset  the letter's increment in a 22 x 5-bit count vector (finals
//              folded): equal multisets = anagrams.
// The static_asserts at the end regenerate every row from the definitions,
// so a mistyped value does not compile.
#pragma once

#include <array>
#include <cstdint>

namespace gem::kab {

enum System : unsigned { kHechrachi, kGadol, kKatan, kSiduri, kSystems };

inline constexpr std::array<const char *, kSystems> kSystemName{"hechrachi", "gadol", "katan", "siduri"};

inline constexpr wchar_t kFirstLetter = L'א';
inline constexpr unsigned kCodes = 27;        // code points U+05D0..U+05EA
inline constexpr unsigned kBaseLetters = 22;  // alef..tav, finals folded
inline constexpr unsigned kLaneBits = 16;     // bits per system in a packed key
inline constexpr unsigned kCountBits = 5;     // bits per letter in a Multiset
inline constexpr unsigned kHalfLetters = 11;  // letters per Multiset half

// Longest word the packed forms hold without a lane or a count overflowing.
inline constexpr unsigned kMaxWordLetters = 31;

struct LetterRow {
    wchar_t key;
    std::array<uint16_t, kSystems> value;  // indexed by System
};

inline constexpr std::array<LetterRow, kCodes> kLetters{{
    //  key          hechrachi gadol katan siduri
    {L'א', {1, 1, 1, 1}},        // alef
    {L'ב', {2, 2, 2, 2}},        // bet
    {L'ג', {3, 3, 3, 3}},        // gimel
    {L'ד', {4, 4, 4, 4}},        // dalet
    {L'ה', {5, 5, 5, 5}},        // he
    {L'ו', {6, 6, 6, 6}},        // vav
    {L'ז', {7, 7, 7, 7}},        // zayin
    {L'ח', {8, 8, 8, 8}},        // het
    {L'ט', {9, 9, 9, 9}},        // tet
    {L'י', {10, 10, 1, 10}},     // yod
    {L'ך', {20, 500, 2, 11}},    // final kaf
    {L'כ', {20, 20, 2, 11}},     // kaf
    {L'ל', {30, 30, 3, 12}},     // lamed
    {L'ם', {40, 600, 4, 13}},    // final mem
    {L'מ', {40, 40, 4, 13}},     // mem
    {L'ן', {50, 700, 5, 14}},    // final nun
    {L'נ', {50, 50, 5, 14}},     // nun
    {L'ס', {60, 60, 6, 15}},     // samekh
    {L'ע', {70, 70, 7, 16}},     // ayin
    {L'ף', {80, 800, 8, 17}},    // final pe
    {L'פ', {80, 80, 8, 17}},     // pe
    {L'ץ', {90, 900, 9, 18}},    // final tsadi
    {L'צ', {90, 90, 9, 18}},     // tsadi
    {L'ק', {100, 100, 1, 19}},   // qof
    {L'ר', {200, 200, 2, 20}},   // resh
    {L'ש', {300, 300, 3, 21}},   // shin
    {L'ת', {400, 400, 4, 22}},   // tav
}};

// Letter code 0..26 of a code point; any value >= kCodes means "not a
// letter". One subtraction and one unsigned compare classify and index.
constexpr unsigned letter_code(wchar_t wc)
{
    return static_cast<unsigned>(wc) - static_cast<unsigned>(kFirstLetter);
}

// Value of one letter in one system; 0 for anything that is not a letter.
constexpr unsigned letter_value(System s, wchar_t wc)
{
    unsigned c = letter_code(wc);
    return c < kCodes ? kLetters[c].value[s] : 0u;
}

// Value of system s inside a packed key.
constexpr unsigned lane(uint64_t key, System s)
{
    return static_cast<unsigned>(key >> (kLaneBits * s) & 0xFFFFu);
}

// The independent coordinates behind the four lanes of a key (the identities
// are proved per letter by detail::systems_are_dependent below):
//   decade_sum    = (siduri - katan) / 9: the word's letters counted 0 for a
//                   units letter, 1 for a tens letter, 2 for a hundreds letter
//   final_surplus = gadol - hechrachi: what the final forms add
// Two words agree in all four systems iff they agree in hechrachi, katan,
// decade_sum and final_surplus.
constexpr unsigned decade_sum(uint64_t key)
{
    return (lane(key, kSiduri) - lane(key, kKatan)) / 9u;
}

constexpr unsigned final_surplus(uint64_t key)
{
    return lane(key, kGadol) - lane(key, kHechrachi);
}

// Letter counts of a word, finals folded: letters 0..10 in lo, 11..21 in hi.
struct Multiset {
    uint64_t lo = 0, hi = 0;
    constexpr bool operator==(const Multiset &) const = default;
    constexpr Multiset &operator+=(const Multiset &o)
    {
        lo += o.lo;
        hi += o.hi;
        return *this;
    }
    // Occurrences of base letter b (0 = alef .. 21 = tav).
    constexpr unsigned count(unsigned b) const
    {
        uint64_t half = b < kHalfLetters ? lo : hi;
        return static_cast<unsigned>(half >> (kCountBits * (b % kHalfLetters)) & ((1u << kCountBits) - 1u));
    }
};

namespace detail {

constexpr std::array<uint64_t, kCodes> make_packed()
{
    std::array<uint64_t, kCodes> p{};
    for (unsigned c = 0; c < kCodes; c++)
        for (unsigned s = 0; s < kSystems; s++)
            p[c] |= static_cast<uint64_t>(kLetters[c].value[s]) << (kLaneBits * s);
    return p;
}

constexpr std::array<Multiset, kCodes> make_multiset()
{
    std::array<Multiset, kCodes> m{};
    for (unsigned c = 0; c < kCodes; c++) {
        unsigned b = kLetters[c].value[kSiduri] - 1u;
        uint64_t one = uint64_t{1} << (kCountBits * (b % kHalfLetters));
        (b < kHalfLetters ? m[c].lo : m[c].hi) = one;
    }
    return m;
}

}  // namespace detail

inline constexpr std::array<uint64_t, kCodes> kPacked = detail::make_packed();
inline constexpr std::array<Multiset, kCodes> kMultiset = detail::make_multiset();

// Systems in which two packed keys agree, as a 4-bit mask (bit s = System
// s): the plain form, four lane compares.
constexpr unsigned agree_plain(uint64_t a, uint64_t b)
{
    unsigned m = 0;
    for (unsigned s = 0; s < kSystems; s++)
        m |= static_cast<unsigned>(lane(a, static_cast<System>(s)) == lane(b, static_cast<System>(s))) << s;
    return m;
}

// The same mask without a compare or a branch. In x = a ^ b a lane is zero
// iff the keys agree there; (x & low15) + low15 carries into the lane's top
// bit iff any low bit is set, | x adds the top bit itself, so the top bit
// of a lane ends up set iff the lane is non-zero. The inverted top bits sit
// at 15, 31, 47, 63; shifted to 0, 16, 32, 48 one multiply drops them into
// bits 48..51 (partial products land on distinct bits: no carries).
constexpr unsigned agree_swar(uint64_t a, uint64_t b)
{
    constexpr uint64_t low15 = 0x7FFF7FFF7FFF7FFFull;
    constexpr uint64_t gather = (uint64_t{1} << 48) | (uint64_t{1} << 33) | (uint64_t{1} << 18) | (uint64_t{1} << 3);
    uint64_t x = a ^ b;
    uint64_t nonzero = ((x & low15) + low15) | x;
    uint64_t equal = (~nonzero & ~low15) >> 15;
    return static_cast<unsigned>((equal * gather) >> 48) & 0xFu;
}

namespace detail {

// Regenerates every row from the definitions in the header comment.
constexpr bool rows_match_definitions()
{
    unsigned ordinal = 0, finals = 0;
    for (unsigned c = 0; c < kCodes; c++) {
        const LetterRow &r = kLetters[c];
        // The five finals sit directly before their base letter in Unicode.
        bool is_final = c == 10 || c == 13 || c == 15 || c == 19 || c == 21;
        unsigned o = ordinal + 1u;
        if (!is_final)
            ordinal++;
        unsigned h = o <= 9u ? o : o <= 18u ? (o - 9u) * 10u : (o - 18u) * 100u;
        unsigned g = is_final ? 500u + 100u * finals++ : h;
        unsigned k = h >= 100u ? h / 100u : h >= 10u ? h / 10u : h;
        if (r.key != static_cast<wchar_t>(kFirstLetter + c) || r.value[kHechrachi] != h || r.value[kGadol] != g ||
            r.value[kKatan] != k || r.value[kSiduri] != o)
            return false;
    }
    return ordinal == kBaseLetters && finals == 5;
}

// Structure of the four systems, proved on the rows: with d = 0, 1, 2 for a
// units, tens or hundreds letter,
//   hechrachi = katan x 10^d      (so hechrachi = katan mod 9)
//   siduri    = katan + 9 x d
// Summed over a word: siduri = katan + 9 x (sum of d). Siduri therefore adds
// one number (the sum of d) to katan, not an independent value, and katan is
// tied to hechrachi modulo 9. Gadol is hechrachi except on the five finals.
constexpr bool systems_are_dependent()
{
    for (const LetterRow &r : kLetters) {
        unsigned h = r.value[kHechrachi], k = r.value[kKatan];
        unsigned d = h >= 100u ? 2u : h >= 10u ? 1u : 0u;
        unsigned pow10 = d == 2u ? 100u : d == 1u ? 10u : 1u;
        if (h != k * pow10 || h % 9u != k % 9u || r.value[kSiduri] != k + 9u * d)
            return false;
    }
    return true;
}

constexpr unsigned column_sum(System s)
{
    unsigned sum = 0;
    for (const LetterRow &r : kLetters)
        sum += r.value[s];
    return sum;
}

// agree_swar against agree_plain over every pair of single-letter keys and
// a set of keys with one lane forced equal.
constexpr bool swar_matches_plain()
{
    for (uint64_t a : kPacked)
        for (uint64_t b : kPacked) {
            if (agree_swar(a, b) != agree_plain(a, b))
                return false;
            for (unsigned s = 0; s < kSystems; s++) {
                uint64_t mask = uint64_t{0xFFFF} << (kLaneBits * s);
                uint64_t forced = (b & ~mask) | (a & mask);
                if (agree_swar(a, forced) != agree_plain(a, forced) || !(agree_swar(a, forced) >> s & 1u))
                    return false;
            }
        }
    return agree_swar(0, ~uint64_t{0}) == 0 && agree_swar(0x8000800080008000ull, 0) == 0 &&
           agree_swar(0xFFFF0000FFFF0000ull, 0xFFFF0001FFFF0001ull) == 0xAu;
}

}  // namespace detail

static_assert(sizeof(wchar_t) >= 2, "wchar_t must hold U+05EA");
static_assert(detail::rows_match_definitions(), "a letter row disagrees with its definition");
static_assert(detail::systems_are_dependent(), "hechrachi = katan x 10^d and siduri = katan + 9d must hold per letter");
static_assert(detail::column_sum(kHechrachi) == 1495 + 280, "hechrachi: 22 letters 1495, five finals 280");
static_assert(detail::column_sum(kGadol) == 1495 + 3500, "gadol: finals 500..900 sum to 3500");
static_assert(detail::column_sum(kKatan) == 100 + 28, "katan: 22 letters 100, five finals 28");
static_assert(detail::column_sum(kSiduri) == 253 + 73, "siduri: 1..22 = 253, five finals 73");
static_assert(kMaxWordLetters * 900u <= 0xFFFFu, "a gadol lane must hold the longest word");
static_assert(kMaxWordLetters < (1u << kCountBits), "a letter count must hold the longest word");
static_assert(kBaseLetters == 2 * kHalfLetters && kHalfLetters * kCountBits <= 64, "Multiset layout");
static_assert(detail::swar_matches_plain(), "agree_swar disagrees with agree_plain");

}  // namespace gem::kab
