// corpus.cpp - stage K, UTF-8 Hebrew corpora: strict decoding, the
// tokenizer, type interning and the value indexes. Contract: corpus.hpp.
#include "gem/kab/corpus.hpp"

#include <chrono>
#include <fstream>
#include <stdexcept>

namespace gem::kab {

namespace {

enum class Cls : uint8_t { Letter, Mark, Punct, Space, Ascii, Foreign };

// Classes of U+0590..U+05FF, built at compile time.
constexpr uint32_t kBlockFirst = 0x0590, kBlockSize = 0x70;

constexpr std::array<Cls, kBlockSize> make_block()
{
    std::array<Cls, kBlockSize> t{};
    for (Cls &c : t)
        c = Cls::Foreign;
    for (uint32_t cp = 0x0591; cp <= 0x05C7; cp++)
        t[cp - kBlockFirst] = Cls::Mark;
    for (uint32_t cp : {0x05BEu, 0x05C0u, 0x05C3u, 0x05C6u, 0x05F3u, 0x05F4u})
        t[cp - kBlockFirst] = Cls::Punct;
    for (uint32_t c = 0; c < kCodes; c++)
        t[static_cast<uint32_t>(kFirstLetter) + c - kBlockFirst] = Cls::Letter;
    return t;
}

constexpr std::array<Cls, kBlockSize> kBlock = make_block();

constexpr Cls classify(uint32_t cp)
{
    if (cp < 0x80)
        return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' ? Cls::Space : Cls::Ascii;
    if (cp - kBlockFirst < kBlockSize)
        return kBlock[cp - kBlockFirst];
    return cp == 0x034F ? Cls::Mark : Cls::Foreign;
}

constexpr bool continuation(unsigned char b)
{
    return (b & 0xC0u) == 0x80u;
}

// One scalar value from p[0..n), n >= 1. Returns the bytes used, or 0 for a
// malformed, overlong, surrogate or out-of-range sequence.
size_t decode(const unsigned char *p, size_t n, uint32_t &cp)
{
    uint32_t b0 = p[0];
    if (b0 < 0x80) {
        cp = b0;
        return 1;
    }
    if (b0 < 0xC2)
        return 0;
    if (b0 < 0xE0) {
        if (n < 2 || !continuation(p[1]))
            return 0;
        cp = (b0 & 0x1Fu) << 6 | (p[1] & 0x3Fu);
        return 2;
    }
    if (b0 < 0xF0) {
        if (n < 3 || !continuation(p[1]) || !continuation(p[2]))
            return 0;
        cp = (b0 & 0x0Fu) << 12 | (p[1] & 0x3Fu) << 6 | (p[2] & 0x3Fu);
        return cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF) ? 0 : 3;
    }
    if (b0 < 0xF5) {
        if (n < 4 || !continuation(p[1]) || !continuation(p[2]) || !continuation(p[3]))
            return 0;
        cp = (b0 & 0x07u) << 18 | (p[1] & 0x3Fu) << 12 | (p[2] & 0x3Fu) << 6 | (p[3] & 0x3Fu);
        return cp < 0x10000 || cp > 0x10FFFF ? 0 : 4;
    }
    return 0;
}

// Calls sink(word) for every word of text and counts the classes in st.
template <typename Sink>
void scan(std::string_view text, LoadStats &st, Sink &&sink)
{
    const auto *p = reinterpret_cast<const unsigned char *>(text.data());
    const unsigned char *end = p + text.size();
    Word w;
    auto flush = [&] {
        if (w.letters == 0)
            return;
        sink(w);
        w.text.clear();
        w.key = 0;
        w.multiset = Multiset{};
        w.letters = 0;
    };
    while (p < end) {
        uint32_t cp = 0;
        size_t used = decode(p, static_cast<size_t>(end - p), cp);
        if (used == 0) {
            st.malformed++;
            flush();
            p++;
            continue;
        }
        switch (classify(cp)) {
        case Cls::Letter: {
            unsigned code = letter_code(static_cast<wchar_t>(cp));
            if (w.letters < kMaxWordLetters) {
                w.key += kPacked[code];
                w.multiset += kMultiset[code];
            }
            w.text.append(reinterpret_cast<const char *>(p), used);
            w.letters++;
            break;
        }
        case Cls::Mark:
            st.marks++;
            break;
        case Cls::Punct:
            st.punct++;
            flush();
            break;
        case Cls::Space:
            flush();
            break;
        case Cls::Ascii:
            st.ascii++;
            flush();
            break;
        case Cls::Foreign:
            st.foreign++;
            flush();
            break;
        }
        p += used;
    }
    flush();
}

std::string read_file(const std::string &path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        throw std::runtime_error("cannot read " + path);
    std::string data(static_cast<size_t>(in.tellg()), '\0');
    in.seekg(0);
    in.read(data.data(), static_cast<std::streamsize>(data.size()));
    if (!in)
        throw std::runtime_error("short read of " + path);
    return data;
}

}  // namespace

