// kab_test.cpp - stage K gate: the pure C++ tables and loader (gem::kab)
// against the C core (libgem), two independent implementations of the same
// cited definitions.
//
// Usage: gem-kab-test WLC.txt POINTED-GEN1.txt
//   WLC.txt           build/gem-export out/wlc.gemc WLC.txt
//   POINTED-GEN1.txt  scripts/pointed-gen1.sh data/morphhb/wlc/Gen.xml
//
// Checks:
//   1. every table row against gem_letter_table (27 code points x 4 systems)
//   2. the loaded corpus against the frozen counts of stages G1-G3
//      (README "Measured"), and every word type's four values and letter
//      multiset against gem_value / gem_letter_base
//   3. agree_swar against agree_plain on 10,000,000 random key pairs
//   4. pointed Genesis 1 (points and accents present) against the same
//      chapter of the letters-only text, verse by verse
extern "C" {
#include "gem/hebrew.h"
#include "gem/methods.h"
}

#include "gem/kab/corpus.hpp"

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <exception>

using namespace gem::kab;

static_assert(GEM_M_HECHRACHI == static_cast<int>(kHechrachi) && GEM_M_GADOL == static_cast<int>(kGadol) &&
              GEM_M_KATAN == static_cast<int>(kKatan) && GEM_M_SIDURI == static_cast<int>(kSiduri));
static_assert(GEM_LETTER_CODES == kCodes && GEM_BASE_LETTERS == kBaseLetters);

