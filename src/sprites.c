/* sprites.c - duszki sprzetowe motocykli (AGA, 32 px, pary attached = 15 kolorow).
 *
 * Format danych przy FMODE SPR32 (W = SPRITE_W/16 = 2 slowa na plan):
 *   naglowek: POS, 0, CTL, 0                  (2*W slow)
 *   linia:    planA[W], planB[W]               (2*W slow) x SPRITE_H
 *   koniec:   2*W zer
 * Para attached: duszek parzysty daje bity 0-1 koloru, nieparzysty (bit ATT) bity 2-3.
 * Wszystkie pary attached dziela kolory 240..255, dlatego kolor kasku jest
 * w danych (osobny zestaw klatek na kazde pole A-D), a nie w palecie.
 *
 * Klatki: 4 kaski x 32 kierunki (co 11.25 st.) x 3 uslizgi x 2 polowki.
 * Ponizej generator zastepczej grafiki - docelowo dane z pliku pikselartu. */
#include <exec/memory.h>
#include <proto/exec.h>

#include "video.h"
#include "fixmath.h"
#include "game.h"

#define W            (SPRITE_W / 16)
#define HALF_WORDS   ((SPRITE_H + 2) * 2 * W)
#define FRAME_WORDS  (HALF_WORDS * 2)
#define NUM_FRAMES   (4 * SPRITE_DIRS * SKID_FRAMES)
#define SPR_BYTES    ((ULONG)NUM_FRAMES * FRAME_WORDS * 2)

static UWORD *spr_mem;
UWORD *g_blank_sprite;

/* kolory w obrebie duszka (0 = przezroczysty) -> paleta 240+n */
enum { SC_TYRE = 1, SC_FRAME, SC_LEATHER, SC_HELMET_A, SC_HELMET_B, SC_HELMET_C, SC_HELMET_D, SC_NUMBER, SC_EXHAUST };

UWORD *sprite_frame_ptr(UBYTE gate, UBYTE frame, UBYTE skid, UBYTE half)
{
    ULONG idx = ((ULONG)gate * SPRITE_DIRS + frame) * SKID_FRAMES + skid;
    return spr_mem + idx * FRAME_WORDS + half * HALF_WORDS;
}

static UBYTE bike_pixel(WORD u, WORD v, UBYTE gate, UBYTE skid)
{
    /* u: wzdluz motocykla (+ przod), v: w poprzek. Uslizg wychyla tyl na zewnatrz. */
    if (u < 0) v += (WORD)(skid * (-u) / 3);
    if (u >= 5 && u <= 9 && v >= -1 && v <= 1)  return SC_TYRE;
    if (u >= -10 && u <= -5 && v >= -1 && v <= 1) return SC_TYRE;
    if (u >= -1 && u <= 2 && v >= -2 && v <= 2)  return (UBYTE)(SC_HELMET_A + gate);
    if (u >= -5 && u <= 3 && v >= -3 && v <= 3)  return SC_LEATHER;
    if (u >= 3 && u <= 4 && v >= -5 && v <= 5)   return SC_FRAME;
    if (u >= -6 && u <= 6 && v >= -1 && v <= 1)  return SC_FRAME;
    if (u == -7 && v == 2)                       return SC_EXHAUST;
    return 0;
}

static void render_frame(UBYTE gate, UBYTE dir, UBYTE skid)
{
    UWORD *even = sprite_frame_ptr(gate, dir, skid, 0);
    UWORD *odd  = sprite_frame_ptr(gate, dir, skid, 1);
    angle8 a = (angle8)(dir * 8);
    WORD cs = COS(a), sn = SIN(a);
    WORD x, y;

    for (y = 0; y < SPRITE_H; y++) {
        UWORD *le = even + 2 * W + y * 2 * W;
        UWORD *lo = odd  + 2 * W + y * 2 * W;
        for (x = 0; x < SPRITE_W; x++) {
            WORD dx = x - SPRITE_W / 2, dy = y - SPRITE_H / 2;
            WORD u = (WORD)(((LONG)dx * cs + (LONG)dy * sn) >> 14);
            WORD v = (WORD)(((LONG)dy * cs - (LONG)dx * sn) >> 14);
            UBYTE c = bike_pixel(u, v, gate, skid);
            UWORD bit = (UWORD)(0x8000 >> (x & 15));
            UWORD wi = (UWORD)(x >> 4);
            if (c & 1) le[wi]     |= bit;
            if (c & 2) le[W + wi] |= bit;
            if (c & 4) lo[wi]     |= bit;
            if (c & 8) lo[W + wi] |= bit;
        }
    }
}

BOOL sprites_init(void)
{
    UBYTE g, d, s;
    spr_mem = AllocMem(SPR_BYTES + HALF_WORDS * 2, MEMF_CHIP | MEMF_CLEAR);
    if (!spr_mem) return FALSE;
    g_blank_sprite = spr_mem + SPR_BYTES / 2;    /* same zera = duszek wylaczony */

    /* pikselart z tools/genart.py (dokladnie ten uklad pamieci); bez pliku - zastepcze klatki */
    if (asset_load("PROGDIR:data/bikes.spr", spr_mem, SPR_BYTES))
        return TRUE;

    for (g = 0; g < 4; g++)
        for (d = 0; d < SPRITE_DIRS; d++)
            for (s = 0; s < SKID_FRAMES; s++)
                render_frame(g, d, s);
    return TRUE;
}

void sprites_free(void)
{
    if (spr_mem) FreeMem(spr_mem, SPR_BYTES + HALF_WORDS * 2);
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
