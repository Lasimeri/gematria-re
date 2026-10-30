/*
 * gemstat.h - C ABI of the statistics engine, for the Rust TUI.
 *
 * One handle = one corpus with its all-books loose lexicon (PREREG-001
 * claim auditor scope). Every function catches its C++ exceptions and
 * reports failure by return value; nothing unwinds across this boundary.
 * Text arguments are UTF-8 Hebrew (letters kept, marks dropped).
 */
#ifndef GEM_GEMSTAT_H
#define GEM_GEMSTAT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gemstat gemstat;

#define GEMSTAT_METHODS 16u

struct gemstat_info {
    uint32_t types;          /* word types in the lexicon */
    uint64_t tokens;         /* word tokens in the lexicon */
    int frozen_ok;           /* corpus and preregistrations match their frozen sha256 */
    char frozen[200];        /* one-line frozen status */
};

struct gemstat_claim {
    int holds;               /* B matches A under (mask, kolel) */
    uint32_t matching;       /* bit m set: B matches A under method m */
    uint32_t share_types;    /* corpus types != A matching A */
    uint64_t share_tokens;
    uint32_t other_types;    /* corpus types != A */
    double p_types, p_tokens;
    uint32_t dof;            /* |mask| x |K| */
    int rare;                /* p_types < alpha */
    double alpha;
};

struct gemstat_hit {
    char word[96];           /* UTF-8 */
    uint32_t tokens;
    char ref[32];            /* first occurrence, "Gen 1:1" */
};

/* Returns NULL on failure with a message in err. */
gemstat *gemstat_open(const char *corpus_path, char *err, size_t errlen);
void gemstat_close(gemstat *g);
void gemstat_get_info(const gemstat *g, struct gemstat_info *out);

const char *gemstat_method_name(unsigned m);

/* Values of text under all 16 methods. Returns the letter count (> 0) and
 * the word count, or -1 if text is not Hebrew. */
long gemstat_values(const char *text, uint64_t out[GEMSTAT_METHODS], unsigned *words);

/* Claim auditor. Returns 0, or -1 if a or b is not Hebrew. */
int gemstat_audit(gemstat *g, const char *a, const char *b, uint32_t mask, int kolel, struct gemstat_claim *out);

/* Match share of a alone (no partner): fills out like gemstat_audit with
 * holds = 0, and up to cap of the most frequent matching types into hits.
 * Returns the number of hits written, or -1 if a is not Hebrew. */
long gemstat_matches(gemstat *g, const char *a, uint32_t mask, int kolel, struct gemstat_claim *out,
                     struct gemstat_hit *hits, uint32_t cap);

#ifdef __cplusplus
}
#endif

#endif
