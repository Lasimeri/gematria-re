/*
 * osis.c - strict streaming parser for OSHB morphhb OSIS book files.
 *
 * Source format: https://github.com/openscriptures/morphhb (the wlc book files),
 * pinned at commit 3d15126fb1ef74867fc1434be1942e837932691f, WLC 4.20.
 *
 * Contract (ketiv-only, the letters a scroll writes):
 *  - <header> content is skipped whole (it holds the only entities).
 *  - <note> content is skipped whole: qere readings (<rdg type="x-qere">),
 *    catch-words, alternative accents and exegesis readings.
 *  - Corpus letters come from the text of <w> elements outside notes,
 *    including the nested <seg type="x-large|x-small|x-suspended"> letters.
 *    '/' inside <w> is a morpheme separator. Accents, points and U+034F
 *    are counted and dropped.
 *  - Outside <w>, only these <seg> types may hold text, and none of it is
 *    corpus text: x-maqqef (U+05BE, joins the neighbouring words),
 *    x-sof-pasuq (U+05C3), x-paseq (U+05C0), x-reversednun (U+05C6) and
 *    the paragraph markers x-pe ("פ") and x-samekh ("ס").
 *  - Everything else is a violation: an entity or Hebrew text outside the
 *    places above, any other <seg> type, nested <w> or <seg>, an empty
 *    <w>, a <w> outside a <verse>, an osisID of another book, malformed
 *    UTF-8, any element not in the known set.
 */
#include "gem/osis.h"
#include "gem/hebrew.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum seg_kind {
    SEG_NONE = 0, SEG_MAQQEF, SEG_SOF_PASUQ, SEG_PASEQ, SEG_REVNUN, SEG_PE, SEG_SAMEKH,
    SEG_LARGE, SEG_SMALL, SEG_SUSPENDED, SEG_OTHER
};

struct seg_type_name {
    const char *name;
    enum seg_kind kind;
    uint32_t glyph;     /* the one code point a non-text seg must hold; 0 = letters */
};

static const struct seg_type_name seg_types[] = {
    { "x-maqqef", SEG_MAQQEF, 0x05BEu },
    { "x-sof-pasuq", SEG_SOF_PASUQ, 0x05C3u },
    { "x-paseq", SEG_PASEQ, 0x05C0u },
    { "x-reversednun", SEG_REVNUN, 0x05C6u },
    { "x-pe", SEG_PE, 0x05E4u },
    { "x-samekh", SEG_SAMEKH, 0x05E1u },
    { "x-large", SEG_LARGE, 0 },
    { "x-small", SEG_SMALL, 0 },
    { "x-suspended", SEG_SUSPENDED, 0 },
};

struct parser {
    struct gem_corpus *c;
    struct gem_osis_stats *st;
    const char *book;
    const char *buf;
    size_t len, pos;
    int header_depth, note_depth, in_verse, in_w, in_qere;
    enum seg_kind seg;
    uint32_t seg_glyph, seg_cps;
    /* word under construction */
    uint32_t w_first, w_morph, w_lemma;
    uint32_t w_part_start, w_parts, w_morph_segs;
    uint8_t w_flags;
    int have_word;              /* a word exists that a maqaf may join */
    int oom;
};

static void violation(struct parser *p, const char *fmt, ...)
{
    struct gem_osis_stats *st = p->st;
    st->violations++;
    if (st->n_msgs >= GEM_OSIS_MSGS)
        return;
    char tmp[120];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    snprintf(st->msgs[st->n_msgs++], sizeof st->msgs[0], "%s @%zu: %s", p->book, p->pos, tmp);
}

static void unknown_cp(struct parser *p, uint32_t cp)
{
    struct gem_osis_stats *st = p->st;
    st->unknown_total++;
    for (uint32_t i = 0; i < st->n_unknown_kinds; i++) {
        if (st->unknown_cp[i] == cp) {
            st->unknown_count[i]++;
            return;
        }
    }
    if (st->n_unknown_kinds < GEM_OSIS_UNKNOWN) {
        st->unknown_cp[st->n_unknown_kinds] = cp;
        st->unknown_count[st->n_unknown_kinds++] = 1;
    }
}

