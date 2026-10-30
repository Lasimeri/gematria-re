/*
 * els_vpu.h - kernel V: the ELS rolling-key count on the KNC 512-bit vector
 * unit. Included by els_card.c only when compiled with -march=knc.
 *
 * For skip d, lane j of residue block r0 carries residue class r0 + j: at
 * step t its letter sits at r0 + j + t*d, so the 16 lanes read 16 contiguous
 * int32 letters (one unaligned load = vloadunpackld + vloadunpackhd). Each
 * lane rolls its 25-bit key; from the 5th step a multiplicative hash picks a
 * bit of an 8,192-bit filter held in 16 registers (vpermd per register,
 * merged under the lane mask of that register); filter positives are
 * verified in scalar code against the sorted key array. Lane masks: lanes
 * with r0 + j >= d (d < 16 or the last block) and lanes past the end of
 * the text never count.
 *
 * Builtins: clang BuiltinsX86KNC.td (generated from Intel 327364-001), form
 * (passthrough, mask16, operands..., swizzle). vpu_selftest() checks every
 * builtin used here against scalar arithmetic before any count is made.
 */
#ifdef __KNC__
#include <immintrin.h>

typedef int v16i __attribute__((vector_size(64)));
#define V_ALL ((unsigned short)0xffff)
#define HASH13_SHIFT 19

static inline v16i v_add(v16i a, v16i b) { return __builtin_ia32_knc_vpaddd_zzz(a, V_ALL, a, b, 0); }
static inline v16i v_and(v16i a, v16i b) { return __builtin_ia32_knc_vpandd_zzz(a, V_ALL, a, b, 0); }
static inline v16i v_mul(v16i a, v16i b) { return __builtin_ia32_knc_vpmulld_zzz(a, V_ALL, a, b, 0); }
static inline v16i v_srlv(v16i a, v16i b) { return __builtin_ia32_knc_vpsrlvd_zzz(a, V_ALL, a, b, 0); }
static inline v16i v_perm(v16i keep, unsigned short m, v16i idx, v16i table)
{
    return __builtin_ia32_knc_vpermd_zzz(keep, m, idx, table);
}
static inline unsigned short v_eq(v16i a, v16i b) { return __builtin_ia32_knc_vpcmpeqd_kzz(V_ALL, a, b, 0); }
static inline v16i v_set1(int x) { return (v16i)_mm512_set1_epi32(x); }
static inline v16i v_or(v16i a, v16i b) { return (v16i)_mm512_or_epi32((__m512i)a, (__m512i)b); }
/* Immediate shifts: the KNC builtin needs a constant count, so macros. */
#define v_shl(a, s) ((v16i)_mm512_slli_epi32((__m512i)(a), (s)))
#define v_shr(a, s) ((v16i)_mm512_srli_epi32((__m512i)(a), (s)))
static inline v16i v_loadu(const int32_t *p)
{
    __m512i v = _mm512_undefined_epi32();
    v = _mm512_loadunpacklo_epi32(v, p);
    return (v16i)_mm512_loadunpackhi_epi32(v, p + 16);
}

static inline uint32_t hash13(uint32_t key)
{
    return (key * 0x9E3779B1u) >> HASH13_SHIFT;
}

/* 0 if every builtin used by count_d_vpu matches scalar arithmetic. */
static int vpu_selftest(void)
{
    int32_t a[16] __attribute__((aligned(64))), b[16] __attribute__((aligned(64))), out[16] __attribute__((aligned(64)));
    int32_t text[64] __attribute__((aligned(64)));
    for (int i = 0; i < 64; i++)
        text[i] = i * 7 + 3;
    for (int i = 0; i < 16; i++) {
        a[i] = (int32_t)(0x01234567u * (unsigned)(i + 1));
        b[i] = (i * 5) & 15;
    }
    const v16i va = *(const v16i *)a, vb = *(const v16i *)b;
    int bad = 0;
    *(v16i *)out = v_mul(va, va);
    for (int i = 0; i < 16; i++)
        bad += (uint32_t)out[i] != (uint32_t)a[i] * (uint32_t)a[i];
    *(v16i *)out = v_srlv(va, vb);
    for (int i = 0; i < 16; i++)
        bad += (uint32_t)out[i] != (uint32_t)a[i] >> b[i];
    *(v16i *)out = v_perm(v_set1(-1), V_ALL, vb, va); /* out[i] = a[b[i]] */
    for (int i = 0; i < 16; i++)
        bad += out[i] != a[b[i]];
    *(v16i *)out = v_perm(v_set1(-1), 0x00ff, vb, va); /* masked: high lanes keep -1 */
    for (int i = 0; i < 16; i++)
        bad += out[i] != (i < 8 ? a[b[i]] : -1);
    for (int off = 0; off < 32; off += 5) {
        *(v16i *)out = v_loadu(text + off);
        for (int i = 0; i < 16; i++)
            bad += out[i] != text[off + i];
    }
    unsigned short m = v_eq(va, va), z = v_eq(va, v_add(va, v_set1(1)));
    bad += m != 0xffff || z != 0;
    return bad;
}

static uint64_t count_d_vpu(const struct job *j, uint32_t d)
{
    v16i tab[16];
    for (int r = 0; r < 16; r++)
        tab[r] = *(const v16i *)(j->filter13 + 16 * r);
    const v16i lane = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const v16i mask25 = v_set1((int)KEY_MASK), golden = v_set1((int)0x9E3779B1u);
    const v16i fifteen = v_set1(15), thirtyone = v_set1(31), one = v_set1(1), zero = v_set1(0);
    v16i reg_id[16];
    for (int r = 0; r < 16; r++)
        reg_id[r] = v_set1(r);
    int32_t keys_out[16] __attribute__((aligned(64)));
    uint64_t h = 0;
    for (uint32_t r0 = 0; r0 < d; r0 += 16) {
        const uint32_t lanes = d - r0 >= 16 ? 16u : d - r0;
        const unsigned short active = lanes == 16 ? V_ALL : (unsigned short)((1u << lanes) - 1u);
        v16i key = zero;
        for (uint32_t t = 0, base = r0; base < j->n; t++, base += d) {
            const uint32_t rem = j->n - base;
            const unsigned short live = active & (rem >= 16 ? V_ALL : (unsigned short)((1u << rem) - 1u));
            key = v_and(v_or(v_shl(key, 5), v_loadu(j->L32 + base)), mask25);
            if (t < 4)
                continue;
            const v16i hsh = v_shr(v_mul(key, golden), HASH13_SHIFT);
            const v16i reg = v_shr(hsh, 9), dw = v_and(v_shr(hsh, 5), fifteen), bit = v_and(hsh, thirtyone);
            v16i word = zero;
            for (int r = 0; r < 16; r++)
                word = v_perm(word, v_eq(reg, reg_id[r]), dw, tab[r]);
            const unsigned short hit = live & (unsigned short)~v_eq(v_and(v_srlv(word, bit), one), zero);
            if (hit) {
                *(v16i *)keys_out = key;
                for (unsigned short m = hit; m; m &= (unsigned short)(m - 1))
                    h += multiplicity(j, (uint32_t)keys_out[__builtin_ctz(m)]);
            }
        }
    }
    (void)lane;
    return h;
}

#endif /* __KNC__ */
