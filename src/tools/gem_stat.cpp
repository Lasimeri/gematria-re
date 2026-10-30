// gem_stat - statistics CLI over a GEMC corpus (PREREG-001).
//
// Usage:
//   gem-stat CORPUS r1                       R1 over all WLC word types
//   gem-stat CORPUS r2 [M1 M2]               R2 coverage + nulls (default 1000 300)
//   gem-stat CORPUS claim A B [METHODS] [K]  claim auditor; METHODS = name,name|all
//   gem-stat CORPUS word A [METHODS] [K]     values of A and its matching corpus types
//
// Every run first checks the corpus and prereg/PREREG-001.md against their
// frozen sha256 (OpenSSL EVP) and prints the verdict of that check; results
// carry the PREREG-001 label only when both match.
#include "gem/stats.hpp"

extern "C" {
#include "gem/hebrew.h"
}

#include <openssl/evp.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <thread>

#ifndef GEM_SOURCE_DIR
#error "GEM_SOURCE_DIR must name the repository root"
#endif

namespace {

constexpr const char *kCorpusSha = "46ce61ff38c678231f7283b13085a5278bee631ea6b40db62ec2737eccf86575";

// Pichkhadze, "Coincidence is cheap" (sapir861.com/research/coincidence):
// values in 1..861 with no Pentateuch word.
const std::set<unsigned> kSrcLooseMissing = {1, 2, 583, 599, 729, 773, 779, 801, 827, 833, 839, 847, 859};
const std::set<unsigned> kSrcStrictMissing = {
    1,   2,   229, 279, 369, 379, 384, 533, 549, 574, 579, 583, 587, 593, 597, 629, 634, 638, 639,
    642, 644, 664, 669, 694, 699, 721, 727, 729, 733, 739, 754, 765, 767, 771, 773, 774, 779, 783,
    787, 795, 797, 803, 813, 818, 821, 823, 827, 831, 833, 837, 839, 841, 843, 847, 853, 861};

std::string sha256_file(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return "unreadable";
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int n = 0;
    if (!EVP_Digest(data.data(), data.size(), md, &n, EVP_sha256(), nullptr))
        return "digest-failed";
    std::string hex;
    char b[3];
    for (unsigned i = 0; i < n; i++) {
        std::snprintf(b, sizeof b, "%02x", md[i]);
        hex += b;
    }
    return hex;
}

bool check_frozen(const std::string &corpus)
{
    const std::string prereg = std::string(GEM_SOURCE_DIR) + "/prereg/PREREG-001.md";
    std::ifstream f(std::string(GEM_SOURCE_DIR) + "/prereg/PREREG-001.sha256");
    std::string want_prereg;
    f >> want_prereg;
    std::string got_corpus = sha256_file(corpus), got_prereg = sha256_file(prereg);
    bool ok = got_corpus == kCorpusSha && got_prereg == want_prereg;
    std::printf("PREREG-001 %s (prereg %.12s %s, corpus %.12s %s)\n", ok ? "VERIFIED" : "NOT VERIFIED",
                got_prereg.c_str(), got_prereg == want_prereg ? "ok" : "MISMATCH", got_corpus.c_str(),
                got_corpus == kCorpusSha ? "ok" : "MISMATCH");
    return ok;
}

std::vector<uint8_t> parse_word(const char *text)
{
    std::vector<uint8_t> codes(256);
    unsigned words = 0;
    long n = gem_parse_text(text, codes.data(), codes.size(), &words);
    if (n <= 0)
        throw std::runtime_error(std::string("not a Hebrew word: ") + text);
    codes.resize(static_cast<size_t>(n));
    return codes;
}

uint32_t parse_methods(const char *spec)
{
    if (!spec || !std::strcmp(spec, "all"))
        return spec ? gem::kAllMethods : 1u << GEM_M_HECHRACHI;
    uint32_t mask = 0;
    std::stringstream ss(spec);
    std::string name;
    while (std::getline(ss, name, ',')) {
        unsigned m = 0;
        while (m < gem::kMethods && name != gem_method_name(static_cast<gem_method>(m)))
            m++;
        if (m == gem::kMethods)
            throw std::runtime_error("unknown method " + name);
        mask |= 1u << m;
    }
    return mask;
}

std::string utf8(std::span<const uint8_t> codes)
{
    std::string s;
    char b[2];
    for (uint8_t c : codes) {
        gem_letter_utf8(c, b);
        s.append(b, 2);
    }
    return s;
}

void print_set_diff(const char *what, const std::vector<unsigned> &ours, const std::set<unsigned> &src)
{
    std::set<unsigned> o(ours.begin(), ours.end());
    std::vector<unsigned> only_ours, only_src;
    std::set_difference(o.begin(), o.end(), src.begin(), src.end(), std::back_inserter(only_ours));
    std::set_difference(src.begin(), src.end(), o.begin(), o.end(), std::back_inserter(only_src));
    std::printf("  %s missing in 1..861: ours %zu, source %zu, only ours {", what, o.size(), src.size());
    for (unsigned v : only_ours)
        std::printf(" %u", v);
    std::printf(" }, only source {");
    for (unsigned v : only_src)
        std::printf(" %u", v);
    std::printf(" }%s\n", only_ours.empty() && only_src.empty() ? "  EXACT" : "");
}

void print_null(const char *name, const gem::NullRun &n, unsigned observed)
{
    std::printf("  %-10s %4u reps  mean %7.2f  sd %5.2f  z %+7.2f  reps <= observed(%u): %u  [%.2f s]\n", name,
                n.replicates, n.mean, n.sd, n.z, observed, n.at_or_below, n.seconds);
}

int cmd_r1(const gem::Corpus &corpus, unsigned threads)
{
    gem::Lexicon lex(corpus.raw(), 0, corpus.raw().n_books - 1, gem::Rule::Loose);
    gem::R1 r = gem::run_r1(lex, threads);
    std::printf("R1 (WLC analogue of dictionary figures; %u types, %llu tokens, %.2f s):\n", r.types,
                static_cast<unsigned long long>(lex.total_tokens()), r.seconds);
    std::printf("  pair collision, hechrachi           1 in %.0f   (source: 1 in 692, Tanakh dictionary)\n",
                1.0 / r.pair_collision);
    std::printf("  30 random types share a value       %.1f %%   (source: 45 %%)\n", 100.0 * r.birthday30);
    std::printf("  median match share, hechrachi K=0   %.3f %%   (source: 0.13 %%)\n", 100.0 * r.median_share_std);
    std::printf("  median match share, 16 methods K=1  %.1f %%   (source: 34.9 %%, 17 methods)\n",
                100.0 * r.median_share_all);
    return 0;
}

int cmd_r2(const gem::Corpus &corpus, unsigned m1, unsigned m2, unsigned threads)
{
    gem::R2 r = gem::run_r2(corpus.raw(), m1, m2, threads);
    std::printf("R2 (Pentateuch coverage of 1..1000, hechrachi):\n");
    std::printf("  loose  %u   (source 937)\n  strict %u   (source 867)\n", r.loose, r.strict);
    print_set_diff("loose ", r.loose_missing_861, kSrcLooseMissing);
    print_set_diff("strict", r.strict_missing_861, kSrcStrictMissing);
    std::printf("nulls, strict (source: M1 946.9 +/- 4.9, M2 938.1 +/- 5.1):\n");
    print_null("M1 strict", r.m1_strict, r.strict);
    print_null("M2 strict", r.m2_strict, r.strict);
    std::printf("nulls, loose:\n");
    print_null("M1 loose", r.m1_loose, r.loose);
    print_null("M2 loose", r.m2_loose, r.loose);
    return 0;
}

int cmd_r2x(const gem::Corpus &corpus)
{
    std::printf("R2x EXPLORATORY (not preregistered): strict-rule variants vs source 867\n");
    const struct {
        const char *name;
        gem::StrictVariant v;
    } variants[] = {
        {"V0 PREREG rule", {false, false}},
        {"V1 no proper nouns", {true, false}},
        {"V2 suffixes stripped", {false, true}},
        {"V3 V1 + V2", {true, true}},
    };
    for (const auto &x : variants) {
        gem::VariantCoverage vc = gem::strict_variant(corpus.raw(), x.v);
        std::printf("%-22s coverage %u\n", x.name, vc.covered);
        print_set_diff("strict", vc.missing_861, kSrcStrictMissing);
    }
    return 0;
}

void print_values(const char *label, std::span<const uint8_t> w)
{
    gem::Values v = gem::all_values(w);
    std::printf("%s %s:", label, utf8(w).c_str());
    for (unsigned m = 0; m < gem::kMethods; m++)
        std::printf(" %s=%llu", gem_method_name(static_cast<gem_method>(m)), static_cast<unsigned long long>(v[m]));
    std::printf("\n");
}

int cmd_claim(const gem::Corpus &corpus, const char *a, const char *b, uint32_t mask, int kolel)
{
    gem::Lexicon lex(corpus.raw(), 0, corpus.raw().n_books - 1, gem::Rule::Loose);
    auto wa = parse_word(a), wb = parse_word(b);
    print_values("A", wa);
    print_values("B", wb);
    gem::ClaimVerdict v = gem::audit_claim(lex, wa, wb, mask, kolel);
    std::printf("claim A = B under %u method(s), K = +/-%d: %s", static_cast<unsigned>(std::popcount(mask)), kolel,
                v.holds ? "HOLDS via" : "DOES NOT HOLD");
    for (unsigned m = 0; m < gem::kMethods; m++)
        if (v.matching_methods >> m & 1u)
            std::printf(" %s", gem_method_name(static_cast<gem_method>(m)));
    std::printf("\n  corpus types matching A as well: %u of %u (p = %.4f types, %.4f tokens), DoF price %u\n",
                v.share.types, lex.size() - (lex.find(wa) >= 0 ? 1u : 0u), v.p_types, v.p_tokens, v.dof);
    std::printf("  verdict: %s (alpha %.2f)\n",
                !v.holds ? "no equality to assess"
                : v.rare ? "rare under the declared method set"
                         : "not distinguishable from chance",
                gem::kAlpha);
    return 0;
}

int cmd_word(const gem::Corpus &corpus, const char *a, uint32_t mask, int kolel)
{
    gem::Lexicon lex(corpus.raw(), 0, corpus.raw().n_books - 1, gem::Rule::Loose);
    auto wa = parse_word(a);
    print_values("word", wa);
    std::vector<uint32_t> hits;
    gem::Share s = gem::match_share(lex, gem::all_values(wa), mask, kolel, lex.find(wa), &hits);
    std::sort(hits.begin(), hits.end(), [&](uint32_t x, uint32_t y) { return lex.tokens(x) > lex.tokens(y); });
    std::printf("%u matching types (%llu tokens); most frequent:\n", s.types, static_cast<unsigned long long>(s.tokens));
    for (size_t i = 0; i < std::min<size_t>(hits.size(), 12); i++)
        std::printf("  %-12s x%-5u first %s\n", utf8(lex.letters(hits[i])).c_str(), lex.tokens(hits[i]),
                    corpus.ref(lex.first_word(hits[i])).c_str());
    return 0;
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s CORPUS r1|r2|claim|word ...\n", argv[0]);
        return 2;
    }
    try {
        check_frozen(argv[1]);
        gem::Corpus corpus(argv[1]);
        unsigned threads = std::max(1u, std::thread::hardware_concurrency());
        std::string cmd = argv[2];
        if (cmd == "r1")
            return cmd_r1(corpus, threads);
        if (cmd == "r2")
            return cmd_r2(corpus, argc > 3 ? static_cast<unsigned>(std::stoul(argv[3])) : 1000u,
                          argc > 4 ? static_cast<unsigned>(std::stoul(argv[4])) : 300u, threads);
        if (cmd == "r2x")
            return cmd_r2x(corpus);
        if (cmd == "claim" && argc >= 5)
            return cmd_claim(corpus, argv[3], argv[4], parse_methods(argc > 5 ? argv[5] : nullptr),
                             argc > 6 ? std::stoi(argv[6]) : 0);
        if (cmd == "word" && argc >= 4)
            return cmd_word(corpus, argv[3], parse_methods(argc > 4 ? argv[4] : nullptr),
                            argc > 5 ? std::stoi(argv[5]) : 0);
        std::fprintf(stderr, "bad command\n");
        return 2;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
