/* sprites.c - duszki sprzetowe motocykli (AGA, 32 px, pary attached = 15 kolorow).
 *
 * Format danych przy FMODE SPR32 (W = SPRITE_W/16 = 2 slowa na plan):
 *   naglowek: POS, 0, CTL, 0                  (2*W slow)
 *   linia:    planA[W], planB[W]               (2*W slow) x SPRITE_H
 *   koniec:   2*W zer
 * Para attached: duszek parzysty daje bity 0-1 koloru, nieparzysty (bit ATT) bity 2-3.
 * Wszystkie pary attached dziela kolory 240..255, dlatego kolor kasku jest w danych,
 * a nie w palecie.
 *
 * Biblioteka klatek (data/bikes.spr): 32 kierunki (co 11.25 st.) x 3 uslizgi x 2 polowki,
 * jeden komplet z kaskiem w kolorze HELMET_BASE. Ekran nigdy nie pokazuje biblioteki -
 * kazdy motocykl ma wlasne kopie klatki (po jednej na bufor obrazu) w Chip RAM, a kolor
 * kasku zawodnika nadawany jest przy kopiowaniu. Dzieki temu biblioteka (51 KB, dawniej
 * 4 komplety = 204 KB w Chip RAM) moze lezec w dowolnej pamieci. */
#include <exec/memory.h>
#include <proto/exec.h>

#include "video.h"
#include "fixmath.h"
#include "game.h"

#define W            (SPRITE_W / 16)
#define HALF_WORDS   ((SPRITE_H + 2) * 2 * W)
#define FRAME_WORDS  (HALF_WORDS * 2)
#define NUM_FRAMES   (SPRITE_DIRS * SKID_FRAMES)
#define SPR_BYTES    ((ULONG)NUM_FRAMES * FRAME_WORDS * 2)

/* Kopie klatek motocykli: osobno dla kazdego motocykla i kazdego bufora obrazu. Pozycja
 * siedzi w naglowku danych duszka - zapis do klatki, ktora wlasnie wyswietla Agnus, dzialalby
 * raz od tej, raz od nastepnej ramki (zaleznie od chwili zapisu) i ruch by szarpal. */
#define BIKE_COPIES  (MAX_RIDERS * 2)
#define COPY_BYTES   ((ULONG)BIKE_COPIES * FRAME_WORDS * 2)
#define CHIP_BYTES   (HALF_WORDS * 2 + COPY_BYTES)     /* pusty duszek + kopie */

/* kask w bibliotece ma kolor 10 (1010b, czerwony); zawodnik n dostaje 10 + n
 * (tools/genart.py: HR, HB, HW, HY = 10..13) */
#define HELMET_BASE  10

static UWORD *spr_mem, *chip_mem, *bike_mem;
static UWORD  bike_frame_id[BIKE_COPIES];    /* klatka w kopii (0xFFFF = brak) */
UWORD *g_blank_sprite;

static const UWORD *library_frame(UBYTE frame, UBYTE skid)
{
    return spr_mem + ((ULONG)frame * SKID_FRAMES + skid) * FRAME_WORDS;
}

BOOL sprites_init(void)
{
    UBYTE g;
    chip_mem = AllocMem(CHIP_BYTES, MEMF_CHIP | MEMF_CLEAR);
    spr_mem = AllocMem(SPR_BYTES, MEMF_ANY);
    if (!chip_mem || !spr_mem) return FALSE;
    g_blank_sprite = chip_mem;                   /* same zera = duszek wylaczony */
    bike_mem = chip_mem + HALF_WORDS;
    for (g = 0; g < BIKE_COPIES; g++) bike_frame_id[g] = 0xFFFF;
    /* pikselart z tools/genart.py (dokladnie ten uklad pamieci) - bez niego gra nie rusza */
    return asset_load("PROGDIR:data/bikes.spr", spr_mem, SPR_BYTES);
}

void sprites_free(void)
{
    if (spr_mem) FreeMem(spr_mem, SPR_BYTES);
    if (chip_mem) FreeMem(chip_mem, CHIP_BYTES);
}

