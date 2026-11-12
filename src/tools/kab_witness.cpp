// kab_witness.cpp - gem-kab witness: letter-exact comparison of two
// witnesses of the same text (layer L0 of the framework).
//
//   witness [SITES.tsv]
//
// Corpus 0 is the base witness, corpus 1 the second one. The unit of
// alignment is the chapter (the reference up to its ':'), not the verse:
// editions number verses differently in places (the Decalogue), and a
// comparison by verse number would then compare different stretches of text.
// Inside a chapter the two word sequences are aligned (longest common
// subsequence over words); every run of words that does not align is one
// site, reported under the base verse it starts in. A site is
//   division         the same letters, divided into words differently
//   plene/defective  the letters differ only in vav and yod
//   other            anything else
// Printed: totals per book (with both verse counts), the per-letter balance,
// and the site kinds; SITES.tsv receives every site.
#include "kab_cmd.hpp"

#include <array>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace kabtool {

using namespace gem::kab;

namespace {

constexpr unsigned kVav = 5, kYod = 9;  // base letter indexes

struct Chapter {
    std::string key;                 // "Gen 1"
    std::vector<uint32_t> words;     // type ids, in text order
    std::vector<uint32_t> verse_of;  // index into Corpus::verses() of each word
    uint32_t verses = 0;
};

struct Tally {
    uint64_t chapters = 0, verses_a = 0, verses_b = 0, letters_a = 0, letters_b = 0, same = 0, differing = 0;
    void add(const Tally &o)
    {
        chapters += o.chapters;
        verses_a += o.verses_a;
        verses_b += o.verses_b;
        letters_a += o.letters_a;
        letters_b += o.letters_b;
        same += o.same;
        differing += o.differing;
    }
};

struct Site {
    std::string ref, words_a, words_b;
    const char *kind;
    int delta;  // letters in the second witness minus letters in the base
};

// The chapters of one corpus, in text order.
std::vector<Chapter> chapters_of(const Corpus &c, unsigned corpus)
{
    std::vector<Chapter> out;
    for (uint32_t v = 0; v < c.verses().size(); v++) {
        const Verse &verse = c.verses()[v];
        if (verse.corpus != corpus)
            continue;
        std::string key = verse.ref.substr(0, verse.ref.find(':'));
        if (out.empty() || out.back().key != key)
            out.push_back({key, {}, {}, 0});
        Chapter &ch = out.back();
        ch.verses++;
        for (uint32_t k = 0; k < verse.n_tokens; k++) {
            ch.words.push_back(c.token_types()[verse.first_token + k]);
            ch.verse_of.push_back(v);
        }
    }
    return out;
}

// Letter code (0..26) at an odd byte of a type's text: every letter is the
// two bytes D7, 90 + code.
unsigned code_at(const std::string &text, size_t i)
{
    return static_cast<unsigned char>(text[i]) - 0x90u;
}

struct Block {
    std::string words;           // the words, space separated
    std::vector<uint8_t> bases;  // their letters, finals folded
    std::array<int, kBaseLetters> count{};
};

Block block_of(const Corpus &c, const std::vector<uint32_t> &types)
{
    Block b;
    for (uint32_t t : types) {
        const std::string &text = c.text(t);
        b.words += (b.words.empty() ? "" : " ") + text;
        for (size_t i = 1; i < text.size(); i += 2) {
            unsigned base = kLetters[code_at(text, i)].value[kSiduri] - 1u;
            b.bases.push_back(static_cast<uint8_t>(base));
            b.count[base]++;
        }
    }
    return b;
}

Site site_of(const Corpus &c, const std::string &ref, const std::vector<uint32_t> &a, const std::vector<uint32_t> &b)
{
    Block x = block_of(c, a), y = block_of(c, b);
    const char *kind = "division";
    if (x.bases != y.bases) {
        kind = "plene/defective";
        for (unsigned letter = 0; letter < kBaseLetters; letter++)
            if (x.count[letter] != y.count[letter] && letter != kVav && letter != kYod)
                kind = "other";
    }
    return {ref, x.words, y.words, kind, static_cast<int>(y.bases.size()) - static_cast<int>(x.bases.size())};
}

// The sites of one chapter pair: runs of words outside the longest common
// subsequence of the two word sequences. Returns how many were added.
size_t sites_of(const Corpus &c, const Chapter &a, const Chapter &b, std::vector<Site> &out)
{
    const size_t n = a.words.size(), m = b.words.size(), before = out.size();
    std::vector<uint16_t> lcs((n + 1) * (m + 1), 0);
    auto at = [&](size_t i, size_t j) -> uint16_t & { return lcs[i * (m + 1) + j]; };
    for (size_t i = n; i-- > 0;)
        for (size_t j = m; j-- > 0;)
            at(i, j) = a.words[i] == b.words[j] ? static_cast<uint16_t>(at(i + 1, j + 1) + 1)
                                                : std::max(at(i + 1, j), at(i, j + 1));
    std::vector<uint32_t> run_a, run_b;
    size_t run_start = 0;  // position in a where the current run began
    auto flush = [&] {
        if (run_a.empty() && run_b.empty())
            return;
        const std::string &ref = c.verses()[a.verse_of[std::min(run_start, n - 1)]].ref;
        out.push_back(site_of(c, ref, run_a, run_b));
        run_a.clear();
        run_b.clear();
    };
    size_t i = 0, j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && a.words[i] == b.words[j]) {
            flush();
            i++;
            j++;
            continue;
        }
        if (run_a.empty() && run_b.empty())
            run_start = i;
        if (j == m || (i < n && at(i + 1, j) >= at(i, j + 1)))
            run_a.push_back(a.words[i++]);
        else
            run_b.push_back(b.words[j++]);
    }
    flush();
    return out.size() - before;
}

