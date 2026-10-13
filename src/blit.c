/* blit.c - wypelnianie i kopiowanie prostokatow Blitterem.
 *
 * Bitplany sa przeplatane: wiersz r planu p lezy pod (r*8 + p) * ROW_BYTES.
 * Dzieki temu kopia prostokata ze wszystkich planow to JEDEN blit o wysokosci h*8
 * i module ROW_BYTES - szerokosc. Wypelnienie kolorem wymaga 8 blitow (kazdy plan
 * dostaje same jedynki albo same zera), bo minterm jest wspolny dla calego blitu.
 * Wysokosci > 1024 linii -> rejestry ECS/AGA BLTSIZV/BLTSIZH. */
#include <exec/memory.h>
#include <proto/exec.h>

#include "hw.h"
#include "video.h"
#include "blit.h"

#define R_BLTCON0 0x040
#define R_BLTCON1 0x042
#define R_BLTAFWM 0x044
#define R_BLTALWM 0x046
#define R_BLTCPT  0x048
#define R_BLTBPT  0x04C
#define R_BLTAPT  0x050
#define R_BLTDPT  0x054
#define R_BLTCMOD 0x060
#define R_BLTBMOD 0x062
#define R_BLTAMOD 0x064
#define R_BLTDMOD 0x066
#define R_BLTSIZE 0x058
#define R_BLTSIZV 0x05C
#define R_BLTSIZH 0x05E

#define USE_A 0x0800
#define USE_D 0x0100

static void start(UWORD con0, const void *a, void *d, WORD amod, WORD dmod, UWORD rows, UWORD words)
{
    hw_wait_blit();
    REG16(R_BLTCON0) = con0;
    REG16(R_BLTCON1) = 0;
    REG16(R_BLTAFWM) = 0xFFFF;
    REG16(R_BLTALWM) = 0xFFFF;
    if (a) { REG32(R_BLTAPT) = (ULONG)a; REG16(R_BLTAMOD) = (UWORD)amod; }
    REG32(R_BLTDPT) = (ULONG)d;
    REG16(R_BLTDMOD) = (UWORD)dmod;
    REG16(R_BLTSIZV) = rows;
    REG16(R_BLTSIZH) = words;           /* zapis BLTSIZH startuje blit */
}

void blit_clear(UBYTE *buf)
{
    start(USE_D | 0x00, 0, buf, 0, 0, FULL_H * PLAY_DEPTH, ROW_BYTES / 2);
}

void blit_fill(UBYTE *buf, WORD x, WORD y, WORD w, WORD h, UBYTE color)
{
    UBYTE pl;
    UBYTE *d = buf + (LONG)y * PLAY_STRIDE + (x >> 3);
    for (pl = 0; pl < PLAY_DEPTH; pl++, d += ROW_BYTES, color >>= 1)
        start((UWORD)(USE_D | ((color & 1) ? 0xFF : 0x00)), 0, d, 0,
              (WORD)(PLAY_STRIDE - (w >> 3)), (UWORD)h, (UWORD)(w >> 4));
}

void blit_copy(UBYTE *dst, const UBYTE *src, WORD x, WORD y, WORD w, WORD h)
{
    LONG o = (LONG)y * PLAY_STRIDE + (x >> 3);
    WORD mod = (WORD)(ROW_BYTES - (w >> 3));
    start(USE_A | USE_D | 0xF0, src + o, dst + o, mod, mod, (UWORD)(h * PLAY_DEPTH), (UWORD)(w >> 4));
}

/* ------------------------------------------------------------------ *
 * Czasteczki szprycy.
 * Rysowanie = "cookie-cut": D = A&B | ~A&C   (minterm $CA)
 *   A: maska kropki (0xC000 / 0x8000) przesunieta ASHIFT = x & 15,
 *   B: wzor koloru - dla wiersza planu p same jedynki albo zera (bit p koloru),
 *   C=D: ekran. Jeden blit obejmuje wszystkie 8 planow (bitplany przeplatane).
 * Szerokosc 2 slowa, zeby przesunieta maska mogla wejsc do drugiego slowa.
 * ------------------------------------------------------------------ */