std::vector<Word> split_words(std::string_view utf8)
{
    std::vector<Word> words;
    LoadStats ignored;
    scan(utf8, ignored, [&](const Word &w) { words.push_back(w); });
    return words;
}

uint32_t Corpus::intern(const Word &w, unsigned corpus, LoadStats &st)
{
    auto [it, fresh] = by_text_.try_emplace(w.text, types());
    if (fresh) {
        key_.push_back(w.key);
        multiset_.push_back(w.multiset);
        text_.push_back(w.text);
        letters_.push_back(static_cast<uint8_t>(w.letters));
        tokens_.push_back(0);
        in_corpora_.push_back(0);
        st.new_types++;
    }
    uint32_t t = it->second;
    tokens_[t]++;
    in_corpora_[t] = static_cast<uint8_t>(in_corpora_[t] | 1u << corpus);
    return t;
}

const LoadStats &Corpus::load(const std::string &path, const std::string &name)
{
    if (corpora() >= kMaxCorpora)
        throw std::runtime_error("more than " + std::to_string(kMaxCorpora) + " corpora");
    std::string data = read_file(path);
    auto t0 = std::chrono::steady_clock::now();
    unsigned corpus = corpora();
    LoadStats st;
    st.name = name;
    st.path = path;
    st.bytes = data.size();
    for (size_t pos = 0; pos < data.size();) {
        size_t nl = data.find('\n', pos);
        if (nl == std::string::npos)
            nl = data.size();
        std::string_view line(data.data() + pos, nl - pos);
        pos = nl + 1;
        st.lines++;
        std::string_view ref, text = line;
        size_t tab = line.find('\t');
        if (tab != std::string_view::npos && line.substr(0, tab).find('\xD7') == std::string_view::npos) {
            ref = line.substr(0, tab);
            text = line.substr(tab + 1);
        }
        Verse v;
        v.first_token = static_cast<uint32_t>(token_type_.size());
        v.corpus = static_cast<uint8_t>(corpus);
        scan(text, st, [&](const Word &w) {
            if (w.letters > kMaxWordLetters) {
                st.overlong++;
                return;
            }
            token_type_.push_back(intern(w, corpus, st));
            st.tokens++;
            st.letters += w.letters;
            for (unsigned s = 0; s < kSystems; s++)
                v.sum[s] += lane(w.key, static_cast<System>(s));
        });
        v.n_tokens = static_cast<uint32_t>(token_type_.size()) - v.first_token;
        if (v.n_tokens == 0)
            continue;
        v.ref = ref.empty() ? name + ":" + std::to_string(st.lines) : std::string(ref);
        verses_.push_back(std::move(v));
        st.verses++;
    }
    st.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    stats_.push_back(std::move(st));
    return stats_.back();
}

int64_t Corpus::find(std::string_view letters) const
{
    auto it = by_text_.find(std::string(letters));
    return it == by_text_.end() ? -1 : static_cast<int64_t>(it->second);
}

ValueIndex Corpus::word_index(System s) const
{
    ValueIndex index;
    for (uint32_t t = 0; t < types(); t++)
        index[value(t, s)].push_back(text_[t]);
    return index;
}

ValueIndex Corpus::verse_index(System s) const
{
    ValueIndex index;
    for (const Verse &v : verses_)
        index[static_cast<int>(v.sum[s])].push_back(v.ref);
    return index;
}

}  // namespace gem::kab
