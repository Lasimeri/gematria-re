/* hello.c - card gate G0: the 7120A runs a program built by card/build.sh and
 * reports its CPU count and one 512-bit vector operation's result. */
#include <immintrin.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    __m512i a = _mm512_set1_epi32(3), b = _mm512_slli_epi32(a, 4);
    int out[16] __attribute__((aligned(64)));
    _mm512_store_epi32(out, _mm512_or_epi32(a, b));
    printf("hello from the 7120A: %ld cpus online, vector (3 << 4) | 3 = %d (want 51)\n", sysconf(_SC_NPROCESSORS_ONLN),
           out[15]);
    return out[15] == 51 ? 0 : 1;
}