/* Find attribute `name` in the tag text [s, e); returns its value span. */
static int attr(const char *s, const char *e, const char *name, const char **v, size_t *vn)
{
    size_t nl = strlen(name);
    for (const char *q = s; q + nl + 2 < e; q++) {
        if ((q == s || q[-1] == ' ' || q[-1] == '\t' || q[-1] == '\n') &&
            memcmp(q, name, nl) == 0 && q[nl] == '=' && q[nl + 1] == '"') {
            const char *a = q + nl + 2;
            const char *b = memchr(a, '"', (size_t)(e - a));
            if (!b)
                return 0;
            *v = a;
            *vn = (size_t)(b - a);
            return 1;
        }
    }
    return 0;
}

static int span_eq(const char *v, size_t vn, const char *lit)
{
    return strlen(lit) == vn && memcmp(v, lit, vn) == 0;
}

static void open_verse(struct parser *p, const char *as, const char *ae)
{
    const char *v;
    size_t vn;
    size_t bl = strlen(p->book);
    if (!attr(as, ae, "osisID", &v, &vn) || vn <= bl + 1 || memcmp(v, p->book, bl) != 0 || v[bl] != '.') {
        violation(p, "verse without osisID of this book");
        return;
    }
    unsigned ch = 0, vs = 0;
    char tmp[32];
    size_t n = vn < sizeof tmp - 1 ? vn : sizeof tmp - 1;
    memcpy(tmp, v, n);
    tmp[n] = '\0';
    if (sscanf(tmp + bl + 1, "%u.%u", &ch, &vs) != 2 || ch == 0 || vs == 0 || ch > 0xFFFFu || vs > 0xFFFFu) {
        violation(p, "bad osisID %s", tmp);
        return;
    }
    if (gem_corpus_add_verse(p->c, (uint16_t)ch, (uint16_t)vs))
        p->oom = 1;
    p->in_verse = 1;
}

static void open_w(struct parser *p, const char *as, const char *ae)
{
    if (p->in_w) {
        violation(p, "nested <w>");
        return;
    }
    if (!p->in_verse)
        violation(p, "<w> outside <verse>");
    const char *v;
    size_t vn;
    p->in_w = 1;
    p->w_first = p->w_part_start = p->c->n_letters;
    p->w_parts = 0;
    p->w_morph_segs = 0;
    p->w_flags = 0;
    p->w_morph = p->w_lemma = 0;
    if (attr(as, ae, "type", &v, &vn)) {
        if (span_eq(v, vn, "x-ketiv"))
            p->w_flags |= GEM_W_KETIV;
        else
            violation(p, "<w type=%.*s>", (int)vn, v);
    }
    if (attr(as, ae, "morph", &v, &vn)) {
        p->w_morph_segs = 1;
        for (size_t i = 0; i < vn; i++)
            p->w_morph_segs += v[i] == '/';
        if (gem_corpus_intern(p->c, v, vn, &p->w_morph))
            p->oom = 1;
    }
    if (attr(as, ae, "lemma", &v, &vn) && gem_corpus_intern(p->c, v, vn, &p->w_lemma))
        p->oom = 1;
}

/* Close the current morpheme part of the open word (at '/' or </w>). */
static void end_part(struct parser *p)
{
    uint32_t n = p->c->n_letters - p->w_part_start;
    if (n == 0)
        p->st->empty_parts++;
    if (n > UINT8_MAX) {
        violation(p, "morpheme part of %u letters", n);
        n = UINT8_MAX;
    }
    if (gem_corpus_add_part(p->c, (uint8_t)n))
        p->oom = 1;
    p->w_parts++;
    p->w_part_start = p->c->n_letters;
}

static void close_w(struct parser *p)
{
    if (!p->in_w)
        return;
    p->in_w = 0;
    end_part(p);
    if (p->w_morph_segs != p->w_parts)
        p->st->morph_part_mismatch++;
    uint32_t n = p->c->n_letters - p->w_first;
    if (n == 0) {
        violation(p, "empty <w>");
        return;
    }
    if (gem_corpus_add_word(p->c, p->w_first, n, p->w_parts, p->w_flags, p->w_morph, p->w_lemma)) {
        violation(p, "word rejected (%u letters, %u parts)", n, p->w_parts);
        return;
    }
    p->st->w_elements++;
    p->have_word = 1;
}

