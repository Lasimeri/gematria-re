/*
 * methods.h - gematria methods and letter ciphers (layer L1).
 *
 * Every method is defined exactly as listed in Wikipedia "Gematria",
 * section "Other methods" and "Related transformations" (methods compiled
 * by Moshe Cordovero), https://en.wikipedia.org/wiki/Gematria , fetched
 * 2026-10-30. Inputs are letter codes 0..26 (code point - U+05D0, finals
 * kept); only GEM_M_GADOL gives the final forms their own values, every
 * other method folds them to the base letter.
 *
 * Deferred (each needs a cited spelling table and adds degrees of freedom):
 * otiyot be-milui, mispar ne'elam, mispar mispari.
 */
#ifndef GEM_METHODS_H
#define GEM_METHODS_H

#include <stddef.h>
#include <stdint.h>

enum gem_method {
    GEM_M_HECHRACHI = 0,  /* standard: 1-9, 10-90, 100-400 */
    GEM_M_GADOL,          /* finals 500-900 */
    GEM_M_KATAN,          /* standard value with zeros truncated: 1-9, 1-9, 1-4 */
    GEM_M_SIDURI,         /* ordinal 1-22 */
    GEM_M_KIDMI,          /* letter = sum of standard values up to and including it */
    GEM_M_PRATI,          /* letter = square of its standard value */
    GEM_M_MESHULASH,      /* letter = cube of its standard value */
    GEM_M_BONEEH,         /* running total of prefix sums: achad = 1+(1+8)+(1+8+4) = 23 */
    GEM_M_AKHOR,          /* standard value x position (ascending, from 1) */
    GEM_M_KLALI,          /* square of the word's standard value */
    GEM_M_KATAN_MISPARI,  /* digit root of the standard value */
    GEM_M_MUSAFI,         /* standard value + number of letters */
    GEM_M_ATBASH,         /* standard value after atbash (alef<->tav, bet<->shin, ...) */
    GEM_M_ALBAM,          /* standard value after albam (alef<->lamed, bet<->mem, ...) */
    GEM_M_ACHBI,          /* standard value after achbi (two halves of 11, each reversed) */
    GEM_M_AVGAD,          /* standard value after avgad (each letter -> next, tav -> alef) */
    GEM_M_COUNT
};

/* Order-sensitive methods: the value depends on letter order, not only on
 * the letter multiset. */
#define GEM_M_ORDER_SENSITIVE(m) ((m) == GEM_M_BONEEH || (m) == GEM_M_AKHOR)

const char *gem_method_name(enum gem_method m);

/* Value of one word (codes[0..n)). */
uint64_t gem_value(enum gem_method m, const uint8_t *codes, size_t n);

/* Per-letter value table of the additive methods (HECHRACHI, GADOL, KATAN,
 * SIDURI, KIDMI, PRATI, MESHULASH, ATBASH, ALBAM, ACHBI, AVGAD); NULL for
 * the others. Indexed by letter code 0..26. */
const uint32_t *gem_letter_table(enum gem_method m);

/* Base-letter cipher maps (0..21 -> 0..21) for atbash, albam, achbi, avgad. */
uint8_t gem_cipher(enum gem_method m, uint8_t base);

#endif
