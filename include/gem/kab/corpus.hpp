// corpus.hpp - stage K, UTF-8 Hebrew corpora: loading, tokenizing, word
// types, and the value indexes.
//
// Input format: one verse (or mishnah, or line) per text line. If a line
// has a TAB and the part before it holds no Hebrew letter, that part is the
// verse reference; otherwise the reference is "<name>:<line number>".
//
// Tokenizer contract (classes from the Unicode 17.0 Hebrew chart,
// https://unicode.org/charts/PDF/U0590.pdf):
//   letters   U+05D0..U+05EA, kept, finals kept as written
//   marks     accents and points U+0591..U+05C7 except the punctuation
//             below, and U+034F: dropped, they do not end a word
//   punct     maqaf U+05BE, paseq U+05C0, sof pasuq U+05C3, nun hafukha
//             U+05C6, geresh U+05F3, gershayim U+05F4: end a word
//   ASCII     every code point below U+0080 ends a word
//   foreign   anything else, and every malformed UTF-8 byte: ends a word
// Every class is counted per corpus, so a corpus with unexpected content
// shows it in its LoadStats instead of failing or passing silently. Words
// longer than kMaxWordLetters are dropped and counted (overlong).
//
// A word type is a distinct letter string. Each type carries its packed key
// (the four values), its letter multiset, its token count and the set of
// corpora it occurs in.
#pragma once

#include "gem/kab/tables.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gem::kab {

// value -> the strings carrying it (word types, or verse references).
using ValueIndex = std::unordered_map<int, std::vector<std::string>>;

inline constexpr unsigned kMaxCorpora = 8;

struct LoadStats {
    std::string name, path;
    uint64_t bytes = 0, lines = 0, verses = 0, tokens = 0, letters = 0;
    uint64_t marks = 0, punct = 0, ascii = 0, foreign = 0, malformed = 0, overlong = 0;
    uint32_t new_types = 0;  // types first seen in this corpus
    double seconds = 0;
};

struct Verse {
    std::string ref;
    uint32_t first_token = 0, n_tokens = 0;
    std::array<uint32_t, kSystems> sum{};
    uint8_t corpus = 0;
};

// One word as the tokenizer sees it.
struct Word {
    std::string text;  // UTF-8 letters
    uint64_t key = 0;
    Multiset multiset;
    unsigned letters = 0;
};

// The words of a UTF-8 string under the tokenizer contract (overlong words
// included, with only their first kMaxWordLetters letters summed).
std::vector<Word> split_words(std::string_view utf8);

class Corpus {
public:
    // Appends the file as corpus number corpora(). Throws std::runtime_error
    // if it cannot be read or kMaxCorpora is exceeded.
    const LoadStats &load(const std::string &path, const std::string &name);

    unsigned corpora() const { return static_cast<unsigned>(stats_.size()); }
    const LoadStats &stats(unsigned k) const { return stats_[k]; }

    uint32_t types() const { return static_cast<uint32_t>(key_.size()); }
    std::span<const uint64_t> keys() const { return key_; }
    uint64_t key(uint32_t t) const { return key_[t]; }
    int value(uint32_t t, System s) const { return static_cast<int>(lane(key_[t], s)); }
    const Multiset &multiset(uint32_t t) const { return multiset_[t]; }
    const std::string &text(uint32_t t) const { return text_[t]; }
    unsigned letters(uint32_t t) const { return letters_[t]; }
    uint32_t tokens(uint32_t t) const { return tokens_[t]; }
    // Bit k set iff the type occurs in corpus k.
    unsigned in_corpora(uint32_t t) const { return in_corpora_[t]; }
    // Type of a letter string, or -1.
    int64_t find(std::string_view letters) const;

    const std::vector<Verse> &verses() const { return verses_; }
    // Type of every token, in text order (Verse::first_token indexes it).
    const std::vector<uint32_t> &token_types() const { return token_type_; }

    // Word types by their value in system s.
    ValueIndex word_index(System s) const;
    // Verse references by the verse's letter sum in system s.
    ValueIndex verse_index(System s) const;

private:
    uint32_t intern(const Word &w, unsigned corpus, LoadStats &st);

    std::vector<LoadStats> stats_;
    std::vector<uint64_t> key_;
    std::vector<Multiset> multiset_;
    std::vector<std::string> text_;
    std::vector<uint8_t> letters_, in_corpora_;
    std::vector<uint32_t> tokens_;
    std::unordered_map<std::string, uint32_t> by_text_;
    std::vector<Verse> verses_;
    std::vector<uint32_t> token_type_;
};

}  // namespace gem::kab
