/*
 * els_card.c - ELS counting for the host/card A/B (PREREG-004 kernel rule).
 *
 * One source, two targets: the host (gcc, 16 threads) and the Xeon Phi
 * 7120A (card/build.sh, up to 236 threads pinned to cores 0-58). Kernel H is
 * the scalar rolling-key counter of src/stats/els.cpp (8 KB hashed prefilter,
 * then the sorted key array); on the card, kernel V is the same count on the
 * 512-bit vector unit (els_vpu.h). Counts must equal gem-els kernel H exactly.
 *
 * Usage: els-card FILE THREADS KERNEL ROUNDS
 *   FILE    written by `gem-els CORPUS export`: texts (base letters 0..21) + sorted target keys
 *   KERNEL  H (scalar) or V (vector, KNC only)
 * Output: per text "text I total T bands B0 B1 B2", then per-text kernel time
 * (median over ROUNDS; the pool is created and parked before any timing).
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define KEY_MASK ((1u << 25) - 1u)
#define FILTER_BITS 16

struct job {
    const uint8_t *L;
    const int32_t *L32;        /* letters widened to int32 (vector kernel) */
    uint32_t n, dmin, dmax;
    const uint32_t *keys, *mult;
    uint32_t n_keys;
    const uint64_t *filter;    /* 2^16 bits, kernel H */
    const uint32_t *filter13;  /* 2^13 bits as 256 dwords, kernel V */
    char kernel;
};

struct counts {
    uint64_t total, band[3];
};

static inline uint32_t hash16(uint32_t key)
{
    return (key * 0x9E3779B1u) >> (32 - FILTER_BITS);
}

static inline unsigned band_of(uint32_t d)
{
    return d <= 10 ? 0u : d <= 100 ? 1u : 2u;
}

