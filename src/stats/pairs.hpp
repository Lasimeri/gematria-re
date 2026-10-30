// pairs.hpp - qualifying-pair counter of PREREG-003, shared by cluster.cpp
// and tests/pairs_test.cpp (which checks it against an O(n^2) brute force).
//
// A pair qualifies when the values are equal (kolel 0) or differ by at most
// 1 (kolel 1), the lemmas differ and the letter multisets differ. Counted
// by sorting on (value, lemma, multiset) and inclusion-exclusion inside
// each run of equal value; for kolel 1 the cross pairs of adjacent runs
// (v, v+1) are added minus those sharing a lemma (equal multisets imply
// equal values, so none of those is shared across runs).
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace gem::detail {

struct Item {
    uint64_t v;
    uint32_t lemma, mset;
};

inline uint64_t c2(uint64_t n)
{
    return n < 2 ? 0 : n * (n - 1) / 2;
}

inline uint64_t count_pairs(std::vector<Item> &items, int kolel, std::vector<uint32_t> &scratch)
{
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        if (a.v != b.v)
            return a.v < b.v;
        if (a.lemma != b.lemma)
            return a.lemma < b.lemma;
        return a.mset < b.mset;
    });
    uint64_t total = 0;
    const size_t n = items.size();
    size_t prev_run = 0, prev_end = 0;
    for (size_t run = 0; run < n;) {
        size_t end = run;
        while (end < n && items[end].v == items[run].v)
            end++;
        uint64_t same_lemma = 0, same_both = 0, same_mset = 0;
        for (size_t i = run; i < end;) {
            size_t j = i;
            while (j < end && items[j].lemma == items[i].lemma)
                j++;
            same_lemma += c2(j - i);
            for (size_t k = i; k < j;) {
                size_t l = k;
                while (l < j && items[l].mset == items[k].mset)
                    l++;
                same_both += c2(l - k);
                k = l;
            }
            i = j;
        }
        scratch.clear();
        for (size_t i = run; i < end; i++)
            scratch.push_back(items[i].mset);
        std::sort(scratch.begin(), scratch.end());
        for (size_t i = 0; i < scratch.size();) {
            size_t j = i;
            while (j < scratch.size() && scratch[j] == scratch[i])
                j++;
            same_mset += c2(j - i);
            i = j;
        }
        total += c2(end - run) - same_lemma - same_mset + same_both;
        if (kolel && prev_end > prev_run && items[prev_run].v + 1 == items[run].v) {
            uint64_t cross = static_cast<uint64_t>(prev_end - prev_run) * (end - run);
            size_t a = prev_run, b = run;
            while (a < prev_end && b < end) {
                if (items[a].lemma < items[b].lemma) {
                    a++;
                } else if (items[b].lemma < items[a].lemma) {
                    b++;
                } else {
                    const uint32_t lem = items[a].lemma;
                    uint64_t ca = 0, cb = 0;
                    for (; a < prev_end && items[a].lemma == lem; a++)
                        ca++;
                    for (; b < end && items[b].lemma == lem; b++)
                        cb++;
                    cross -= ca * cb;
                }
            }
            total += cross;
        }
        prev_run = run;
        prev_end = end;
        run = end;
    }
    return total;
}

// Reference: every pair checked directly.
inline uint64_t count_pairs_brute(const std::vector<Item> &items, int kolel)
{
    uint64_t total = 0;
    for (size_t i = 0; i < items.size(); i++)
        for (size_t j = i + 1; j < items.size(); j++) {
            const Item &a = items[i], &b = items[j];
            uint64_t d = a.v > b.v ? a.v - b.v : b.v - a.v;
            total += d <= static_cast<uint64_t>(kolel) && a.lemma != b.lemma && a.mset != b.mset;
        }
    return total;
}

}  // namespace gem::detail
