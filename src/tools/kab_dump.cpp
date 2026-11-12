// kab_dump.cpp - gem-kab dump: the matches to read by hand against the
// reference text.
//
//   dump OUTDIR [ATTESTED.tsv]
//
// The last corpus loaded is the reference text (Sefer Yetzirah in this
// project). Two tab-separated files are written, each with '#' header lines
// naming its inputs and its base rate:
//   OUTDIR/matches-reference.tsv  pairs of words that both occur in the
//       reference text, have different letter multisets and agree at least
//       in hechrachi; most systems agreeing first, then least alike first
//   OUTDIR/matches-all4.tsv       pairs from all corpora that agree in all
//       four systems with different letter multisets; pairs touching the
//       reference text first, then the rarest four-value tuple, then least
//       alike
// ATTESTED.tsv (word A, word B, source) lists equalities already attested in
// the literature: a listed pair gets status "attested", every other pair
// "novel", and each attested pair is audited on stdout whether or not it
// reaches a file. A match is a candidate for reading, never a finding: its
// price (how many pairs agree that much) is printed beside it.
#include "kab_cmd.hpp"

#include "gem/kab/graph.hpp"
#include "gem/kab/vectors.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>

namespace kabtool {

using namespace gem::kab;

namespace {

struct Row {
    Match m;
    unsigned mask, shared, in_reference;  // in_reference: how many of the two words the reference holds
    uint32_t tuple_types;                 // types carrying word a's four values
    float cosine;
};

// Letters the two words have in common, counted with multiplicity.
unsigned shared_letters(const Multiset &a, const Multiset &b)
{
    unsigned n = 0;
    for (unsigned letter = 0; letter < kBaseLetters; letter++)
        n += std::min(a.count(letter), b.count(letter));
    return n;
}

using Attested = std::map<std::pair<std::string, std::string>, std::string>;

std::pair<std::string, std::string> ordered(const std::string &a, const std::string &b)
{
    return a < b ? std::pair{a, b} : std::pair{b, a};
}

// Lines "A <TAB> B <TAB> source"; A and B must each be one Hebrew word.
Attested read_attested(const char *path)
{
    Attested out;
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error(std::string("cannot read ") + path);
    for (std::string line; std::getline(in, line);) {
        size_t t1 = line.find('\t'), t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
        if (t2 == std::string::npos)
            continue;
        std::vector<Word> a = split_words(line.substr(0, t1)), b = split_words(line.substr(t1 + 1, t2 - t1 - 1));
        if (a.size() != 1 || b.size() != 1)
            throw std::runtime_error(std::string(path) + ": not two single words: " + line);
        out[ordered(a[0].text, b[0].text)] = line.substr(t2 + 1);
    }
    return out;
}

struct Context {
    const Corpus &c;
    const ValueGraph &graph;
    const PairCensus &census;
    const Vectors &vectors;
    const KeyIndex &tuples;
    const Attested &attested;
    unsigned reference_bit;

