/*
 * gem_ingest - build the WLC witness corpus from OSHB morphhb (stage S0).
 *
 * Usage: gem-ingest WLC_DIR OUT.gemc
 *
 * Reads the 39 books by explicit name (wlc/ also holds VerseMap.xml, which
 * is not a book), parses each with the strict OSIS parser, writes the GEMC
 * corpus and prints per-book counts, the Torah comparison and the contract
 * totals. Exit status 0 only with 0 violations, 0 unknown code points and
 * a byte-identical reread of the written corpus.
 *
 * Reference counts are comparisons, not targets: WLC is the Leningrad
 * Codex, the references describe the accepted scroll text.
 *  - per-book Torah words and letters (totals 79,847 / 304,805, Ben Asher)
 *    and per-letter Torah counts (Tanach Yehoash list):
 *    https://www.aishdas.org/toratemet/en_pamphlet9.html
 *  - 304,850 Leningrad letters: third-party, unverified,
 *    https://thegematria.com/words/by-the-numbers
 */
#include "gem/corpus.h"
#include "gem/hebrew.h"
#include "gem/osis.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Printed Tanakh order: Torah, Nevi'im, Ketuvim. */
static const char *const books[39] = {
    "Gen", "Exod", "Lev", "Num", "Deut",
    "Josh", "Judg", "1Sam", "2Sam", "1Kgs", "2Kgs", "Isa", "Jer", "Ezek",
    "Hos", "Joel", "Amos", "Obad", "Jonah", "Mic", "Nah", "Hab", "Zeph", "Hag", "Zech", "Mal",
    "Ps", "Prov", "Job", "Song", "Ruth", "Lam", "Eccl", "Esth", "Dan", "Ezra", "Neh", "1Chr", "2Chr"
};

/* aishdas pamphlet 9: Genesis..Deuteronomy. */
static const unsigned ref_book_words[5] = { 20512, 16723, 11950, 16368, 14294 };
static const unsigned ref_book_letters[5] = { 78064, 63529, 44790, 63530, 54892 };
/* aishdas pamphlet 9 (Tanach Yehoash): alef..tav, finals folded. */
static const unsigned ref_letter_counts[GEM_BASE_LETTERS] = {
    27057, 16344, 2109, 7032, 28052, 30509, 2198, 7187, 1802, 31522, 11960,
    21570, 25078, 14107, 1833, 11244, 4805, 4052, 4694, 18109, 15592, 17949
};
#define REF_TORAH_LETTERS_L3P 304850u

static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    char *buf = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long n = ftell(f);
        if (n >= 0 && fseek(f, 0, SEEK_SET) == 0 && (buf = malloc((size_t)n + 1u)) != NULL) {
            if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
                free(buf);
                buf = NULL;
            } else {
                buf[n] = '\0';
                *len = (size_t)n;
            }
        }
    }
    fclose(f);
    return buf;
}

static void add_stats(struct gem_osis_stats *t, const struct gem_osis_stats *s)
{
    t->w_elements += s->w_elements;
    t->qere_words += s->qere_words;
    t->notes += s->notes;
    t->maqaf += s->maqaf;
    t->sof_pasuq += s->sof_pasuq;
    t->paseq += s->paseq;
    t->pe += s->pe;
    t->samekh += s->samekh;
    t->reversed_nun += s->reversed_nun;
    t->large += s->large;
    t->small += s->small;
    t->suspended += s->suspended;
    t->morpheme_seps += s->morpheme_seps;
    t->empty_parts += s->empty_parts;
    t->morph_part_mismatch += s->morph_part_mismatch;
    t->accents += s->accents;
    t->points += s->points;
    t->joiners += s->joiners;
    t->violations += s->violations;
    t->unknown_total += s->unknown_total;
    for (uint32_t i = 0; i < s->n_unknown_kinds; i++) {
        uint32_t j = 0;
        while (j < t->n_unknown_kinds && t->unknown_cp[j] != s->unknown_cp[i])
            j++;
        if (j == t->n_unknown_kinds) {
            if (j == GEM_OSIS_UNKNOWN)
                continue;
            t->unknown_cp[j] = s->unknown_cp[i];
            t->unknown_count[j] = 0;
            t->n_unknown_kinds++;
        }
        t->unknown_count[j] += s->unknown_count[i];
    }
    for (uint32_t i = 0; i < s->n_msgs && t->n_msgs < GEM_OSIS_MSGS; i++)
        memcpy(t->msgs[t->n_msgs++], s->msgs[i], sizeof t->msgs[0]);
}

static uint32_t merged_words(const struct gem_corpus *c, const struct gem_book *b)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < b->n_words; i++)
        n += !(c->words[b->first_word + i].flags & GEM_W_MAQAF);
    return n;
}

static uint32_t ketiv_words(const struct gem_corpus *c, const struct gem_book *b)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < b->n_words; i++)
        n += (c->words[b->first_word + i].flags & GEM_W_KETIV) != 0;
    return n;
}

