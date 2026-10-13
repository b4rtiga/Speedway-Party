/* fixmath.c - tablica sinusow bez FPU (przyblizenie Bhaskary, blad < 0.2%). */
#include "fixmath.h"

WORD  g_sin[256];
ULONG g_rng = 0x2545F491UL;

void fixmath_init(void)
{
    WORD a;
    for (a = 0; a < 128; a++) {
        /* x w jednostkach: 128 = pi */
        LONG p = (LONG)a * (128 - a);
        LONG v = (16L * p * 16384L) / (5L * 128 * 128 - 4L * p);
        g_sin[a] = (WORD)v;
        g_sin[a + 128] = (WORD)-v;
    }
}
