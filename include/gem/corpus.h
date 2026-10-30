/*
 * corpus.h - the normalized witness: books, verses, words, morpheme parts,
 * letters.
 *
 * One corpus = one witness (here WLC 4.20 via OSHB morphhb). Letters are
 * codes 0..26 (code point - U+05D0, finals kept). Every word records its
 * verse, its OSHB morph and lemma strings (offsets into strpool), flags,
 * and the letter length of each morpheme part (OSHB writes the parts
 * separated by '/', in the same order as the morph segments), so prefixes
 * can be stripped exactly.
 *
 * File format "GEMC" v2, native little-endian, written and read only by
 * this module: 8-byte magic, six u32 counts (books, verses, words, parts,
 * letters, strpool bytes), then the six arrays in that order.
 */
#ifndef GEM_CORPUS_H
#define GEM_CORPUS_H

#include <stddef.h>
#include <stdint.h>

#define GEM_W_KETIV     0x01u  /* <w type="x-ketiv">: written form of a ketiv/qere pair */
#define GEM_W_MAQAF     0x02u  /* joined to the next word by a maqaf */
#define GEM_W_LARGE     0x04u  /* contains a large letter (seg x-large) */
#define GEM_W_SMALL     0x08u  /* contains a small letter (seg x-small) */
#define GEM_W_SUSPENDED 0x10u  /* contains a suspended letter (seg x-suspended) */

struct gem_book {
    char osis[8];                 /* OSIS book id, NUL-terminated */
    uint32_t first_verse, n_verses;
    uint32_t first_word, n_words;
    uint32_t first_letter, n_letters;
};

struct gem_verse {
    uint16_t book, chapter, verse, reserved;
    uint32_t first_word;
};

struct gem_word {
    uint32_t first_letter;
    uint32_t verse;
    uint32_t morph, lemma;        /* strpool offsets */
    uint32_t first_part;          /* index into parts[] */
    uint8_t n_letters, flags, n_parts, reserved;
};

struct gem_corpus {
    uint32_t n_books, n_verses, n_words, n_parts, n_letters, n_strpool;
    struct gem_book *books;
    struct gem_verse *verses;
    struct gem_word *words;
    uint8_t *parts;               /* letter length of each morpheme part */
    uint8_t *letters;
    char *strpool;
    /* builder capacities; zero for a corpus loaded from a file */
    uint32_t cap_books, cap_verses, cap_words, cap_parts, cap_letters, cap_strpool;
};

void gem_corpus_init(struct gem_corpus *c);
void gem_corpus_free(struct gem_corpus *c);

/* Builder. All return 0 on success, -1 on allocation failure or overflow. */
int gem_corpus_add_book(struct gem_corpus *c, const char *osis);
int gem_corpus_add_verse(struct gem_corpus *c, uint16_t chapter, uint16_t verse);
int gem_corpus_add_letter(struct gem_corpus *c, uint8_t code);
int gem_corpus_add_part(struct gem_corpus *c, uint8_t n_letters);
int gem_corpus_intern(struct gem_corpus *c, const char *s, size_t n, uint32_t *off);
/* Adds a word whose parts are the last n_parts entries added by
 * gem_corpus_add_part; their lengths must sum to n_letters. */
int gem_corpus_add_word(struct gem_corpus *c, uint32_t first_letter, uint32_t n_letters,
                        uint32_t n_parts, uint8_t flags, uint32_t morph, uint32_t lemma);
/* Close the current book: fills its word/letter ranges. */
void gem_corpus_end_book(struct gem_corpus *c);

int gem_corpus_write(const struct gem_corpus *c, const char *path);
int gem_corpus_read(struct gem_corpus *c, const char *path);

#endif
