// kab_index.cpp - gem-kab index / value: the unordered_map<int,
// vector<string>> value indexes over words and verses.
//
//   index                 per system: distinct values, largest bucket, pair
//                         collision rate (words), shared sums (verses)
//   value SYSTEM N        the word types and verses whose value is N
#include "kab_cmd.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace kabtool {

using namespace gem::kab;

namespace {

struct IndexShape {
    size_t values = 0, entries = 0, largest = 0;
    int largest_value = 0, max_value = 0;
    uint64_t pairs = 0;  // unordered pairs of entries sharing a value
};

IndexShape shape_of(const ValueIndex &index)
{
    IndexShape s;
    s.values = index.size();
    for (const auto &[value, members] : index) {
        size_t n = members.size();
        s.entries += n;
        s.pairs += static_cast<uint64_t>(n) * (n - 1) / 2;
        s.max_value = std::max(s.max_value, value);
        if (n > s.largest || (n == s.largest && value < s.largest_value)) {
            s.largest = n;
            s.largest_value = value;
        }
    }
    return s;
}

void print_shape(const char *system, const IndexShape &s)
{
    double all_pairs = static_cast<double>(s.entries) * static_cast<double>(s.entries - 1) / 2.0;
    std::printf("  %-9s  %6zu values (max %7d)  largest bucket %5zu at %-6d  %14s equal pairs = 1 in %.1f\n", system,
                s.values, s.max_value, s.largest, s.largest_value, commas(s.pairs).c_str(),
                s.pairs ? all_pairs / static_cast<double>(s.pairs) : 0.0);
}

int system_by_name(const char *name)
{
    for (unsigned s = 0; s < kSystems; s++)
        if (std::strcmp(name, kSystemName[s]) == 0)
            return static_cast<int>(s);
    return -1;
}

}  // namespace

int cmd_index(const Corpus &c, Args)
{
    std::printf("word index, %s types (unordered_map<int, vector<string>> per system):\n", commas(c.types()).c_str());
    for (unsigned s = 0; s < kSystems; s++)
        print_shape(kSystemName[s], shape_of(c.word_index(static_cast<System>(s))));
    std::printf("verse index, %s verses:\n", commas(c.verses().size()).c_str());
    for (unsigned s = 0; s < kSystems; s++)
        print_shape(kSystemName[s], shape_of(c.verse_index(static_cast<System>(s))));
    return 0;
}

int cmd_value(const Corpus &c, Args args)
{
    int s = args.size() == 2 ? system_by_name(args[0]) : -1;
    if (s < 0) {
        std::fprintf(stderr, "usage: value hechrachi|gadol|katan|siduri N\n");
        return 2;
    }
    int n = std::atoi(args[1]);
    ValueIndex words = c.word_index(static_cast<System>(s)), verses = c.verse_index(static_cast<System>(s));
    auto show = [&](const char *what, const ValueIndex &index) {
        auto it = index.find(n);
        size_t count = it == index.end() ? 0 : it->second.size();
        std::printf("%s with %s value %d: %zu\n", what, kSystemName[static_cast<unsigned>(s)], n, count);
        for (size_t i = 0; i < count; i++)
            std::printf("%s%s", it->second[i].c_str(), i + 1 == count ? "\n" : (i % 12 == 11 ? "\n" : "  "));
    };
    show("word types", words);
    show("verses", verses);
    return 0;
}

}  // namespace kabtool