static void open_seg(struct parser *p, const char *as, const char *ae)
{
    if (p->seg != SEG_NONE) {
        violation(p, "nested <seg>");
        return;
    }
    const char *v;
    size_t vn;
    p->seg = SEG_OTHER;
    p->seg_glyph = 0;
    p->seg_cps = 0;
    if (attr(as, ae, "type", &v, &vn)) {
        for (size_t i = 0; i < sizeof seg_types / sizeof seg_types[0]; i++) {
            if (span_eq(v, vn, seg_types[i].name)) {
                p->seg = seg_types[i].kind;
                p->seg_glyph = seg_types[i].glyph;
            }
        }
    }
    int letter_seg = p->seg == SEG_LARGE || p->seg == SEG_SMALL || p->seg == SEG_SUSPENDED;
    if (p->seg == SEG_OTHER || letter_seg != p->in_w) {
        violation(p, "<seg type=%.*s> %s <w>", (int)(attr(as, ae, "type", &v, &vn) ? vn : 0),
                  attr(as, ae, "type", &v, &vn) ? v : "", p->in_w ? "inside" : "outside");
        return;
    }
    struct gem_osis_stats *st = p->st;
    switch (p->seg) {
    case SEG_MAQQEF:
        st->maqaf++;
        if (p->have_word)
            p->c->words[p->c->n_words - 1u].flags |= GEM_W_MAQAF;
        else
            violation(p, "maqaf before any word");
        break;
    case SEG_SOF_PASUQ: st->sof_pasuq++; break;
    case SEG_PASEQ: st->paseq++; break;
    case SEG_REVNUN: st->reversed_nun++; break;
    case SEG_PE: st->pe++; break;
    case SEG_SAMEKH: st->samekh++; break;
    case SEG_LARGE: st->large++; p->w_flags |= GEM_W_LARGE; break;
    case SEG_SMALL: st->small++; p->w_flags |= GEM_W_SMALL; break;
    case SEG_SUSPENDED: st->suspended++; p->w_flags |= GEM_W_SUSPENDED; break;
    case SEG_NONE:
    case SEG_OTHER:
        break;
    }
}

static void close_seg(struct parser *p)
{
    if (p->seg == SEG_NONE)
        return;
    if (p->seg_glyph && p->seg_cps != 1)
        violation(p, "non-text seg holds %u code points", p->seg_cps);
    p->seg = SEG_NONE;
}

static void text(struct parser *p, const char *s, size_t n)
{
    if (p->header_depth || p->note_depth)
        return;
    struct gem_osis_stats *st = p->st;
    const unsigned char *u = (const unsigned char *)s;
    size_t i = 0;
    while (i < n) {
        uint32_t cp;
        size_t k = gem_utf8_next(u + i, n - i, &cp);
        if (k == 0) {
            violation(p, "malformed UTF-8");
            return;
        }
        i += k;
        enum gem_cp_class cls = gem_cp_classify(cp);
        if (cp == '&') {
            violation(p, "entity outside header");
            continue;
        }
        if (p->in_w) {
            switch (cls) {
            case GEM_CP_LETTER:
                if (gem_corpus_add_letter(p->c, (uint8_t)(cp - GEM_LETTER_FIRST)))
                    p->oom = 1;
                break;
            case GEM_CP_ACCENT: st->accents++; break;
            case GEM_CP_POINT: st->points++; break;
            case GEM_CP_JOINER: st->joiners++; break;
            case GEM_CP_SPACE: violation(p, "whitespace inside <w>"); break;
            case GEM_CP_PUNCT: violation(p, "punctuation U+%04X inside <w>", cp); break;
            case GEM_CP_OTHER:
                if (cp == '/') {
                    st->morpheme_seps++;
                    end_part(p);
                } else {
                    unknown_cp(p, cp);
                }
                break;
            }
            continue;
        }
        if (cls == GEM_CP_SPACE)
            continue;
        if (p->seg != SEG_NONE && p->seg_glyph) {
            p->seg_cps++;
            if (cp != p->seg_glyph)
                violation(p, "seg holds U+%04X, expected U+%04X", cp, p->seg_glyph);
            continue;
        }
        if (cls == GEM_CP_OTHER)
            unknown_cp(p, cp);
        else
            violation(p, "text U+%04X outside <w> and non-text segs", cp);
    }
}

