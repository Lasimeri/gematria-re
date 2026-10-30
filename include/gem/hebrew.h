/*
 * hebrew.h - strict UTF-8 decoding and Hebrew code point classes.
 *
 * Letters are stored as codes 0..26 = code point - U+05D0, so final forms
 * keep their identity (gadol needs them) and fold to the 22 base letters
 * through gem_letter_base().
 *
 * Source for the classes: Unicode 17.0 Hebrew block chart,
 * https://unicode.org/charts/PDF/U0590.pdf
 */
#ifndef GEM_HEBREW_H
#define GEM_HEBREW_H

#include <stddef.h>
#include <stdint.h>

#define GEM_LETTER_FIRST 0x05D0u
#define GEM_LETTER_LAST  0x05EAu
#define GEM_LETTER_CODES 27u   /* 22 base letters + 5 final forms */
#define GEM_BASE_LETTERS 22u

enum gem_cp_class {
    GEM_CP_OTHER = 0,   /* outside every class below: a contract failure */
    GEM_CP_LETTER,      /* U+05D0..U+05EA */
    GEM_CP_ACCENT,      /* cantillation U+0591..U+05AF */
    GEM_CP_POINT,       /* vowel and other points: U+05B0..U+05BD, U+05BF,
                           U+05C1, U+05C2, U+05C4, U+05C5, U+05C7 */
    GEM_CP_PUNCT,       /* maqaf U+05BE, paseq U+05C0, sof pasuq U+05C3,
                           nun hafukha U+05C6 */
    GEM_CP_JOINER,      /* combining grapheme joiner U+034F */
    GEM_CP_SPACE        /* U+0009, U+000A, U+000D, U+0020 */
};

/* Decode one scalar value from s[0..n). Returns the number of bytes used
 * (1..4) or 0 for any malformed, overlong, surrogate or out-of-range
 * sequence. */
size_t gem_utf8_next(const unsigned char *s, size_t n, uint32_t *cp);

enum gem_cp_class gem_cp_classify(uint32_t cp);

static inline int gem_cp_is_letter(uint32_t cp)
{
    return cp >= GEM_LETTER_FIRST && cp <= GEM_LETTER_LAST;
}

/* code 0..26 -> base letter 0..21 (alef = 0 ... tav = 21). */
uint8_t gem_letter_base(uint8_t code);

/* base letter 0..21 -> code of its non-final form. */
uint8_t gem_base_code(uint8_t base);

/* 1 for the five final forms (kaf, mem, nun, pe, tsadi sofit). */
int gem_letter_is_final(uint8_t code);

/* code 0..26 -> UTF-8 bytes (always 2); returns 2. */
size_t gem_letter_utf8(uint8_t code, char out[2]);

/* Parse Hebrew text into letter codes. Letters are kept, accents, points
 * and U+034F dropped; spaces, maqaf and sof pasuq separate words. Returns
 * the number of letters (0..cap) and stores the word count, or -1 for a
 * malformed or non-Hebrew character, or more than cap letters. */
long gem_parse_text(const char *utf8, uint8_t *codes, size_t cap, unsigned *words);

#endif
