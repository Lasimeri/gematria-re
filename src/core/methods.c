/*
 * methods.c - gematria methods and letter ciphers (layer L1).
 *
 * Definitions: Wikipedia "Gematria", sections "Standard encoding", "Other
 * methods" and "Related transformations", fetched 2026-10-30. The additive
 * methods are one 27-entry table each, built once from the base values;
 * the order-sensitive and whole-word methods are computed directly.
 */
#include "gem/methods.h"
#include "gem/hebrew.h"

#include <pthread.h>

/* Standard (mispar hechrachi) value of base letters alef..tav. */
static const uint32_t base_value[GEM_BASE_LETTERS] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400
};

/* Mispar gadol values of the five finals, by letter code. */
static uint32_t gadol_final(uint8_t code)
{
    switch (code) {
    case 10: return 500;  /* final kaf */
    case 13: return 600;  /* final mem */
    case 15: return 700;  /* final nun */
    case 19: return 800;  /* final pe */
    case 21: return 900;  /* final tsadi */
    default: return 0;
    }
}

static const char *const names[GEM_M_COUNT] = {
    "hechrachi", "gadol", "katan", "siduri", "kidmi", "prati", "meshulash", "boneeh",
    "akhor", "klali", "katan-mispari", "musafi", "atbash", "albam", "achbi", "avgad"
};

const char *gem_method_name(enum gem_method m)
{
    return (unsigned)m < GEM_M_COUNT ? names[m] : "?";
}

uint8_t gem_cipher(enum gem_method m, uint8_t b)
{
    switch (m) {
    case GEM_M_ATBASH:
        return (uint8_t)(21u - b);
    case GEM_M_ALBAM:
        return (uint8_t)(b < 11u ? b + 11u : b - 11u);
    case GEM_M_ACHBI:
        return (uint8_t)(b < 11u ? 10u - b : 32u - b);
    case GEM_M_AVGAD:
        return (uint8_t)(b == 21u ? 0u : b + 1u);
    default:
        return b;
    }
}

static uint32_t tables[GEM_M_COUNT][GEM_LETTER_CODES];
static pthread_once_t tables_once = PTHREAD_ONCE_INIT;

static uint32_t letter_value(enum gem_method m, uint8_t code)
{
    uint8_t b = gem_letter_base(code);
    uint32_t v = base_value[b];
    switch (m) {
    case GEM_M_HECHRACHI:
        return v;
    case GEM_M_GADOL:
        return gem_letter_is_final(code) ? gadol_final(code) : v;
    case GEM_M_KATAN:
        return v >= 100u ? v / 100u : v >= 10u ? v / 10u : v;
    case GEM_M_SIDURI:
        return b + 1u;
    case GEM_M_KIDMI: {
        uint32_t s = 0;
        for (uint8_t i = 0; i <= b; i++)
            s += base_value[i];
        return s;
    }
    case GEM_M_PRATI:
        return v * v;
    case GEM_M_MESHULASH:
        return v * v * v;
    case GEM_M_ATBASH:
    case GEM_M_ALBAM:
    case GEM_M_ACHBI:
    case GEM_M_AVGAD:
        return base_value[gem_cipher(m, b)];
    default:
        return 0;
    }
}

static int additive(enum gem_method m)
{
    switch (m) {
    case GEM_M_HECHRACHI: case GEM_M_GADOL: case GEM_M_KATAN: case GEM_M_SIDURI:
    case GEM_M_KIDMI: case GEM_M_PRATI: case GEM_M_MESHULASH:
    case GEM_M_ATBASH: case GEM_M_ALBAM: case GEM_M_ACHBI: case GEM_M_AVGAD:
        return 1;
    default:
        return 0;
    }
}

static void build_tables(void)
{
    for (unsigned m = 0; m < GEM_M_COUNT; m++)
        if (additive((enum gem_method)m))
            for (uint8_t c = 0; c < GEM_LETTER_CODES; c++)
                tables[m][c] = letter_value((enum gem_method)m, c);
}

const uint32_t *gem_letter_table(enum gem_method m)
{
    pthread_once(&tables_once, build_tables);
    return (unsigned)m < GEM_M_COUNT && additive(m) ? tables[m] : NULL;
}

static uint64_t sum_table(const uint32_t *t, const uint8_t *codes, size_t n)
{
    uint64_t s = 0;
    for (size_t i = 0; i < n; i++)
        s += t[codes[i]];
    return s;
}

uint64_t gem_value(enum gem_method m, const uint8_t *codes, size_t n)
{
    const uint32_t *t = gem_letter_table(m);
    if (t)
        return sum_table(t, codes, n);
    const uint32_t *std = gem_letter_table(GEM_M_HECHRACHI);
    switch (m) {
    case GEM_M_BONEEH: {
        uint64_t prefix = 0, total = 0;
        for (size_t i = 0; i < n; i++) {
            prefix += std[codes[i]];
            total += prefix;
        }
        return total;
    }
    case GEM_M_AKHOR: {
        uint64_t s = 0;
        for (size_t i = 0; i < n; i++)
            s += (uint64_t)std[codes[i]] * (i + 1u);
        return s;
    }
    case GEM_M_KLALI: {
        uint64_t v = sum_table(std, codes, n);
        return v * v;
    }
    case GEM_M_KATAN_MISPARI: {
        uint64_t v = sum_table(std, codes, n);
        return v == 0 ? 0 : 1u + (v - 1u) % 9u;
    }
    case GEM_M_MUSAFI:
        return sum_table(std, codes, n) + n;
    default:
        return 0;
    }
}