static void print_torah(const struct gem_corpus *c)
{
    unsigned sum_w = 0, sum_m = 0, sum_l = 0, ref_w = 0, ref_l = 0;
    printf("Torah vs aishdas (Ben Asher; WLC = Leningrad Codex, comparison only):\n");
    printf("  %-5s %8s %8s %6s %8s %8s %6s\n", "book", "letters", "ref", "delta", "merged", "ref", "delta");
    for (unsigned i = 0; i < 5; i++) {
        const struct gem_book *b = &c->books[i];
        unsigned m = merged_words(c, b);
        printf("  %-5s %8u %8u %+6d %8u %8u %+6d   (<w> %u)\n", b->osis, b->n_letters, ref_book_letters[i],
               (int)b->n_letters - (int)ref_book_letters[i], m, ref_book_words[i],
               (int)m - (int)ref_book_words[i], b->n_words);
        sum_w += b->n_words;
        sum_m += m;
        sum_l += b->n_letters;
        ref_w += ref_book_words[i];
        ref_l += ref_book_letters[i];
    }
    printf("  %-5s %8u %8u %+6d %8u %8u %+6d   (<w> %u)\n", "Torah", sum_l, ref_l, (int)sum_l - (int)ref_l,
           sum_m, ref_w, (int)sum_m - (int)ref_w, sum_w);
    printf("  Torah letters vs %u (Leningrad, third-party, unverified): %+d\n", REF_TORAH_LETTERS_L3P,
           (int)sum_l - (int)REF_TORAH_LETTERS_L3P);

    unsigned got[GEM_BASE_LETTERS] = { 0 }, ref_sum = 0;
    uint32_t end = c->books[4].first_letter + c->books[4].n_letters;
    for (uint32_t i = 0; i < end; i++)
        got[gem_letter_base(c->letters[i])]++;
    printf("  per letter (finals folded), WLC - ref:");
    for (unsigned k = 0; k < GEM_BASE_LETTERS; k++) {
        char u[3] = { 0 };
        gem_letter_utf8(gem_base_code((uint8_t)k), u);
        printf("%s %s %+d", k % 6 ? "," : "\n   ", u, (int)got[k] - (int)ref_letter_counts[k]);
        ref_sum += ref_letter_counts[k];
    }
    printf("\n  (reference letter table sums to %u)\n", ref_sum);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s WLC_DIR OUT.gemc\n", argv[0]);
        return 2;
    }
    struct gem_corpus c;
    gem_corpus_init(&c);
    struct gem_osis_stats total;
    memset(&total, 0, sizeof total);

    printf("%-6s %7s %8s %8s %9s %6s %5s\n", "book", "verses", "<w>", "merged", "letters", "ketiv", "qere");
    for (size_t i = 0; i < 39; i++) {
        char path[4096];
        snprintf(path, sizeof path, "%s/%s.xml", argv[1], books[i]);
        size_t len = 0;
        char *buf = slurp(path, &len);
        if (!buf) {
            fprintf(stderr, "cannot read %s\n", path);
            return 1;
        }
        struct gem_osis_stats st;
        memset(&st, 0, sizeof st);
        if (gem_osis_parse_book(&c, books[i], buf, len, &st)) {
            fprintf(stderr, "out of memory in %s\n", books[i]);
            return 1;
        }
        free(buf);
        const struct gem_book *b = &c.books[c.n_books - 1u];
        printf("%-6s %7u %8u %8u %9u %6u %5llu\n", b->osis, b->n_verses, b->n_words,
               merged_words(&c, b), b->n_letters, ketiv_words(&c, b), (unsigned long long)st.qere_words);
        add_stats(&total, &st);
    }
    printf("TOTAL  %7u %8u %8s %9u\n\n", c.n_verses, c.n_words, "", c.n_letters);
    print_torah(&c);

    printf("\nmarkup: maqaf %llu, sof pasuq %llu, paseq %llu, pe %llu, samekh %llu, reversed nun %llu\n",
           (unsigned long long)total.maqaf, (unsigned long long)total.sof_pasuq,
           (unsigned long long)total.paseq, (unsigned long long)total.pe,
           (unsigned long long)total.samekh, (unsigned long long)total.reversed_nun);
    printf("letters large %llu, small %llu, suspended %llu; notes %llu, qere words %llu\n",
           (unsigned long long)total.large, (unsigned long long)total.small,
           (unsigned long long)total.suspended, (unsigned long long)total.notes,
           (unsigned long long)total.qere_words);
    printf("dropped marks: accents %llu, points %llu, CGJ %llu, morpheme '/' %llu\n",
           (unsigned long long)total.accents, (unsigned long long)total.points,
           (unsigned long long)total.joiners, (unsigned long long)total.morpheme_seps);
    printf("morpheme parts %u: empty %llu, words with morph segments != parts %llu\n", c.n_parts,
           (unsigned long long)total.empty_parts, (unsigned long long)total.morph_part_mismatch);

    printf("\ncontract: violations %llu, unknown code points %llu\n",
           (unsigned long long)total.violations, (unsigned long long)total.unknown_total);
    for (uint32_t i = 0; i < total.n_unknown_kinds; i++)
        printf("  unknown U+%04X x %llu\n", total.unknown_cp[i], (unsigned long long)total.unknown_count[i]);
    for (uint32_t i = 0; i < total.n_msgs; i++)
        printf("  %s\n", total.msgs[i]);

    if (gem_corpus_write(&c, argv[2])) {
        fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    struct gem_corpus check;
    int reread = gem_corpus_read(&check, argv[2]);
    int same = reread == 0 && check.n_letters == c.n_letters && check.n_words == c.n_words &&
               memcmp(check.letters, c.letters, c.n_letters) == 0 &&
               memcmp(check.words, c.words, (size_t)c.n_words * sizeof *c.words) == 0;
    printf("wrote %s: reread %s\n", argv[2], same ? "identical" : "MISMATCH");
    if (reread == 0)
        gem_corpus_free(&check);
    gem_corpus_free(&c);
    return (total.violations || total.unknown_total || !same) ? 1 : 0;
}
