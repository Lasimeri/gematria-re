/*
 * corpus.c - builder, writer and reader for the "GEMC" v2 corpus file.
 *
 * Contract: arrays grow by doubling; any count that would pass UINT32_MAX
 * fails the call. A word's part lengths must sum to its letter count. The
 * reader checks the magic and that the file length equals header + arrays
 * exactly, so a truncated or padded file is refused.
 */
#include "gem/corpus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char gemc_magic[8] = { 'G', 'E', 'M', 'C', 0, 0, 0, 2 };

void gem_corpus_init(struct gem_corpus *c)
{
    memset(c, 0, sizeof *c);
}

void gem_corpus_free(struct gem_corpus *c)
{
    free(c->books);
    free(c->verses);
    free(c->words);
    free(c->parts);
    free(c->letters);
    free(c->strpool);
    gem_corpus_init(c);
}

static int grow(void **p, uint32_t *cap, uint32_t need, size_t elem)
{
    if (need <= *cap)
        return 0;
    uint64_t ncap = *cap ? *cap : 64u;
    while (ncap < need)
        ncap *= 2u;
    if (ncap > UINT32_MAX)
        ncap = UINT32_MAX;
    void *q = realloc(*p, (size_t)ncap * elem);
    if (!q)
        return -1;
    *p = q;
    *cap = (uint32_t)ncap;
    return 0;
}

int gem_corpus_add_book(struct gem_corpus *c, const char *osis)
{
    if (strlen(osis) >= sizeof c->books[0].osis)
        return -1;
    if (grow((void **)&c->books, &c->cap_books, c->n_books + 1u, sizeof *c->books))
        return -1;
    struct gem_book *b = &c->books[c->n_books++];
    memset(b, 0, sizeof *b);
    strcpy(b->osis, osis);
    b->first_verse = c->n_verses;
    b->first_word = c->n_words;
    b->first_letter = c->n_letters;
    return 0;
}

void gem_corpus_end_book(struct gem_corpus *c)
{
    struct gem_book *b = &c->books[c->n_books - 1u];
    b->n_verses = c->n_verses - b->first_verse;
    b->n_words = c->n_words - b->first_word;
    b->n_letters = c->n_letters - b->first_letter;
}

int gem_corpus_add_verse(struct gem_corpus *c, uint16_t chapter, uint16_t verse)
{
    if (c->n_books == 0 || c->n_verses == UINT32_MAX)
        return -1;
    if (grow((void **)&c->verses, &c->cap_verses, c->n_verses + 1u, sizeof *c->verses))
        return -1;
    struct gem_verse *v = &c->verses[c->n_verses++];
    v->book = (uint16_t)(c->n_books - 1u);
    v->chapter = chapter;
    v->verse = verse;
    v->reserved = 0;
    v->first_word = c->n_words;
    return 0;
}

int gem_corpus_add_letter(struct gem_corpus *c, uint8_t code)
{
    if (c->n_letters == UINT32_MAX)
        return -1;
    if (grow((void **)&c->letters, &c->cap_letters, c->n_letters + 1u, 1))
        return -1;
    c->letters[c->n_letters++] = code;
    return 0;
}

int gem_corpus_add_part(struct gem_corpus *c, uint8_t n_letters)
{
    if (c->n_parts == UINT32_MAX)
        return -1;
    if (grow((void **)&c->parts, &c->cap_parts, c->n_parts + 1u, 1))
        return -1;
    c->parts[c->n_parts++] = n_letters;
    return 0;
}

int gem_corpus_intern(struct gem_corpus *c, const char *s, size_t n, uint32_t *off)
{
    uint64_t need = (uint64_t)c->n_strpool + n + 1u;
    if (need > UINT32_MAX)
        return -1;
    if (grow((void **)&c->strpool, &c->cap_strpool, (uint32_t)need, 1))
        return -1;
    *off = c->n_strpool;
    memcpy(c->strpool + c->n_strpool, s, n);
    c->strpool[c->n_strpool + n] = '\0';
    c->n_strpool = (uint32_t)need;
    return 0;
}

int gem_corpus_add_word(struct gem_corpus *c, uint32_t first_letter, uint32_t n_letters,
                        uint32_t n_parts, uint8_t flags, uint32_t morph, uint32_t lemma)
{
    if (n_letters == 0 || n_letters > UINT8_MAX || n_parts == 0 || n_parts > UINT8_MAX ||
        n_parts > c->n_parts || c->n_verses == 0 || c->n_words == UINT32_MAX)
        return -1;
    uint32_t sum = 0;
    for (uint32_t i = c->n_parts - n_parts; i < c->n_parts; i++)
        sum += c->parts[i];
    if (sum != n_letters)
        return -1;
    if (grow((void **)&c->words, &c->cap_words, c->n_words + 1u, sizeof *c->words))
        return -1;
    struct gem_word *w = &c->words[c->n_words++];
    w->first_letter = first_letter;
    w->verse = c->n_verses - 1u;
    w->morph = morph;
    w->lemma = lemma;
    w->first_part = c->n_parts - n_parts;
    w->n_letters = (uint8_t)n_letters;
    w->flags = flags;
    w->n_parts = (uint8_t)n_parts;
    w->reserved = 0;
    return 0;
}

static int put(FILE *f, const void *p, size_t n)
{
    return n == 0 || fwrite(p, 1, n, f) == n ? 0 : -1;
}

int gem_corpus_write(const struct gem_corpus *c, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    uint32_t counts[6] = { c->n_books, c->n_verses, c->n_words, c->n_parts, c->n_letters, c->n_strpool };
    int rc = put(f, gemc_magic, sizeof gemc_magic) | put(f, counts, sizeof counts) |
             put(f, c->books, (size_t)c->n_books * sizeof *c->books) |
             put(f, c->verses, (size_t)c->n_verses * sizeof *c->verses) |
             put(f, c->words, (size_t)c->n_words * sizeof *c->words) |
             put(f, c->parts, c->n_parts) | put(f, c->letters, c->n_letters) |
             put(f, c->strpool, c->n_strpool);
    if (fclose(f) != 0)
        rc = -1;
    return rc;
}

static void *take(FILE *f, size_t n)
{
    void *p = malloc(n ? n : 1);
    if (p && n && fread(p, 1, n, f) != n) {
        free(p);
        return NULL;
    }
    return p;
}

int gem_corpus_read(struct gem_corpus *c, const char *path)
{
    gem_corpus_init(c);
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    char magic[8];
    uint32_t counts[6];
    int rc = -1;
    if (fread(magic, 1, sizeof magic, f) != sizeof magic || memcmp(magic, gemc_magic, sizeof magic) ||
        fread(counts, 1, sizeof counts, f) != sizeof counts)
        goto out;
    c->n_books = counts[0];
    c->n_verses = counts[1];
    c->n_words = counts[2];
    c->n_parts = counts[3];
    c->n_letters = counts[4];
    c->n_strpool = counts[5];
    c->books = take(f, (size_t)c->n_books * sizeof *c->books);
    c->verses = take(f, (size_t)c->n_verses * sizeof *c->verses);
    c->words = take(f, (size_t)c->n_words * sizeof *c->words);
    c->parts = take(f, c->n_parts);
    c->letters = take(f, c->n_letters);
    c->strpool = take(f, c->n_strpool);
    if (!c->books || !c->verses || !c->words || !c->parts || !c->letters || !c->strpool || fgetc(f) != EOF)
        goto out;
    rc = 0;
out:
    fclose(f);
    if (rc)
        gem_corpus_free(c);
    return rc;
}