    uint64_t others_at_least(unsigned mask) const
    {
        return census.at_least(mask) - census.anagrams_at_least(mask);
    }
    double one_in(unsigned mask) const
    {
        return static_cast<double>(others_at_least(0)) / static_cast<double>(others_at_least(mask));
    }
    Row row(Match m) const
    {
        Row r{};
        r.m = m;
        r.mask = agree_swar(c.key(m.a), c.key(m.b));
        r.shared = shared_letters(c.multiset(m.a), c.multiset(m.b));
        r.in_reference = ((c.in_corpora(m.a) & reference_bit) != 0) + ((c.in_corpora(m.b) & reference_bit) != 0);
        r.tuple_types = static_cast<uint32_t>(tuples.at(c.key(m.a)).size());
        r.cosine = dot_plain(vectors.row(m.a), vectors.row(m.b));
        return r;
    }
    const char *status(const Row &r) const
    {
        return attested.contains(ordered(c.text(r.m.a), c.text(r.m.b))) ? "attested" : "novel";
    }
};

void write_rows(const Context &x, const std::vector<Row> &rows, const std::string &path, const std::string &what)
{
    std::FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        throw std::runtime_error("cannot write " + path);
    std::fprintf(f, "# %s\n# exploratory, not preregistered: candidates for reading, no significance is claimed\n", what.c_str());
    for (unsigned k = 0; k < x.c.corpora(); k++)
        std::fprintf(f, "# corpus %s: %s, %s words\n", x.c.stats(k).name.c_str(), x.c.stats(k).path.c_str(),
                     commas(x.c.stats(k).tokens).c_str());
    std::fprintf(f, "# base rates over %s type pairs with different letter multisets: at least H--- 1 in %.1f, "
                    "at least HGKS 1 in %.1f\n",
                 commas(x.others_at_least(0)).c_str(), x.one_in(1u << kHechrachi), x.one_in(kAllSystems));
    std::fprintf(f, "rank\tagree\tbase_rate_1_in\thechrachi_a\thechrachi_b\tgadol_a\tgadol_b\tkatan_a\tkatan_b\t"
                    "siduri_a\tsiduri_b\ttypes_same_hechrachi_as_a\ttypes_same_4_values_as_a\ttokens_a\ttokens_b\t"
                    "corpora_a\tcorpora_b\tshared_letters\tcosine\tstatus\tword_a\tword_b\n");
    size_t rank = 0;
    for (const Row &r : rows) {
        uint32_t a = r.m.a, b = r.m.b;
        std::fprintf(f, "%zu\t%s\t%.1f\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%zu\t%u\t%u\t%u\t%s\t%s\t%u\t%.4f\t%s\t%s\t%s\n",
                     ++rank, mask_name(r.mask).c_str(), x.one_in(r.mask), x.c.value(a, kHechrachi),
                     x.c.value(b, kHechrachi), x.c.value(a, kGadol), x.c.value(b, kGadol), x.c.value(a, kKatan),
                     x.c.value(b, kKatan), x.c.value(a, kSiduri), x.c.value(b, kSiduri),
                     x.graph.bucket(kHechrachi, static_cast<unsigned>(x.c.value(a, kHechrachi))).size(),
                     r.tuple_types, x.c.tokens(a), x.c.tokens(b), corpora_of(x.c, a).c_str(),
                     corpora_of(x.c, b).c_str(), r.shared, static_cast<double>(r.cosine), x.status(r),
                     x.c.text(a).c_str(), x.c.text(b).c_str());
    }
    if (std::fclose(f) != 0)
        throw std::runtime_error("cannot write " + path);
}

void print_top(const Context &x, const std::vector<Row> &rows, size_t top)
{
    std::printf("  %-5s %9s %6s %5s %6s %7s %6s %-8s %-8s %-8s %s\n", "agree", "hechrachi", "gadol", "katan", "siduri",
                "cosine", "shared", "status", "corpora", "", "words");
    for (size_t i = 0; i < rows.size() && i < top; i++) {
        const Row &r = rows[i];
        std::printf("  %-5s %9d %6d %5d %6d %7.4f %6u %-8s %-8s %-8s %s  %s\n", mask_name(r.mask).c_str(),
                    x.c.value(r.m.a, kHechrachi), x.c.value(r.m.a, kGadol), x.c.value(r.m.a, kKatan),
                    x.c.value(r.m.a, kSiduri), static_cast<double>(r.cosine), r.shared, x.status(r),
                    corpora_of(x.c, r.m.a).c_str(), corpora_of(x.c, r.m.b).c_str(), x.c.text(r.m.a).c_str(),
                    x.c.text(r.m.b).c_str());
    }
}

void audit_attested(const Context &x)
{
    std::printf("attested equalities (%zu) and where they fall:\n", x.attested.size());
    for (const auto &[pair, source] : x.attested) {
        std::vector<Word> a = split_words(pair.first), b = split_words(pair.second);
        unsigned mask = agree_swar(a[0].key, b[0].key);
        int64_t ta = x.c.find(a[0].text), tb = x.c.find(b[0].text);
        std::printf("  %s (%d of %u systems), 1 in %.1f pairs agree that much; %s / %s; %s  %s  %s\n",
                    mask_name(mask).c_str(), std::popcount(mask), kSystems, x.one_in(mask),
                    ta < 0 ? "absent" : corpora_of(x.c, static_cast<uint32_t>(ta)).c_str(),
                    tb < 0 ? "absent" : corpora_of(x.c, static_cast<uint32_t>(tb)).c_str(), source.c_str(),
                    pair.first.c_str(), pair.second.c_str());
    }
}

}  // namespace

int cmd_dump(const Corpus &c, Args args)
{
    if (args.empty() || args.size() > 2 || c.corpora() < 2) {
        std::fprintf(stderr, "usage: dump OUTDIR [ATTESTED.tsv]   (needs two corpora; the last is the reference)\n");
        return 2;
    }
    std::string dir = args[0];
    std::filesystem::create_directories(dir);
    Attested attested = args.size() == 2 ? read_attested(args[1]) : Attested{};
    ValueGraph graph(c);
    PairCensus census = census_by_grouping(c);
    Vectors vectors(c);
    KeyIndex tuples = hash_equalities(c, kAllSystems, 0);
    unsigned reference = c.corpora() - 1u;
    Context x{c, graph, census, vectors, tuples, attested, 1u << reference};
    const std::string &name = c.stats(reference).name;

    uint32_t reference_types = 0;
    for (uint32_t t = 0; t < c.types(); t++)
        reference_types += (c.in_corpora(t) & x.reference_bit) != 0;
    std::printf("reference text %s: %s word types, %s of them absent from the other corpora\n", name.c_str(),
                commas(reference_types).c_str(), commas(c.stats(reference).new_types).c_str());
    audit_attested(x);

    std::vector<Row> inside;
    for (Match m : matches(c, 1u << kHechrachi, x.reference_bit))
        inside.push_back(x.row(m));
    std::sort(inside.begin(), inside.end(), [](const Row &p, const Row &q) {
        int pp = std::popcount(p.mask), qp = std::popcount(q.mask);
        if (pp != qp)
            return pp > qp;
        if (p.cosine != q.cosine)
            return p.cosine < q.cosine;
        return p.m.a != q.m.a ? p.m.a < q.m.a : p.m.b < q.m.b;
    });
    std::string inside_path = dir + "/matches-reference.tsv";
    write_rows(x, inside, inside_path,
               "pairs of words of " + name + " with different letter multisets agreeing at least in hechrachi");
    std::array<size_t, kSystems + 1> by_systems{};
    for (const Row &r : inside)
        by_systems[static_cast<size_t>(std::popcount(r.mask))]++;
    uint64_t reference_pairs = static_cast<uint64_t>(reference_types) * (reference_types - 1u) / 2u;
    std::printf("%s: %s pairs inside %s out of %s (1 in %.1f; all corpora: 1 in %.1f); agreeing in 4 systems %zu, "
                "3 systems %zu, 2 systems %zu, hechrachi only %zu\n",
                inside_path.c_str(), commas(inside.size()).c_str(), name.c_str(), commas(reference_pairs).c_str(),
                inside.empty() ? 0.0 : static_cast<double>(reference_pairs) / static_cast<double>(inside.size()),
                x.one_in(1u << kHechrachi), by_systems[4], by_systems[3], by_systems[2], by_systems[1]);
    print_top(x, inside, 12);

    std::vector<Row> all4;
    for (Match m : matches(c, kAllSystems, 0))
        all4.push_back(x.row(m));
    std::sort(all4.begin(), all4.end(), [](const Row &p, const Row &q) {
        if (p.in_reference != q.in_reference)
            return p.in_reference > q.in_reference;
        if (p.tuple_types != q.tuple_types)
            return p.tuple_types < q.tuple_types;
        if (p.cosine != q.cosine)
            return p.cosine < q.cosine;
        return p.m.a != q.m.a ? p.m.a < q.m.a : p.m.b < q.m.b;
    });
    std::string all4_path = dir + "/matches-all4.tsv";
    write_rows(x, all4, all4_path, "pairs from all corpora with different letter multisets agreeing in all four systems");
    std::array<size_t, 3> touching{};
    for (const Row &r : all4)
        touching[r.in_reference]++;
    std::printf("%s: %s pairs (census: %s); both words in %s %zu, one word %zu, neither %zu\n", all4_path.c_str(),
                commas(all4.size()).c_str(), commas(x.others_at_least(kAllSystems)).c_str(), name.c_str(),
                touching[2], touching[1], touching[0]);
    print_top(x, all4, 12);
    return all4.size() == x.others_at_least(kAllSystems) ? 0 : 1;
}

}  // namespace kabtool