namespace {

int pass, fail;

void check(const char *what, uint64_t got, uint64_t want)
{
    bool ok = got == want;
    pass += ok;
    fail += !ok;
    std::printf("  %-4s %-58s %12" PRIu64 "  (want %" PRIu64 ")\n", ok ? "ok" : "FAIL", what, got, want);
}

void tables_against_core()
{
    std::printf("tables against the C core:\n");
    uint64_t bad = 0;
    for (unsigned s = 0; s < kSystems; s++) {
        const uint32_t *core = gem_letter_table(static_cast<gem_method>(s));
        for (unsigned c = 0; c < kCodes; c++) {
            wchar_t wc = static_cast<wchar_t>(kFirstLetter + c);
            bad += core[c] != kLetters[c].value[s] || letter_value(static_cast<System>(s), wc) != core[c] ||
                   lane(kPacked[c], static_cast<System>(s)) != core[c];
        }
    }
    check("rows, letter_value and packed lanes differing (108 cells)", bad, 0);
    check("letter_value of a non-letter (U+05BE maqaf)", letter_value(kHechrachi, L'־'), 0);
}

void corpus_against_core(const Corpus &c)
{
    std::printf("corpus against the frozen counts and the C core:\n");
    const LoadStats &st = c.stats(0);
    check("verses (G1: 23,213)", st.verses, 23213);
    check("words (G1: 305,507)", st.tokens, 305507);
    check("letters (G1: 1,197,042)", st.letters, 1197042);
    check("word types (G3 lexicon: 39,615)", c.types(), 39615);
    check("marks + punctuation + ASCII + foreign + malformed + overlong",
          st.marks + st.punct + st.ascii + st.foreign + st.malformed + st.overlong, 0);

    uint64_t bad_values = 0, bad_multisets = 0, bad_parse = 0;
    for (uint32_t t = 0; t < c.types(); t++) {
        uint8_t codes[kMaxWordLetters + 1];
        unsigned words = 0;
        long n = gem_parse_text(c.text(t).c_str(), codes, sizeof codes, &words);
        if (n != static_cast<long>(c.letters(t)) || words != 1) {
            bad_parse++;
            continue;
        }
        unsigned counts[kBaseLetters] = {};
        for (long i = 0; i < n; i++)
            counts[gem_letter_base(codes[i])]++;
        for (unsigned s = 0; s < kSystems; s++)
            bad_values += gem_value(static_cast<gem_method>(s), codes, static_cast<size_t>(n)) !=
                          static_cast<uint64_t>(c.value(t, static_cast<System>(s)));
        for (unsigned b = 0; b < kBaseLetters; b++)
            bad_multisets += c.multiset(t).count(b) != counts[b];
    }
    check("types the C parser reads differently", bad_parse, 0);
    check("type values differing from gem_value (39,615 x 4)", bad_values, 0);
    check("letter counts differing from gem_letter_base (39,615 x 22)", bad_multisets, 0);

    uint64_t torah = 0, torah_verses = 0;
    for (const Verse &v : c.verses()) {
        const std::string &r = v.ref;
        if (r.starts_with("Gen ") || r.starts_with("Exod ") || r.starts_with("Lev ") || r.starts_with("Num ") ||
            r.starts_with("Deut ")) {
            torah += v.sum[kHechrachi];
            torah_verses++;
        }
    }
    check("Torah verses (G1: 5,853)", torah_verses, 5853);
    check("Torah hechrachi sum over verse sums (G2: 21,010,192)", torah, 21010192);

    ValueIndex index = c.word_index(kHechrachi);
    check("types with hechrachi 70 (G3: yayin + 82 others)", index[70].size(), 83);
    uint64_t pairs = 0;
    for (const auto &[value, members] : index)
        pairs += static_cast<uint64_t>(members.size()) * (members.size() - 1) / 2;
    double all = static_cast<double>(c.types()) * (c.types() - 1) / 2.0;
    check("hechrachi pair collision, 1 in N (R1: 704)", static_cast<uint64_t>(std::lround(all / static_cast<double>(pairs))),
          704);
    std::printf("       equal-valued pairs %" PRIu64 " of %.0f\n", pairs, all);
}

void swar_against_plain()
{
    std::printf("agree_swar against agree_plain:\n");
    uint64_t state = 20261112, bad = 0;
    auto next = [&] {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    };
    const unsigned n = 10'000'000;
    for (unsigned i = 0; i < n; i++) {
        uint64_t a = next(), b = next(), force = next();
        // Copy a random subset of lanes from a, so every mask occurs.
        for (unsigned s = 0; s < kSystems; s++)
            if (force >> s & 1u) {
                uint64_t m = uint64_t{0xFFFF} << (kLaneBits * s);
                b = (b & ~m) | (a & m);
            }
        bad += agree_swar(a, b) != agree_plain(a, b);
    }
    check("mismatches in 10,000,000 random pairs (all 16 masks forced)", bad, 0);
}

void pointed_against_plain(const Corpus &plain, const Corpus &pointed)
{
    std::printf("pointed Genesis 1 against the letters-only text:\n");
    const LoadStats &st = pointed.stats(0);
    check("verses", st.verses, 31);
    check("foreign + malformed + overlong", st.foreign + st.malformed + st.overlong, 0);
    std::printf("       dropped marks %" PRIu64 ", Hebrew punctuation %" PRIu64 ", ASCII %" PRIu64 "\n", st.marks,
                st.punct, st.ascii);
    uint64_t bad = 0;
    for (size_t i = 0; i < pointed.verses().size() && i < plain.verses().size(); i++) {
        const Verse &a = pointed.verses()[i], &b = plain.verses()[i];
        bool same = a.ref == b.ref && a.n_tokens == b.n_tokens && a.sum == b.sum;
        for (uint32_t k = 0; same && k < a.n_tokens; k++)
            same = pointed.text(pointed.token_types()[a.first_token + k]) ==
                   plain.text(plain.token_types()[b.first_token + k]);
        bad += !same;
    }
    check("verses differing in reference, words or sums", bad, 0);
    check("marks dropped (must be > 0: 1 = yes)", st.marks > 0, 1);
    const Verse &first = pointed.verses().front();
    uint64_t letters = 0;
    for (uint32_t k = 0; k < first.n_tokens; k++)
        letters += pointed.letters(pointed.token_types()[first.first_token + k]);
    check("Gen 1:1 words", first.n_tokens, 7);
    check("Gen 1:1 letters", letters, 28);
    check("Gen 1:1 hechrachi", first.sum[kHechrachi], 2701);
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: gem-kab-test WLC.txt POINTED-GEN1.txt\n");
        return 2;
    }
    try {
        Corpus plain, pointed;
        plain.load(argv[1], "WLC");
        pointed.load(argv[2], "GEN1");
        tables_against_core();
        corpus_against_core(plain);
        swar_against_plain();
        pointed_against_plain(plain, pointed);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "gem-kab-test: %s\n", e.what());
        return 1;
    }
    std::printf("kab test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