/* Kask w kolorze HELMET_BASE -> HELMET_BASE + color. Kolor piksela to 4 bity w 4 planach
 * (polowka parzysta: bity 0-1, nieparzysta: bity 2-3), wiec maska pikseli kasku i podmiana
 * koloru to kilka operacji na slowo - bez przegladania pikseli. */
static void recolor_helmet(UWORD *f, UBYTE color)
{
    UBYTE nc = (UBYTE)(HELMET_BASE + color);
    UWORD *e = f + 2 * W, *o = f + HALF_WORDS + 2 * W;   /* pierwsze linie obu polowek */
    UWORD i;
    for (i = 0; i < SPRITE_H * 2 * W; i += 2 * W) {
        UBYTE k;
        for (k = 0; k < W; k++) {
            UWORD *p0 = e + i + k, *p1 = p0 + W, *p2 = o + i + k, *p3 = p2 + W;
            UWORD m = (UWORD)(*p3 & ~*p2 & *p1 & ~*p0);  /* piksele koloru 1010b */
            if (!m) continue;
            *p0 = (UWORD)((nc & 1) ? *p0 | m : *p0 & ~m);
            *p1 = (UWORD)((nc & 2) ? *p1 | m : *p1 & ~m);
            *p2 = (UWORD)((nc & 4) ? *p2 | m : *p2 & ~m);
            *p3 = (UWORD)((nc & 8) ? *p3 | m : *p3 & ~m);
        }
    }
}

/* klatka motocykla w jego kopii dla bufora buf (polowka parzysta; nieparzysta = +HALF_WORDS);
 * dane obrazu kopiowane (i kask przemalowany) tylko przy zmianie klatki */
UWORD *sprite_bike_frame(UBYTE bike, UBYTE buf, UBYTE color, UBYTE frame, UBYTE skid)
{
    UBYTE n = (UBYTE)(bike * 2 + buf);
    UWORD id = (UWORD)(((color * SPRITE_DIRS) + frame) * SKID_FRAMES + skid);
    UWORD *dst = bike_mem + (ULONG)n * FRAME_WORDS;
    if (bike_frame_id[n] != id) {
        const ULONG *s = (const ULONG *)library_frame(frame, skid);
        ULONG *d = (ULONG *)dst;
        UWORD k;
        for (k = 0; k < FRAME_WORDS / 2; k++) d[k] = s[k];
        if (color) recolor_helmet(dst, color);
        bike_frame_id[n] = id;
    }
    return dst;
}

/* jak sprite_set_pos, ale x w cwiartkach piksela (AGA: bity SH1/SH0 w CTL, 70/35 ns) */
void sprite_set_pos_q(UWORD *spr, LONG xq, WORD y, UWORD height, BOOL attached)
{
    UWORD hq = (UWORD)(xq - (SPRITE_W / 2) * 4 + 0x80 * 4);
    UWORD vs = (UWORD)(y - SPRITE_H / 2 + DISPLAY_VSTART);
    UWORD ve = (UWORD)(vs + height);
    spr[0] = (UWORD)(((vs & 0xFF) << 8) | ((hq >> 3) & 0xFF));
    spr[W] = (UWORD)(((ve & 0xFF) << 8) | (attached ? 0x80 : 0) | (((hq >> 1) & 1) << 4) | ((hq & 1) << 3)
                   | (((vs >> 8) & 1) << 2) | (((ve >> 8) & 1) << 1) | ((hq >> 2) & 1));
}

/* x,y = srodek motocykla w pikselach pola gry */
void sprite_set_pos(UWORD *spr, WORD x, WORD y, UWORD height, BOOL attached)
{
    UWORD hs = (UWORD)(x - SPRITE_W / 2 + 0x80);
    UWORD vs = (UWORD)(y - SPRITE_H / 2 + DISPLAY_VSTART);
    UWORD ve = (UWORD)(vs + height);
    spr[0] = (UWORD)(((vs & 0xFF) << 8) | ((hs >> 1) & 0xFF));
    spr[W] = (UWORD)(((ve & 0xFF) << 8) | (attached ? 0x80 : 0)
                   | (((vs >> 8) & 1) << 2) | (((ve >> 8) & 1) << 1) | (hs & 1));
}