static uint32_t multiplicity(const struct job *j, uint32_t key)
{
    uint32_t lo = 0, hi = j->n_keys;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (j->keys[mid] < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < j->n_keys && j->keys[lo] == key ? j->mult[lo] : 0u;
}

/* Kernel H for one skip d. */
static uint64_t count_d_scalar(const struct job *j, uint32_t d)
{
    const uint8_t *L = j->L;
    const uint32_t n = j->n;
    uint64_t h = 0;
    for (uint32_t r = 0; r < d; r++) {
        uint32_t key = 0, have = 0;
        for (uint32_t p = r; p < n; p += d) {
            key = ((key << 5) | L[p]) & KEY_MASK;
            const uint32_t f = hash16(key);
            if (++have >= 5 && (j->filter[f >> 6] >> (f & 63u) & 1u))
                h += multiplicity(j, key);
        }
    }
    return h;
}

#ifdef __KNC__
#include "els_vpu.h"
#endif

/* ---- thread pool with a spin barrier ---------------------------------- */

struct pool {
    unsigned threads;
    atomic_uint arrive, generation, next_d;
    atomic_int quit;
    const struct job *job;
    struct counts *local;      /* one per thread */
};

static void barrier(struct pool *p)
{
    const unsigned gen = atomic_load_explicit(&p->generation, memory_order_acquire);
    if (atomic_fetch_add_explicit(&p->arrive, 1, memory_order_acq_rel) + 1 == p->threads + 1) {
        atomic_store_explicit(&p->arrive, 0, memory_order_relaxed);
        atomic_fetch_add_explicit(&p->generation, 1, memory_order_release);
    } else {
        while (atomic_load_explicit(&p->generation, memory_order_acquire) == gen)
            ;
    }
}

struct worker {
    struct pool *pool;
    unsigned id;
};

static void *work(void *arg)
{
    struct worker *w = arg;
    struct pool *p = w->pool;
    for (;;) {
        barrier(p); /* start */
        if (atomic_load(&p->quit))
            return NULL;
        const struct job *j = p->job;
        struct counts c = {0, {0, 0, 0}};
        for (uint32_t d; (d = atomic_fetch_add_explicit(&p->next_d, 1, memory_order_relaxed)) <= j->dmax;) {
            if (4u * d >= j->n)
                continue;
#ifdef __KNC__
            const uint64_t h = j->kernel == 'V' ? count_d_vpu(j, d) : count_d_scalar(j, d);
#else
            const uint64_t h = count_d_scalar(j, d);
#endif
            c.total += h;
            c.band[band_of(d)] += h;
        }
        p->local[w->id] = c;
        barrier(p); /* end */
    }
}

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

static int cmp_double(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void *read_all(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: %s FILE THREADS KERNEL(H|V) ROUNDS\n", argv[0]);
        return 2;
    }
    const unsigned threads = (unsigned)atoi(argv[2]), rounds = (unsigned)atoi(argv[4]);
    const char kernel = argv[3][0];
#ifndef __KNC__
    if (kernel == 'V') {
        fprintf(stderr, "kernel V needs the KNC vector unit\n");
        return 2;
    }
#endif
    size_t len = 0;
    uint8_t *buf = read_all(argv[1], &len);
    uint32_t hdr[7];
    if (!buf || len < sizeof hdr || (memcpy(hdr, buf, sizeof hdr), memcmp(hdr, "ELSB", 4) != 0)) {
        fprintf(stderr, "bad input %s\n", argv[1]);
        return 1;
    }
    const uint32_t n = hdr[1], texts = hdr[2], n_keys = hdr[3], dmin = hdr[4], dmax = hdr[5];
    const uint8_t *letters = buf + sizeof hdr;
    uint32_t *keys = malloc(n_keys * sizeof *keys), *mult = malloc(n_keys * sizeof *mult);
    memcpy(keys, letters + (size_t)n * texts, n_keys * sizeof *keys);
    memcpy(mult, letters + (size_t)n * texts + n_keys * sizeof *keys, n_keys * sizeof *mult);
    if (sizeof hdr + (size_t)n * texts + 2u * n_keys * sizeof(uint32_t) != len) {
        fprintf(stderr, "size mismatch\n");
        return 1;
    }
    uint64_t *filter = calloc((1u << FILTER_BITS) / 64u, sizeof *filter);
    uint32_t *filter13 = NULL;
    if (posix_memalign((void **)&filter13, 64, 256 * sizeof *filter13) != 0)
        return 1;
    memset(filter13, 0, 256 * sizeof *filter13);
    for (uint32_t i = 0; i < n_keys; i++) {
        const uint32_t f = hash16(keys[i]);
        filter[f >> 6] |= 1ull << (f & 63u);
        const uint32_t g = (keys[i] * 0x9E3779B1u) >> 19; /* hash13, els_vpu.h */
        filter13[g >> 5] |= 1u << (g & 31u);
    }
#ifdef __KNC__
    if (kernel == 'V') {
        const int bad = vpu_selftest();
        printf("vpu selftest: %d mismatches\n", bad);
        if (bad)
            return 1;
    }
#endif
    int32_t *L32 = NULL;
    if (posix_memalign((void **)&L32, 64, ((size_t)n + 64u) * sizeof *L32) != 0)
        return 1;

    struct pool p;
    memset(&p, 0, sizeof p);
    p.threads = threads;
    p.local = calloc(threads, sizeof *p.local);
    struct worker *ws = calloc(threads, sizeof *ws);
    pthread_t *tid = calloc(threads, sizeof *tid);
    for (unsigned i = 0; i < threads; i++) {
        ws[i] = (struct worker){&p, i};
        pthread_create(&tid[i], NULL, work, &ws[i]);
#ifdef __KNC__
        cpu_set_t set; /* CPU k (1..240) = core (k-1)/4; stay on cores 0-58 */
        CPU_ZERO(&set);
        CPU_SET(1 + i % 236, &set);
        pthread_setaffinity_np(tid[i], sizeof set, &set);
#endif
    }

    double *secs = calloc((size_t)texts * rounds, sizeof *secs);
    struct counts *first = calloc(texts, sizeof *first);
    for (unsigned round = 0; round < rounds; round++) {
        for (uint32_t t = 0; t < texts; t++) {
            struct job j = {letters + (size_t)n * t, L32, n, dmin, dmax, keys, mult, n_keys, filter, filter13, kernel};
            for (uint32_t i = 0; i < n; i++)
                L32[i] = j.L[i];
            for (uint32_t i = n; i < n + 64u; i++)
                L32[i] = 0;
            p.job = &j;
            atomic_store(&p.next_d, dmin);
            const double t0 = now();
            barrier(&p);
            barrier(&p);
            secs[(size_t)t * rounds + round] = now() - t0;
            struct counts sum = {0, {0, 0, 0}};
            for (unsigned i = 0; i < threads; i++) {
                sum.total += p.local[i].total;
                for (int b = 0; b < 3; b++)
                    sum.band[b] += p.local[i].band[b];
            }
            if (round == 0)
                first[t] = sum;
        }
    }
    atomic_store(&p.quit, 1);
    barrier(&p);
    for (unsigned i = 0; i < threads; i++)
        pthread_join(tid[i], NULL);

    double all = 0;
    for (uint32_t t = 0; t < texts; t++) {
        printf("text %u total %llu bands %llu %llu %llu\n", t, (unsigned long long)first[t].total,
               (unsigned long long)first[t].band[0], (unsigned long long)first[t].band[1],
               (unsigned long long)first[t].band[2]);
        qsort(secs + (size_t)t * rounds, rounds, sizeof *secs, cmp_double);
        all += secs[(size_t)t * rounds + rounds / 2];
    }
    qsort(secs, (size_t)texts * rounds, sizeof *secs, cmp_double);
    printf("kernel %c, %u threads, %u texts x %u rounds: per-text median %.6f s, min %.6f s, max %.6f s, "
           "sum of medians %.4f s\n",
           kernel, threads, texts, rounds, secs[(size_t)texts * rounds / 2], secs[0], secs[(size_t)texts * rounds - 1],
           all);
    return 0;
}
