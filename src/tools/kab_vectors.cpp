// kab_vectors.cpp - gem-kab vectors / near: the offline float vectors and
// cosine similarity.
//
//   vectors OUT.kvec        build the vectors of every word type, write them,
//                           read the file back and compare
//   near WORD [K] [KVEC]    the K (default 10) types with the largest cosine
//                           to WORD; with KVEC the vectors come from the file,
//                           which must have been built from the same corpora
#include "kab_cmd.hpp"

#include "gem/kab/vectors.hpp"

#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace kabtool {

using namespace gem::kab;

int cmd_vectors(const Corpus &c, Args args)
{
    if (args.size() != 1) {
        std::fprintf(stderr, "usage: vectors OUT.kvec\n");
        return 2;
    }
    auto t0 = std::chrono::steady_clock::now();
    Vectors built(c);
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    double worst = 0;
    for (uint32_t t = 0; t < built.count(); t++) {
        double norm2 = 0;
        for (unsigned d = 0; d < kDim; d++)
            norm2 += static_cast<double>(built.row(t)[d]) * built.row(t)[d];
        worst = std::max(worst, std::fabs(std::sqrt(norm2) - 1.0));
    }
    built.write(args[0]);
    Vectors reread(args[0]);
    bool same = reread.count() == built.count() && reread.lexicon() == built.lexicon() &&
                std::memcmp(reread.data(), built.data(), built.bytes()) == 0;
    std::printf("vectors: %s types x %u floats = %s bytes, built in %.2f ms; largest |norm - 1| = %.2e\n",
                commas(built.count()).c_str(), kDim, commas(built.bytes()).c_str(), seconds * 1e3, worst);
    std::printf("  %s: payload fnv1a %016" PRIx64 ", lexicon fnv1a %016" PRIx64 "; reread %s\n", args[0],
                built.payload_hash(), built.lexicon(), same ? "identical" : "DIFFERS");
    return same ? 0 : 1;
}

int cmd_near(const Corpus &c, Args args)
{
    if (args.empty() || args.size() > 3) {
        std::fprintf(stderr, "usage: near WORD [K] [KVEC]\n");
        return 2;
    }
    std::vector<Word> words = split_words(args[0]);
    if (words.size() != 1 || words[0].letters > kMaxWordLetters) {
        std::fprintf(stderr, "near: \"%s\" is not one Hebrew word of at most %u letters\n", args[0], kMaxWordLetters);
        return 2;
    }
    const Word &w = words[0];
    unsigned k = args.size() >= 2 ? static_cast<unsigned>(std::atoi(args[1])) : 10u;
    Vectors v = args.size() == 3 ? Vectors(std::string(args[2])) : Vectors(c);
    if (v.lexicon() != lexicon_hash(c) || v.count() != c.types())
        throw std::runtime_error("the vector file was built from other corpora");

    alignas(64) float q[kDim];
    unit_vector(w.multiset, w.key, q);
    int64_t self = c.find(w.text);
    std::printf("near %s: hechrachi %u, gadol %u, katan %u, siduri %u; %s\n", w.text.c_str(), lane(w.key, kHechrachi),
                lane(w.key, kGadol), lane(w.key, kKatan), lane(w.key, kSiduri),
                self < 0 ? "not in the corpora" : corpora_of(c, static_cast<uint32_t>(self)).c_str());
    std::printf("  %-8s %-5s %9s %6s %5s %6s  %-8s %s\n", "cosine", "agree", "hechrachi", "gadol", "katan", "siduri",
                "corpora", "word");
    for (const Neighbor &n : nearest(v, q, k + 1)) {
        if (static_cast<int64_t>(n.type) == self)
            continue;
        if (k-- == 0)
            break;
        std::printf("  %.6f %-5s %9d %6d %5d %6d  %-8s %s\n", static_cast<double>(n.cosine),
                    mask_name(agree_swar(w.key, c.key(n.type))).c_str(),
                    c.value(n.type, kHechrachi), c.value(n.type, kGadol), c.value(n.type, kKatan),
                    c.value(n.type, kSiduri), corpora_of(c, n.type).c_str(), c.text(n.type).c_str());
    }
    return 0;
}

}  // namespace kabtool