#define DOT_ROWS_BIG   (2 * PLAY_DEPTH)
#define DOT_WORDS      2

static UWORD *mask_big, *mask_small, *color_pat;     /* Chip RAM */
#define MASK_BYTES  (DOT_ROWS_BIG * DOT_WORDS * 2)
#define PAT_WORDS   (DOT_ROWS_BIG * DOT_WORDS)
#define PAT_BYTES   (256UL * PAT_WORDS * 2)

BOOL blit_init(void)
{
    UWORD r, c;
    mask_big   = AllocMem(MASK_BYTES * 2 + PAT_BYTES, MEMF_CHIP | MEMF_CLEAR);
    if (!mask_big) return FALSE;
    mask_small = mask_big + MASK_BYTES / 2;
    color_pat  = mask_small + MASK_BYTES / 2;
    for (r = 0; r < DOT_ROWS_BIG; r++) {
        mask_big[r * DOT_WORDS] = 0xC000;
        mask_small[r * DOT_WORDS] = 0x8000;
    }
    for (c = 0; c < 256; c++)
        for (r = 0; r < DOT_ROWS_BIG; r++) {
            UWORD v = (UWORD)((c >> (r % PLAY_DEPTH)) & 1 ? 0xFFFF : 0x0000);
            color_pat[c * PAT_WORDS + r * DOT_WORDS]     = v;
            color_pat[c * PAT_WORDS + r * DOT_WORDS + 1] = v;
        }
    return TRUE;
}

void blit_free(void)
{
    if (mask_big) FreeMem(mask_big, MASK_BYTES * 2 + PAT_BYTES);
}

void blit_dot(UBYTE *buf, WORD x, WORD y, BOOL big, UBYTE color)
{
    UBYTE *d = buf + (LONG)y * PLAY_STRIDE + ((x >> 4) << 1);
    hw_wait_blit();
    REG16(R_BLTCON0) = (UWORD)(((x & 15) << 12) | 0x0F00 | 0xCA);
    REG16(R_BLTCON1) = 0;
    REG16(R_BLTAFWM) = 0xFFFF;
    REG16(R_BLTALWM) = 0xFFFF;
    REG32(R_BLTAPT) = (ULONG)(big ? mask_big : mask_small);
    REG32(R_BLTBPT) = (ULONG)(color_pat + color * PAT_WORDS);
    REG32(R_BLTCPT) = (ULONG)d;
    REG32(R_BLTDPT) = (ULONG)d;
    REG16(R_BLTAMOD) = 0;
    REG16(R_BLTBMOD) = 0;
    REG16(R_BLTCMOD) = ROW_BYTES - DOT_WORDS * 2;
    REG16(R_BLTDMOD) = ROW_BYTES - DOT_WORDS * 2;
    REG16(R_BLTSIZE) = (UWORD)(((big ? DOT_ROWS_BIG : PLAY_DEPTH) << 6) | DOT_WORDS);
}

void blit_restore(UBYTE *dst, const UBYTE *src, WORD x, WORD y, BOOL big)
{
    LONG o = (LONG)y * PLAY_STRIDE + ((x >> 4) << 1);
    hw_wait_blit();
    REG16(R_BLTCON0) = 0x09F0;                 /* A -> D */
    REG16(R_BLTCON1) = 0;
    REG16(R_BLTAFWM) = 0xFFFF;
    REG16(R_BLTALWM) = 0xFFFF;
    REG32(R_BLTAPT) = (ULONG)(src + o);
    REG32(R_BLTDPT) = (ULONG)(dst + o);
    REG16(R_BLTAMOD) = ROW_BYTES - DOT_WORDS * 2;
    REG16(R_BLTDMOD) = ROW_BYTES - DOT_WORDS * 2;
    REG16(R_BLTSIZE) = (UWORD)(((big ? DOT_ROWS_BIG : PLAY_DEPTH) << 6) | DOT_WORDS);
}
