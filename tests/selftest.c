/*
 * selftest - stage G2 gate: every method against cited values.
 *
 * Usage: gem-selftest [CORPUS.gemc]
 *
 * Expected values and their sources (all via Wikipedia "Gematria",
 * https://en.wikipedia.org/wiki/Gematria , fetched 2026-10-30, which
 * quotes or cites the primary text):
 *  - yayin = sod = 70 (Sanhedrin 38a)
 *  - HaSatan = 364 (Yoma 20a, quoted: he 5, shin 300, tet 9, nun 50)
 *  - Eliezer = 318 (Pesikta 70b, Gen 14:14)
 *  - achad bone'eh = 1 + (1+8) + (1+8+4) = 23
 *  - emet = 441, katan mispari 9
 *  - Amalek = safek (Hasidic, value 240)
 *  - Neron Qesar = 666 (Rev 13:18 reading)
 *  - chai = 18
 *  - atbash of Babel = Sheshach (Jer 25:26)
 *  - kidmi alef 1, bet 3, gimel 6; prati bet 4, gimel 9; gadol finals 500-900
 * Properties: atbash, albam, achbi are involutions on the 22 letters;
 * avgad is one 22-cycle. With a corpus: the Torah's standard sum computed
 * by table and by letter histogram must agree, and the attested words are
 * counted in WLC.
 */
#include "gem/corpus.h"
#include "gem/hebrew.h"
#include "gem/methods.h"

#include <stdio.h>
#include <string.h>

static int pass, fail;

static void check(const char *what, unsigned long long got, unsigned long long want)
{
    int ok = got == want;
    pass += ok;
    fail += !ok;
    printf("  %-4s %-44s %12llu  (want %llu)\n", ok ? "ok" : "FAIL", what, got, want);
}

static unsigned long long val(enum gem_method m, const char *text)
{
    uint8_t codes[64];
    unsigned words = 0;
    long n = gem_parse_text(text, codes, sizeof codes, &words);
    return n < 0 ? ~0ull : gem_value(m, codes, (size_t)n);
}

static void cipher_text(enum gem_method m, const char *in, char *out, size_t cap)
{
    uint8_t codes[64];
    unsigned words = 0;
    long n = gem_parse_text(in, codes, sizeof codes, &words);
    size_t o = 0;
    for (long i = 0; i < n && o + 3 < cap; i++)
        o += gem_letter_utf8(gem_base_code(gem_cipher(m, gem_letter_base(codes[i]))), out + o);
    out[o] = '\0';
}

static void folded(const char *in, char *out, size_t cap)
{
    cipher_text(GEM_M_HECHRACHI, in, out, cap);
}

static void cited_values(void)
{
    printf("cited values:\n");
    check("hechrachi yayin (Sanhedrin 38a)", val(GEM_M_HECHRACHI, "יין"), 70);
    check("hechrachi sod (Sanhedrin 38a)", val(GEM_M_HECHRACHI, "סוד"), 70);
    check("hechrachi HaSatan (Yoma 20a)", val(GEM_M_HECHRACHI, "השטן"), 364);
    check("hechrachi Eliezer (Pesikta 70b)", val(GEM_M_HECHRACHI, "אליעזר"), 318);
    check("boneeh achad", val(GEM_M_BONEEH, "אחד"), 23);
    check("hechrachi emet", val(GEM_M_HECHRACHI, "אמת"), 441);
    check("katan-mispari emet", val(GEM_M_KATAN_MISPARI, "אמת"), 9);
    check("hechrachi Amalek", val(GEM_M_HECHRACHI, "עמלק"), 240);
    check("hechrachi safek", val(GEM_M_HECHRACHI, "ספק"), 240);
    check("hechrachi Neron Qesar", val(GEM_M_HECHRACHI, "נרון קסר"), 666);
    check("hechrachi chai", val(GEM_M_HECHRACHI, "חי"), 18);
    check("kidmi alef / bet / gimel", val(GEM_M_KIDMI, "א") * 10000 + val(GEM_M_KIDMI, "ב") * 100 +
          val(GEM_M_KIDMI, "ג"), 10306);
    check("prati bet + gimel", val(GEM_M_PRATI, "בג"), 13);
    check("gadol finals kaf..tsadi", val(GEM_M_GADOL, "ךםןףץ"), 3500);
    check("hechrachi finals = base", val(GEM_M_HECHRACHI, "ךםןףץ"), 280);

    char a[64], b[64];
    cipher_text(GEM_M_ATBASH, "בבל", a, sizeof a);
    folded("ששך", b, sizeof b);
    printf("  %-4s %-44s %12s  (want %s)\n", strcmp(a, b) ? "FAIL" : "ok", "atbash Babel (Jer 25:26)", a, b);
    pass += !strcmp(a, b);
    fail += !!strcmp(a, b);
}

static void cipher_properties(void)
{
    printf("cipher properties over all 22 letters:\n");
    const enum gem_method inv[3] = { GEM_M_ATBASH, GEM_M_ALBAM, GEM_M_ACHBI };
    for (int k = 0; k < 3; k++) {
        unsigned bad = 0;
        for (uint8_t x = 0; x < GEM_BASE_LETTERS; x++)
            bad += gem_cipher(inv[k], gem_cipher(inv[k], x)) != x || gem_cipher(inv[k], x) >= GEM_BASE_LETTERS;
        char what[48];
        snprintf(what, sizeof what, "%s is an involution (bad letters)", gem_method_name(inv[k]));
        check(what, bad, 0);
    }
    uint8_t x = 0;
    unsigned steps = 0;
    do {
        x = gem_cipher(GEM_M_AVGAD, x);
        steps++;
    } while (x != 0 && steps < 100);
    check("avgad cycle length", steps, 22);
}

static void corpus_checks(const char *path)
{
    struct gem_corpus c;
    if (gem_corpus_read(&c, path)) {
        printf("corpus %s: cannot read\n", path);
        fail++;
        return;
    }
    printf("corpus %s:\n", path);
    uint32_t end = c.books[4].first_letter + c.books[4].n_letters;
    const uint32_t *t = gem_letter_table(GEM_M_HECHRACHI);
    unsigned long long by_value = gem_value(GEM_M_HECHRACHI, c.letters, end), by_hist = 0;
    unsigned long long hist[GEM_LETTER_CODES] = { 0 };
    for (uint32_t i = 0; i < end; i++)
        hist[c.letters[i]]++;
    for (unsigned k = 0; k < GEM_LETTER_CODES; k++)
        by_hist += hist[k] * t[k];
    check("Torah standard sum: table == histogram", by_value, by_hist);
    printf("  Torah standard sum = %llu over %u letters\n", by_value, end);

    const char *forms[2] = { "יין", "סוד" };
    for (int f = 0; f < 2; f++) {
        uint8_t q[8];
        unsigned words = 0;
        long n = gem_parse_text(forms[f], q, sizeof q, &words);
        unsigned hits = 0;
        for (uint32_t w = 0; w < c.n_words; w++)
            hits += c.words[w].n_letters == n && memcmp(c.letters + c.words[w].first_letter, q, (size_t)n) == 0;
        printf("  WLC occurrences of %s as a whole word: %u\n", forms[f], hits);
    }
    gem_corpus_free(&c);
}

int main(int argc, char **argv)
{
    cited_values();
    cipher_properties();
    if (argc > 1)
        corpus_checks(argv[1]);
    printf("selftest: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