/* Known elements; anything else in the body is a violation. */
static int known_element(const char *n, size_t nl)
{
    static const char *const names[] = {
        "osis", "osisText", "header", "div", "chapter", "verse", "w", "seg", "note", "rdg", "catchWord"
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (span_eq(n, nl, names[i]))
            return 1;
    return 0;
}

static void tag(struct parser *p, const char *s, const char *e)
{
    /* s points after '<', e at '>' */
    int closing = 0, selfclose = 0;
    if (s < e && *s == '/') {
        closing = 1;
        s++;
    }
    if (e > s && e[-1] == '/')
        selfclose = 1;
    const char *n = s;
    while (s < e && *s != ' ' && *s != '\t' && *s != '\n' && *s != '/')
        s++;
    size_t nl = (size_t)(s - n);
    const char *as = s, *ae = selfclose ? e - 1 : e;

    if (span_eq(n, nl, "header")) {
        if (!selfclose)
            p->header_depth += closing ? -1 : 1;
        return;
    }
    if (p->header_depth)
        return;
    if (span_eq(n, nl, "note")) {
        if (!closing)
            p->st->notes++;
        if (!selfclose)
            p->note_depth += closing ? -1 : 1;
        if (p->note_depth < 0) {
            violation(p, "unbalanced </note>");
            p->note_depth = 0;
        }
        return;
    }
    if (p->note_depth) {
        /* Inside a note nothing enters the corpus; only the qere words
         * (<w> inside <rdg type="x-qere">) are counted for the report. */
        const char *v;
        size_t vn;
        if (span_eq(n, nl, "rdg"))
            p->in_qere = !closing && attr(as, ae, "type", &v, &vn) && span_eq(v, vn, "x-qere");
        else if (span_eq(n, nl, "w") && !closing && p->in_qere)
            p->st->qere_words++;
        return;
    }
    if (!known_element(n, nl)) {
        violation(p, "unknown element <%.*s>", (int)nl, n);
        return;
    }
    if (span_eq(n, nl, "rdg") || span_eq(n, nl, "catchWord")) {
        violation(p, "<%.*s> outside <note>", (int)nl, n);
        return;
    }
    if (span_eq(n, nl, "verse")) {
        if (closing)
            p->in_verse = 0;
        else
            open_verse(p, as, ae);
    } else if (span_eq(n, nl, "w")) {
        if (closing)
            close_w(p);
        else {
            open_w(p, as, ae);
            if (selfclose)
                close_w(p);
        }
    } else if (span_eq(n, nl, "seg")) {
        if (closing)
            close_seg(p);
        else {
            open_seg(p, as, ae);
            if (selfclose)
                close_seg(p);
        }
    }
}

int gem_osis_parse_book(struct gem_corpus *c, const char *osis_book,
                        const char *buf, size_t len, struct gem_osis_stats *st)
{
    struct parser p;
    memset(&p, 0, sizeof p);
    p.c = c;
    p.st = st;
    p.book = osis_book;
    p.buf = buf;
    p.len = len;
    if (gem_corpus_add_book(c, osis_book))
        return -1;
    while (p.pos < len && !p.oom) {
        const char *s = buf + p.pos;
        if (*s != '<') {
            const char *lt = memchr(s, '<', len - p.pos);
            size_t n = lt ? (size_t)(lt - s) : len - p.pos;
            text(&p, s, n);
            p.pos += n;
            continue;
        }
        const char *end;
        if (len - p.pos >= 4 && memcmp(s, "<!--", 4) == 0) {
            const char *q = s + 4;
            end = NULL;
            for (; q + 2 < buf + len; q++)
                if (q[0] == '-' && q[1] == '-' && q[2] == '>') {
                    end = q + 2;
                    break;
                }
        } else {
            end = memchr(s, '>', len - p.pos);
        }
        if (!end) {
            violation(&p, "unterminated markup");
            break;
        }
        if (s[1] != '?' && s[1] != '!')
            tag(&p, s + 1, end);
        else if (s[1] == '!' && memcmp(s, "<!--", 4) != 0)
            violation(&p, "declaration <!...> not allowed");
        p.pos = (size_t)(end - buf) + 1u;
    }
    if (p.in_w || p.note_depth || p.header_depth || p.seg != SEG_NONE)
        violation(&p, "document ends inside an element");
    gem_corpus_end_book(c);
    return p.oom ? -1 : 0;
}
