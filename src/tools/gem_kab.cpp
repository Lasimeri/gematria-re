// gem-kab - stage K front end (pure C++): load UTF-8 Hebrew corpora, then
// run one command over them.
//
// Usage: gem-kab NAME=PATH [NAME=PATH ...] COMMAND [ARGS]
//
// Every NAME=PATH before the command is loaded as one corpus, in order
// (format and tokenizer contract: include/gem/kab/corpus.hpp). The load
// report of each corpus is printed first, so every stored result names its
// inputs and their counts.
#include "kab_cmd.hpp"

#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

namespace kabtool {

std::string commas(uint64_t n)
{
    std::string digits = std::to_string(n), out;
    for (size_t i = 0; i < digits.size(); i++) {
        if (i && (digits.size() - i) % 3 == 0)
            out += ',';
        out += digits[i];
    }
    return out;
}

std::string corpora_of(const gem::kab::Corpus &c, uint32_t t)
{
    std::string out;
    for (unsigned k = 0; k < c.corpora(); k++)
        if (c.in_corpora(t) >> k & 1u)
            out += (out.empty() ? "" : "+") + c.stats(k).name;
    return out;
}

std::string mask_name(unsigned mask)
{
    std::string name(gem::kab::kSystems, '-');
    for (unsigned s = 0; s < gem::kab::kSystems; s++)
        if (mask >> s & 1u)
            name[s] = "HGKS"[s];
    return name;
}

namespace {

struct Command {
    const char *name, *help;
    int (*run)(const gem::kab::Corpus &, Args);
};

constexpr Command kCommands[] = {
    {"index", "value indexes of words and verses, per system", cmd_index},
    {"value", "SYSTEM N: the words and verses with that value", cmd_value},
    {"vectors", "OUT.kvec: build and store the offline float vectors", cmd_vectors},
    {"near", "WORD [K] [KVEC]: nearest word types by cosine", cmd_near},
    {"graph", "shared-value graph and the census of pairs by agreement", cmd_graph},
    {"verify", "census by grouping against brute force over every pair", cmd_verify},
    {"pair", "A B: one equality, its agreement mask and its base rate", cmd_pair},
    {"dump", "OUTDIR [ATTESTED.tsv]: matches to read against the last corpus", cmd_dump},
    {"bench", "[ROUNDS]: each kernel against the plain form it replaces", cmd_bench},
};

int usage()
{
    std::fprintf(stderr, "usage: gem-kab NAME=PATH [NAME=PATH ...] COMMAND [ARGS]\n");
    for (const Command &cmd : kCommands)
        std::fprintf(stderr, "  %-8s %s\n", cmd.name, cmd.help);
    return 2;
}

void report(const gem::kab::LoadStats &st)
{
    std::printf("corpus %s (%s): %s bytes, %s verses, %s words, %s letters, %s new types, %.1f ms\n", st.name.c_str(),
                st.path.c_str(), commas(st.bytes).c_str(), commas(st.verses).c_str(), commas(st.tokens).c_str(),
                commas(st.letters).c_str(), commas(st.new_types).c_str(), st.seconds * 1e3);
    std::printf("  dropped marks %s; separators: Hebrew punctuation %s, ASCII %s, foreign %s, malformed %s; "
                "overlong words %s\n",
                commas(st.marks).c_str(), commas(st.punct).c_str(), commas(st.ascii).c_str(),
                commas(st.foreign).c_str(), commas(st.malformed).c_str(), commas(st.overlong).c_str());
}

}  // namespace

}  // namespace kabtool

int main(int argc, char **argv)
{
    using namespace kabtool;
    try {
        gem::kab::Corpus corpus;
        int i = 1;
        for (; i < argc; i++) {
            const char *eq = std::strchr(argv[i], '=');
            if (!eq)
                break;
            report(corpus.load(eq + 1, std::string(argv[i], static_cast<size_t>(eq - argv[i]))));
        }
        if (i >= argc || corpus.corpora() == 0)
            return usage();
        for (const Command &cmd : kCommands)
            if (std::strcmp(argv[i], cmd.name) == 0)
                return cmd.run(corpus, Args(argv + i + 1, static_cast<size_t>(argc - i - 1)));
        return usage();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "gem-kab: %s\n", e.what());
        return 1;
    }
}
