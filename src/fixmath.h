/* fixmath.h - arytmetyka staloprzecinkowa (68EC020 nie ma FPU). */
#ifndef FIXMATH_H
#define FIXMATH_H

#include <exec/types.h>

typedef LONG fix16;                 /* 16.16 */
#define FIX_ONE        (1L << 16)
#define INT2FIX(i)     ((fix16)(i) << 16)
#define FIX2INT(f)     ((WORD)((f) >> 16))
#define FIXMUL(a, b)   ((fix16)(((long long)(a) * (b)) >> 16))
#define FIXPCT(a, pct) ((fix16)(((a) / 100) * (pct)))   /* a * pct% */

/* Kat binarny: 256 jednostek = 360 st.; 8 jednostek = 11.25 st. = 1 klatka duszka. */
typedef UBYTE angle8;
#define ANGLE_STEPS       256
#define SPRITE_DIRS       32
#define ANGLE_TO_FRAME(a) ((UBYTE)((((a) + 4) >> 3) & (SPRITE_DIRS - 1)))

extern WORD g_sin[256];             /* Q14: 16384 = 1.0 */
#define SIN(a) (g_sin[(UBYTE)(a)])
#define COS(a) (g_sin[(UBYTE)((a) + 64)])

void fixmath_init(void);

/* xorshift32 - szybki RNG bez dzielenia */
extern ULONG g_rng;
static inline ULONG rng_next(void)
{
    ULONG x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}
/* losowa liczba z zakresu [lo, hi] (hi-lo < 65536) */
static inline UWORD rng_range(UWORD lo, UWORD hi)
{
    return (UWORD)(lo + (UWORD)(((rng_next() & 0xFFFF) * (ULONG)(hi - lo + 1)) >> 16));
}

#endif
