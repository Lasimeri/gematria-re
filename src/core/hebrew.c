/*
 * hebrew.c - strict UTF-8 decoding and Hebrew code point classes.
 *
 * Contract: gem_utf8_next() accepts only shortest-form UTF-8 scalar values
 * (no surrogates, nothing above U+10FFFF); every other byte sequence is an
 * error, which the corpus parser turns into a hard failure.
 *
 * Classes follow the Unicode 17.0 Hebrew block chart
 * (https://unicode.org/charts/PDF/U0590.pdf): accents U+0591-U+05AF, points
 * U+05B0-U+05BD, maqaf U+05BE, rafe U+05BF, paseq U+05C0, shin and sin dots
 * U+05C1/U+05C2, sof pasuq U+05C3, puncta extraordinaria U+05C4/U+05C5,
 * nun hafukha U+05C6, qamats qatan U+05C7, letters U+05D0-U+05EA.
 */
#include "gem/hebrew.h"

size_t gem_utf8_next(const unsigned char *s, size_t n, uint32_t *cp)
{
    if (n == 0)
        return 0;
    unsigned char b0 = s[0];
    if (b0 < 0x80u) {
        *cp = b0;
        return 1;
    }
    size_t len;
    uint32_t v, min;
    if ((b0 & 0xE0u) == 0xC0u) {
        len = 2; v = b0 & 0x1Fu; min = 0x80u;
    } else if ((b0 & 0xF0u) == 0xE0u) {
        len = 3; v = b0 & 0x0Fu; min = 0x800u;
    } else if ((b0 & 0xF8u) == 0xF0u) {
        len = 4; v = b0 & 0x07u; min = 0x10000u;
    } else {
        return 0;
    }
    if (n < len)
        return 0;
    for (size_t i = 1; i < len; i++) {
        if ((s[i] & 0xC0u) != 0x80u)
            return 0;
        v = (v << 6) | (s[i] & 0x3Fu);
    }
    if (v < min || v > 0x10FFFFu || (v >= 0xD800u && v <= 0xDFFFu))
        return 0;
    *cp = v;
    return len;
}

enum gem_cp_class gem_cp_classify(uint32_t cp)
{
    if (cp >= GEM_LETTER_FIRST && cp <= GEM_LETTER_LAST)
        return GEM_CP_LETTER;
    if (cp >= 0x0591u && cp <= 0x05AFu)
        return GEM_CP_ACCENT;
    if ((cp >= 0x05B0u && cp <= 0x05BDu) || cp == 0x05BFu || cp == 0x05C1u ||
        cp == 0x05C2u || cp == 0x05C4u || cp == 0x05C5u || cp == 0x05C7u)
        return GEM_CP_POINT;
    if (cp == 0x05BEu || cp == 0x05C0u || cp == 0x05C3u || cp == 0x05C6u)
        return GEM_CP_PUNCT;
    if (cp == 0x034Fu)
        return GEM_CP_JOINER;
    if (cp == 0x20u || cp == 0x0Au || cp == 0x0Du || cp == 0x09u)
        return GEM_CP_SPACE;
    return GEM_CP_OTHER;
}

/* U+05D0.. order: alef bet gimel dalet he vav zayin het tet yod, final kaf,
 * kaf, lamed, final mem, mem, final nun, nun, samekh, ayin, final pe, pe,
 * final tsadi, tsadi, qof, resh, shin, tav. */
static const uint8_t base_of_code[GEM_LETTER_CODES] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    10, 10, 11, 12, 12, 13, 13, 14, 15, 16, 16, 17, 17, 18, 19, 20, 21
};

uint8_t gem_letter_base(uint8_t code)
{
    return code < GEM_LETTER_CODES ? base_of_code[code] : 0xFFu;
}

static const uint8_t code_of_base[GEM_BASE_LETTERS] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 14, 16, 17, 18, 20, 22, 23, 24, 25, 26
};

uint8_t gem_base_code(uint8_t base)
{
    return base < GEM_BASE_LETTERS ? code_of_base[base] : 0xFFu;
}

int gem_letter_is_final(uint8_t code)
{
    return code == 10 || code == 13 || code == 15 || code == 19 || code == 21;
}

long gem_parse_text(const char *utf8, uint8_t *codes, size_t cap, unsigned *words)
{
    const unsigned char *s = (const unsigned char *)utf8;
    size_t len = 0;
    while (s[len])
        len++;
    size_t i = 0, n = 0;
    unsigned w = 0;
    int in_word = 0;
    while (i < len) {
        uint32_t cp;
        size_t k = gem_utf8_next(s + i, len - i, &cp);
        if (k == 0)
            return -1;
        i += k;
        switch (gem_cp_classify(cp)) {
        case GEM_CP_LETTER:
            if (n == cap)
                return -1;
            codes[n++] = (uint8_t)(cp - GEM_LETTER_FIRST);
            if (!in_word)
                w++;
            in_word = 1;
            break;
        case GEM_CP_ACCENT:
        case GEM_CP_POINT:
        case GEM_CP_JOINER:
            break;
        case GEM_CP_SPACE:
            in_word = 0;
            break;
        case GEM_CP_PUNCT:
            if (cp != 0x05BEu && cp != 0x05C3u)
                return -1;
            in_word = 0;
            break;
        case GEM_CP_OTHER:
            return -1;
        }
    }
    *words = w;
    return (long)n;
}

size_t gem_letter_utf8(uint8_t code, char out[2])
{
    uint32_t cp = GEM_LETTER_FIRST + code;
    out[0] = (char)(0xC0u | (cp >> 6));
    out[1] = (char)(0x80u | (cp & 0x3Fu));
    return 2;
}
