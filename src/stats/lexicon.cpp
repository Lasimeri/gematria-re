// lexicon.cpp - corpus wrapper, word rules, type lexicon, match shares and
// the claim auditor (prereg/PREREG-001.md, sections "Claim auditor" and
// "R2 word rules").
#include "gem/stats.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace gem {

Values all_values(std::span<const uint8_t> codes)
{
    Values v{};
    for (unsigned m = 0; m < kMethods; m++)
        v[m] = gem_value(static_cast<gem_method>(m), codes.data(), codes.size());
    return v;
}

Corpus::Corpus(const std::string &path)
{
    if (gem_corpus_read(&c_, path.c_str()) != 0)
        throw std::runtime_error("cannot read corpus " + path);
}

Corpus::~Corpus()
{
    gem_corpus_free(&c_);
}

std::string Corpus::ref(uint32_t w) const
{
    const gem_verse &v = c_.verses[c_.words[w].verse];
    return std::string(c_.books[v.book].osis) + " " + std::to_string(v.chapter) + ":" + std::to_string(v.verse);
}

// Morph "HC/Vqq3cp": language letter, then segments split at '/'; the first
// letter of each segment is its part of speech, the second its type
// (e.g. "Np" = proper noun).
WordShape word_shape(const gem_corpus &c, uint32_t w)
{
    const gem_word &word = c.words[w];
    const char *m = c.strpool + word.morph;
    char pos[32], type[32];
    unsigned n = 0;
    if (*m)
        m++;  // language letter
    for (bool at_start = true; *m && n < sizeof pos; m++) {
        if (at_start) {
            pos[n] = m[0];
            type[n] = m[0] && m[1] != '/' ? m[1] : '\0';
            n++;
        }
        at_start = *m == '/';
    }
    // Leading C/R/T segments are prefixes when a later segment is not S.
    unsigned head = 0;
    while (head < n && (pos[head] == 'C' || pos[head] == 'R' || pos[head] == 'T')) {
        bool later_non_suffix = false;
        for (unsigned j = head + 1; j < n; j++)
            later_non_suffix |= pos[j] != 'S';
        if (!later_non_suffix)
            break;
        head++;
    }
    WordShape s{0, 0, false, false};
    if (head < n && n == word.n_parts) {
        for (unsigned i = 0; i < head; i++)
            s.prefix_letters += c.parts[word.first_part + i];
        for (unsigned i = n; i > head + 1 && pos[i - 1] == 'S'; i--)
            s.suffix_letters += c.parts[word.first_part + i - 1];
        s.content = pos[head] == 'N' || pos[head] == 'V' || pos[head] == 'A';
        s.proper = pos[head] == 'N' && type[head] == 'p';
    }
    return s;
}

std::optional<WordSpan> rule_span(const gem_corpus &c, uint32_t w, Rule r)
{
    const gem_word &word = c.words[w];
    if (r == Rule::Loose)
        return WordSpan{word.first_letter, word.n_letters};
    WordShape s = word_shape(c, w);
    if (!s.content || s.prefix_letters >= word.n_letters)
        return std::nullopt;
    return WordSpan{word.first_letter + s.prefix_letters, word.n_letters - s.prefix_letters};
}

Lexicon::Lexicon(const gem_corpus &c, uint32_t book_lo, uint32_t book_hi, Rule rule)
{
    uint32_t w_lo = c.books[book_lo].first_word;
    uint32_t w_hi = c.books[book_hi].first_word + c.books[book_hi].n_words;
    for (uint32_t w = w_lo; w < w_hi; w++) {
        auto sp = rule_span(c, w, rule);
        if (!sp)
            continue;
        std::string key(reinterpret_cast<const char *>(c.letters + sp->first), sp->len);
        auto [it, fresh] = by_letters_.try_emplace(std::move(key), size());
        if (fresh) {
            off_.push_back(static_cast<uint32_t>(pool_.size()));
            len_.push_back(static_cast<uint8_t>(sp->len));
            pool_.insert(pool_.end(), c.letters + sp->first, c.letters + sp->first + sp->len);
            tokens_.push_back(0);
            first_word_.push_back(w);
        }
        tokens_[it->second]++;
        total_tokens_++;
    }
    values_.resize(size());
    for (uint32_t t = 0; t < size(); t++) {
        values_[t] = all_values(letters(t));
        for (unsigned m = 0; m < kMethods; m++)
            index_[m][values_[t][m]].push_back(t);
    }
}

const std::vector<uint32_t> *Lexicon::bucket(unsigned m, uint64_t v) const
{
    auto it = index_[m].find(v);
    return it == index_[m].end() ? nullptr : &it->second;
}

int64_t Lexicon::find(std::span<const uint8_t> codes) const
{
    auto it = by_letters_.find(std::string(reinterpret_cast<const char *>(codes.data()), codes.size()));
    return it == by_letters_.end() ? -1 : static_cast<int64_t>(it->second);
}

Share match_share(const Lexicon &lex, const Values &word, uint32_t mask, int kolel, int64_t exclude,
                  std::vector<uint32_t> *hits)
{
    // Collect the union of the buckets, then deduplicate: a type matching
    // under several methods or offsets counts once.
    std::vector<uint32_t> all;
    for (unsigned m = 0; m < kMethods; m++) {
        if (!(mask >> m & 1u))
            continue;
        for (int k = -kolel; k <= kolel; k++) {
            if (k < 0 && word[m] < static_cast<uint64_t>(-k))
                continue;
            if (const auto *b = lex.bucket(m, word[m] + static_cast<uint64_t>(static_cast<int64_t>(k))))
                all.insert(all.end(), b->begin(), b->end());
        }
    }
    std::sort(all.begin(), all.end());
    all.erase(std::unique(all.begin(), all.end()), all.end());
    Share s;
    for (uint32_t t : all) {
        if (static_cast<int64_t>(t) == exclude)
            continue;
        s.types++;
        s.tokens += lex.tokens(t);
        if (hits)
            hits->push_back(t);
    }
    return s;
}

ClaimVerdict audit_claim(const Lexicon &lex, std::span<const uint8_t> a, std::span<const uint8_t> b,
                         uint32_t mask, int kolel)
{
    ClaimVerdict v;
    Values va = all_values(a), vb = all_values(b);
    for (unsigned m = 0; m < kMethods; m++) {
        if (!(mask >> m & 1u))
            continue;
        int64_t d = static_cast<int64_t>(vb[m]) - static_cast<int64_t>(va[m]);
        if (d >= -kolel && d <= kolel)
            v.matching_methods |= 1u << m;
    }
    v.holds = v.matching_methods != 0;
    int64_t self = lex.find(a);
    v.share = match_share(lex, va, mask, kolel, self);
    uint64_t other_types = lex.size() - (self >= 0 ? 1u : 0u);
    uint64_t other_tokens = lex.total_tokens() - (self >= 0 ? lex.tokens(static_cast<uint32_t>(self)) : 0u);
    v.p_types = other_types ? static_cast<double>(v.share.types) / static_cast<double>(other_types) : 0.0;
    v.p_tokens = other_tokens ? static_cast<double>(v.share.tokens) / static_cast<double>(other_tokens) : 0.0;
    v.dof = static_cast<unsigned>(std::popcount(mask)) * static_cast<unsigned>(2 * kolel + 1);
    v.rare = v.p_types < kAlpha;
    return v;
}

}  // namespace gem