// Adds the letters of a chapter to a per-code histogram; returns their count.
uint64_t count_letters(const Corpus &c, const Chapter &ch, std::array<int64_t, kCodes> &hist)
{
    uint64_t n = 0;
    for (uint32_t t : ch.words) {
        const std::string &text = c.text(t);
        for (size_t i = 1; i < text.size(); i += 2)
            hist[code_at(text, i)]++;
        n += c.letters(t);
    }
    return n;
}

void print_tally(const char *label, const Tally &t)
{
    std::printf("  %-6s %8s %8s %8s %11s %11s %+7lld %10s %10s\n", label, commas(t.chapters).c_str(),
                commas(t.verses_a).c_str(), commas(t.verses_b).c_str(), commas(t.letters_a).c_str(),
                commas(t.letters_b).c_str(), static_cast<long long>(t.letters_b) - static_cast<long long>(t.letters_a),
                commas(t.same).c_str(), commas(t.differing).c_str());
}

}  // namespace

int cmd_witness(const Corpus &c, Args args)
{
    if (c.corpora() != 2 || args.size() > 1) {
        std::fprintf(stderr, "usage: BASE=PATH SECOND=PATH witness [SITES.tsv]\n");
        return 2;
    }
    std::vector<Chapter> base = chapters_of(c, 0), second = chapters_of(c, 1);
    std::map<std::string, const Chapter *> base_by_key;
    for (const Chapter &ch : base)
        base_by_key[ch.key] = &ch;

    std::vector<std::pair<std::string, Tally>> books;
    std::array<int64_t, kCodes> letters_a{}, letters_b{};
    std::vector<Site> sites;
    uint64_t unmatched = 0;
    for (const Chapter &b : second) {
        auto it = base_by_key.find(b.key);
        if (it == base_by_key.end() || it->second->words.empty()) {
            unmatched++;
            continue;
        }
        const Chapter &a = *it->second;
        std::string book = b.key.substr(0, b.key.find(' '));
        if (books.empty() || books.back().first != book)
            books.push_back({book, Tally{}});
        Tally &t = books.back().second;
        t.chapters++;
        t.verses_a += a.verses;
        t.verses_b += b.verses;
        t.letters_a += count_letters(c, a, letters_a);
        t.letters_b += count_letters(c, b, letters_b);
        (sites_of(c, a, b, sites) == 0 ? t.same : t.differing)++;
    }

    const char *name_a = c.stats(0).name.c_str(), *name_b = c.stats(1).name.c_str();
    std::printf("witness %s against %s, aligned by chapter (%s chapters of %s have no base chapter):\n", name_b, name_a,
                commas(unmatched).c_str(), name_b);
    std::printf("  %-6s %8s %8s %8s %11s %11s %7s %10s %10s\n", "book", "chapters", "verses", "verses", "letters",
                "letters", "delta", "chapters", "chapters");
    std::printf("  %-6s %8s %8s %8s %11s %11s %7s %10s %10s\n", "", "", name_a, name_b, name_a, name_b, "", "identical",
                "differing");
    Tally total;
    for (const auto &[book, t] : books) {
        print_tally(book.c_str(), t);
        total.add(t);
    }
    print_tally("all", total);

    std::printf("letters whose count differs (code point: %s, %s, delta):\n ", name_a, name_b);
    for (unsigned code = 0; code < kCodes; code++)
        if (letters_a[code] != letters_b[code])
            std::printf(" U+%04X %lld %lld %+lld;", static_cast<unsigned>(kFirstLetter) + code,
                        static_cast<long long>(letters_a[code]), static_cast<long long>(letters_b[code]),
                        static_cast<long long>(letters_b[code] - letters_a[code]));
    constexpr unsigned tsadi = 0x05E6 - 0x05D0, final_tsadi = 0x05E5 - 0x05D0;
    std::printf("\n  tsadi + final tsadi: %s %lld, %s %lld\n", name_a,
                static_cast<long long>(letters_a[tsadi] + letters_a[final_tsadi]), name_b,
                static_cast<long long>(letters_b[tsadi] + letters_b[final_tsadi]));

    std::map<std::string, std::pair<size_t, int>> by_kind;
    for (const Site &s : sites) {
        by_kind[s.kind].first++;
        by_kind[s.kind].second += s.delta;
    }
    std::printf("sites: %s\n", commas(sites.size()).c_str());
    for (const auto &[kind, n] : by_kind)
        std::printf("  %-16s %5zu sites, net %+d letters\n", kind.c_str(), n.first, n.second);

    if (args.size() == 1) {
        std::FILE *f = std::fopen(args[0], "wb");
        if (!f)
            throw std::runtime_error(std::string("cannot write ") + args[0]);
        std::fprintf(f, "# sites where %s differs from %s, aligned by chapter; delta = letters in %s minus letters in %s\n",
                     name_b, name_a, name_b, name_a);
        std::fprintf(f, "ref\tkind\tdelta\t%s\t%s\n", name_a, name_b);
        for (const Site &s : sites)
            std::fprintf(f, "%s\t%s\t%+d\t%s\t%s\n", s.ref.c_str(), s.kind, s.delta, s.words_a.c_str(), s.words_b.c_str());
        if (std::fclose(f) != 0)
            throw std::runtime_error(std::string("cannot write ") + args[0]);
        std::printf("  written to %s\n", args[0]);
    }
    return 0;
}

}  // namespace kabtool
