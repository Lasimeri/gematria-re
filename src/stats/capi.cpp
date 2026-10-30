// capi.cpp - C ABI of the statistics engine (include/gem/gemstat.h).
// Every entry point catches all exceptions; failures become return values.
#include "gem/gemstat.h"
#include "gem/stats.hpp"

extern "C" {
#include "gem/hebrew.h"
}

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <exception>
#include <map>
#include <new>
#include <thread>
#include <utility>

struct gemstat {
    gem::Corpus corpus;
    gem::Lexicon lex;
    std::string frozen;
    bool frozen_ok = false;
    std::map<std::pair<uint32_t, int>, double> base_rate;  // (mask, kolel) -> fraction of types with p < alpha

    explicit gemstat(const char *path)
        : corpus(path), lex(corpus.raw(), 0, corpus.raw().n_books - 1, gem::Rule::Loose)
    {
        frozen = gem::frozen_status(path, &frozen_ok);
    }
};

namespace {

bool parse(const char *text, std::vector<uint8_t> &codes, unsigned *words)
{
    codes.resize(256);
    unsigned w = 0;
    long n = text ? gem_parse_text(text, codes.data(), codes.size(), &w) : -1;
    if (n <= 0)
        return false;
    codes.resize(static_cast<size_t>(n));
    if (words)
        *words = w;
    return true;
}

void copy(char *dst, size_t cap, const std::string &src)
{
    std::snprintf(dst, cap, "%s", src.c_str());
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

void fill_share(const gem::Lexicon &lex, int64_t self, const gem::Share &s, uint32_t mask, int kolel,
                gemstat_claim *out)
{
    out->share_types = s.types;
    out->share_tokens = s.tokens;
    out->other_types = lex.size() - (self >= 0 ? 1u : 0u);
    uint64_t other_tokens = lex.total_tokens() - (self >= 0 ? lex.tokens(static_cast<uint32_t>(self)) : 0u);
    out->p_types = out->other_types ? static_cast<double>(s.types) / out->other_types : 0.0;
    out->p_tokens = other_tokens ? static_cast<double>(s.tokens) / static_cast<double>(other_tokens) : 0.0;
    out->dof = static_cast<uint32_t>(std::popcount(mask)) * static_cast<uint32_t>(2 * kolel + 1);
    out->alpha = gem::kAlpha;
    out->rare = out->p_types < gem::kAlpha;
}

}  // namespace

extern "C" {

gemstat *gemstat_open(const char *corpus_path, char *err, size_t errlen)
{
    try {
        return new gemstat(corpus_path);
    } catch (const std::exception &e) {
        if (err && errlen)
            std::snprintf(err, errlen, "%s", e.what());
    } catch (...) {
        if (err && errlen)
            std::snprintf(err, errlen, "unknown error");
    }
    return nullptr;
}

void gemstat_close(gemstat *g)
{
    delete g;
}

void gemstat_get_info(const gemstat *g, gemstat_info *out)
{
    out->types = g->lex.size();
    out->tokens = g->lex.total_tokens();
    out->frozen_ok = g->frozen_ok;
    copy(out->frozen, sizeof out->frozen, g->frozen);
}

const char *gemstat_method_name(unsigned m)
{
    return gem_method_name(static_cast<gem_method>(m));
}

long gemstat_values(const char *text, uint64_t out[GEMSTAT_METHODS], unsigned *words)
{
    try {
        std::vector<uint8_t> codes;
        if (!parse(text, codes, words))
            return -1;
        gem::Values v = gem::all_values(codes);
        std::copy(v.begin(), v.end(), out);
        return static_cast<long>(codes.size());
    } catch (...) {
        return -1;
    }
}

int gemstat_audit(gemstat *g, const char *a, const char *b, uint32_t mask, int kolel, gemstat_claim *out)
{
    try {
        std::vector<uint8_t> ca, cb;
        if (!parse(a, ca, nullptr) || !parse(b, cb, nullptr))
            return -1;
        gem::ClaimVerdict v = gem::audit_claim(g->lex, ca, cb, mask, kolel);
        std::memset(out, 0, sizeof *out);
        out->holds = v.holds;
        out->matching = v.matching_methods;
        fill_share(g->lex, g->lex.find(ca), v.share, mask, kolel, out);
        return 0;
    } catch (...) {
        return -1;
    }
}

double gemstat_base_rate(gemstat *g, uint32_t mask, int kolel)
{
    try {
        auto key = std::make_pair(mask, kolel);
        auto it = g->base_rate.find(key);
        if (it != g->base_rate.end())
            return it->second;
        std::vector<double> sh = gem::type_shares(g->lex, mask, kolel, std::max(1u, std::thread::hardware_concurrency()));
        double below = static_cast<double>(std::count_if(sh.begin(), sh.end(), [](double x) { return x < gem::kAlpha; }));
        double rate = sh.empty() ? 0.0 : below / static_cast<double>(sh.size());
        g->base_rate.emplace(key, rate);
        return rate;
    } catch (...) {
        return -1.0;
    }
}

long gemstat_matches(gemstat *g, const char *a, uint32_t mask, int kolel, gemstat_claim *out, gemstat_hit *hits,
                     uint32_t cap)
{
    try {
        std::vector<uint8_t> ca;
        if (!parse(a, ca, nullptr))
            return -1;
        int64_t self = g->lex.find(ca);
        std::vector<uint32_t> found;
        gem::Share s = gem::match_share(g->lex, gem::all_values(ca), mask, kolel, self, &found);
        std::memset(out, 0, sizeof *out);
        fill_share(g->lex, self, s, mask, kolel, out);
        const size_t n = std::min<size_t>(found.size(), cap);
        std::partial_sort(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(n), found.end(),
                          [&](uint32_t x, uint32_t y) { return g->lex.tokens(x) > g->lex.tokens(y); });
        for (size_t i = 0; i < n; i++) {
            copy(hits[i].word, sizeof hits[i].word, utf8(g->lex.letters(found[i])));
            hits[i].tokens = g->lex.tokens(found[i]);
            copy(hits[i].ref, sizeof hits[i].ref, g->corpus.ref(g->lex.first_word(found[i])));
        }
        return static_cast<long>(n);
    } catch (...) {
        return -1;
    }
}

}  // extern "C"
