/*
 * gem-export - write a GEMC corpus as plain UTF-8 text, the input format
 * of stage K (gem-kab).
 *
 * Usage: gem-export CORPUS.gemc OUT.txt
 *
 * One verse per line: "<book> <chapter>:<verse>" TAB, then the verse's
 * words (the corpus's <w> units, ketiv, letters only, finals kept)
 * separated by one space. Nothing else is written, so the letter, word and
 * verse counts of the text are those of the corpus.
 */
#include "gem/corpus.h"
#include "gem/hebrew.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: gem-export CORPUS.gemc OUT.txt\n");
        return 2;
    }
    struct gem_corpus c;
    if (gem_corpus_read(&c, argv[1])) {
        fprintf(stderr, "gem-export: cannot read %s\n", argv[1]);
        return 1;
    }
    FILE *out = fopen(argv[2], "wb");
    if (!out) {
        fprintf(stderr, "gem-export: cannot write %s\n", argv[2]);
        gem_corpus_free(&c);
        return 1;
    }
    unsigned long long letters = 0;
    for (uint32_t v = 0; v < c.n_verses; v++) {
        const struct gem_verse *verse = &c.verses[v];
        uint32_t w_end = v + 1 < c.n_verses ? c.verses[v + 1].first_word : c.n_words;
        fprintf(out, "%s %u:%u\t", c.books[verse->book].osis, verse->chapter, verse->verse);
        for (uint32_t w = verse->first_word; w < w_end; w++) {
            const struct gem_word *word = &c.words[w];
            if (w > verse->first_word)
                fputc(' ', out);
            for (uint32_t i = 0; i < word->n_letters; i++) {
                char utf8[2];
                gem_letter_utf8(c.letters[word->first_letter + i], utf8);
                fwrite(utf8, 1, 2, out);
            }
            letters += word->n_letters;
        }
        fputc('\n', out);
    }
    int bad = ferror(out) | fclose(out);
    printf("gem-export: %u verses, %u words, %llu letters -> %s\n", c.n_verses, c.n_words, letters, argv[2]);
    gem_corpus_free(&c);
    return bad ? 1 : 0;
}
