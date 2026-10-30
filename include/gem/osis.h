/*
 * osis.h - strict parser for OSHB morphhb OSIS book files (WLC text).
 *
 * Contract (ketiv-only witness, see src/ingest/osis.c for the full list):
 * letters are taken from <w> text outside <note>; nothing inside <header>
 * or <note> enters the corpus; every structure outside the contract is
 * counted as a violation and every code point outside the known classes is
 * counted as unknown. A run is valid only with both totals at zero.
 */
#ifndef GEM_OSIS_H
#define GEM_OSIS_H

#include <stddef.h>
#include <stdint.h>

#include "gem/corpus.h"

#define GEM_OSIS_MSGS    16
#define GEM_OSIS_UNKNOWN 32

struct gem_osis_stats {
    uint64_t w_elements;        /* <w> outside notes = words in the corpus */
    uint64_t qere_words;        /* <w> inside <rdg type="x-qere"> (excluded) */
    uint64_t notes;             /* <note> elements (all excluded) */
    uint64_t maqaf, sof_pasuq, paseq, pe, samekh, reversed_nun;
    uint64_t large, small, suspended;
    uint64_t morpheme_seps;     /* '/' inside <w> */
    uint64_t empty_parts;       /* morpheme parts with no letters */
    uint64_t morph_part_mismatch; /* words whose morph segment count != text part count */
    uint64_t accents, points, joiners;
    uint64_t violations;
    uint64_t unknown_total;
    uint32_t unknown_cp[GEM_OSIS_UNKNOWN];
    uint64_t unknown_count[GEM_OSIS_UNKNOWN];
    uint32_t n_unknown_kinds;
    uint32_t n_msgs;
    char msgs[GEM_OSIS_MSGS][160];
};

/* Parse one book file into c (adds the book, its verses, words, letters).
 * Returns 0 if the file was walked to the end (violations are reported in
 * st, not by the return value), -1 on allocation failure. */
int gem_osis_parse_book(struct gem_corpus *c, const char *osis_book,
                        const char *buf, size_t len, struct gem_osis_stats *st);

#endif
