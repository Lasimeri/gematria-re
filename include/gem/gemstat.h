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

/* Base rate of the verdict (exploratory, not preregistered): the fraction of
 * corpus word types whose own match share is below alpha under (mask,
 * kolel), i.e. how often the auditor says "rare" for a random A. The
 * auditor's p depends on A alone; B only decides whether the claim holds.
 * Computed once per (mask, kolel) and cached. Returns -1 on failure. */
double gemstat_base_rate(gemstat *g, uint32_t mask, int kolel);

/* Match share of a alone (no partner): fills out like gemstat_audit with
 * holds = 0, and up to cap of the most frequent matching types into hits.
 * Returns the number of hits written, or -1 if a is not Hebrew. */
long gemstat_matches(gemstat *g, const char *a, uint32_t mask, int kolel, struct gemstat_claim *out,
                     struct gemstat_hit *hits, uint32_t cap);

/* Number claim "a = n": holds if a has the value n under a method of mask
 * (within kolel). out is filled like gemstat_audit, except that the share
 * counts the corpus types != a that also reach n: the price of a number
 * claim depends on the number alone. Returns 0, or -1 if a is not Hebrew. */
int gemstat_audit_number(gemstat *g, const char *a, uint64_t n, uint32_t mask, int kolel,
                         struct gemstat_claim *out);

/* The four systems of stage K (exploratory): hechrachi, gadol, katan,
 * siduri, in that order, in a[] and b[] and as bits 0..3 of agree. */
struct gemstat_four {
    uint32_t a[4], b[4];     /* values of a and of b (b: zeros without a partner) */
    uint32_t agree;          /* bit s set: a and b agree in system s */
    int has_b;               /* a partner was given */
    int anagram;             /* a and b have the same letters, finals folded */
    uint64_t pairs_as_much;  /* corpus type pairs with different letters agreeing in at least `agree` */
    uint64_t pairs;          /* corpus type pairs with different letters */
};

/* Four-system values of a, and with b (may be NULL or empty) their agreement
 * and its base rate over the corpus. The census is computed on the first
 * call and kept. Returns 0, or -1 if a or b is not Hebrew or longer than 31
 * letters. (Named get_four: in C++ a function cannot share the name of the
 * struct.) */
int gemstat_get_four(gemstat *g, const char *a, const char *b, struct gemstat_four *out);

#ifdef __cplusplus
}
#endif

#endif
