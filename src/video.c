/* video.c - Copper-lista AGA: 8 bitplanow (256 kolorow 24-bit), HUD 5 bitplanow,
 * duszki 64 px, podwojne buforowanie przez dwie Copper-listy.
 *
 * Struktura kazdej Copper-listy (wykonywana od linii 0 kazdej ramki):
 *
 *   [naglowek]   FMODE, DIWSTRT/STOP, DDFSTRT/STOP, BPLCON0..4, modulo
 *   [duszki]     SPR0PT..SPR7PT  (16 MOVE)  <- CPU podmienia co klatke
 *   [bitplany]   BPL1PT..BPL8PT  (16 MOVE)  <- bufor tej listy
 *   [paleta]     8 bankow x (BPLCON3 + 32 MOVE hi + BPLCON3|LOCT + 32 MOVE lo)
 *                = 528 MOVE, miesci sie w wygaszaniu pionowym (linie 0..$2B)
 *   [linie 0..207, w menu 0..255] co linie: WAIT, COLOR00 (tlo), BPLCON3 bank3, COLOR100, BPLCON3 bank0
 *                -> gradient tla i "teczowy" kolor napisow w menu, 1040 instrukcji
 *   WAIT $FC     poczatek HUD: BPLCON0 = 0 planow (pusta przerwa 4 linie)
 *                paleta HUD (32 kolory, bank 0), BPL1..5PT -> ekran HUD, modulo HUD
 *   WAIT $FF,$DF przejscie przez linie 255 wiazki
 *   WAIT $00     (= linia $100) BPLCON0 = 5 planow
 *   WAIT kazda linia: COLOR00 = gradient tla HUD (1 MOVE/linie)
 *   END          $FFFF,$FFFE
 *
 * Zmiana palety toru (mokry/suchy) = przepisanie wartosci w MOVE-ach palety
 * obu list; grafika toru w ogole nie jest ruszana. */
#include <exec/memory.h>
#include <proto/exec.h>

#include "hw.h"
#include "video.h"
#include "system.h"

Video g_video;

/* Pobieranie bitplanow AGA. Mozna nadpisac przy kompilacji (make EXTRA=-DDDF_STOP=...)
 * do testow w emulatorze. */
#ifndef BPL_FMODE
#define BPL_FMODE   FMODE_BPL_4X
#endif
#ifndef DDF_START
#define DDF_START   0x0038
#endif
#ifndef DDF_STOP
#define DDF_STOP    0x00A0
#endif
#ifndef MOD_ADJ
#define MOD_ADJ     0           /* korekta modulo, gdy Agnus pobiera dodatkowy blok */
#endif

#define COPPER_WORDS    6144
#define LINE_WORDS      10          /* WAIT + 4 MOVE na linie */
#define UI_RAINBOW      100         /* kolor tekstu zmieniany co linie (bank 3, indeks 4) */
#define HUD_GAP         4

/* indeks palety: kolory duszkow to 240..255 (BPLCON4 OSPRM/ESPRM = $F) */
#define BPLCON4_SPRITES_240 0x00FF

#define BPLCON0_PLAY  (BPLCON0_PLANES(PLAY_DEPTH) | BPLCON0_COLOR | BPLCON0_ECSENA)
#define BPLCON0_HUD   (BPLCON0_PLANES(HUD_DEPTH)  | BPLCON0_COLOR | BPLCON0_ECSENA)
#define BPLCON0_BLANK (BPLCON0_COLOR | BPLCON0_ECSENA)
#define BPLCON2_SPR_FRONT 0x0024      /* duszki przed playfieldem */

static inline UWORD *cop_move(UWORD *c, UWORD reg, UWORD val) { *c++ = reg; *c++ = val; return c; }
static inline UWORD *cop_wait(UWORD *c, UBYTE v, UBYTE h)
{
    *c++ = (UWORD)((v << 8) | (h & 0xFE) | 1);
    *c++ = 0xFFFE;
    return c;
}

static inline UWORD rgb_hi(const RGB24 *c) { return (UWORD)(((c->r >> 4) << 8) | ((c->g >> 4) << 4) | (c->b >> 4)); }
static inline UWORD rgb_lo(const RGB24 *c) { return (UWORD)(((c->r & 15) << 8) | ((c->g & 15) << 4) | (c->b & 15)); }

static UWORD *build_copper(UWORD *c, UBYTE buf, UBYTE menu)
{
    UWORD i, b, lines = menu ? FULL_H : PLAY_H;
    ULONG pl;

    c = cop_move(c, R_FMODE,   BPL_FMODE | (SPRITE_W == 64 ? FMODE_SPR_4X : FMODE_SPR_2X));
    c = cop_move(c, R_DIWSTRT, (DISPLAY_VSTART << 8) | 0x81);
    c = cop_move(c, R_DIWSTOP, ((DISPLAY_VSTART + 256) & 0xFF) << 8 | 0xC1);
    /* 320 px = 5 blokow po 64 px przy fetch 4x; standardowe $38/$D0.
     * Jesli w WinUAE obraz sie "scina" o 64 px - korekta DDFSTOP/modulo tutaj. */
    c = cop_move(c, R_DDFSTRT, DDF_START);
    c = cop_move(c, R_DDFSTOP, DDF_STOP);
    c = cop_move(c, R_BPLCON0, BPLCON0_PLAY);
    c = cop_move(c, R_BPLCON1, 0);
    c = cop_move(c, R_BPLCON2, BPLCON2_SPR_FRONT);
    c = cop_move(c, R_BPLCON3, BPLCON3_BANK(0));
    c = cop_move(c, R_BPLCON4, BPLCON4_SPRITES_240);
    c = cop_move(c, R_BPL1MOD, PLAY_STRIDE - ROW_BYTES + MOD_ADJ);   /* przeplot: pomijamy pozostale plany */
    c = cop_move(c, R_BPL2MOD, PLAY_STRIDE - ROW_BYTES + MOD_ADJ);

    g_video.cop_sprites[menu][buf] = c;
    for (i = 0; i < 16; i++)
        c = cop_move(c, R_SPR0PT + i * 2, 0);

    pl = (ULONG)g_video.play[buf];
    for (i = 0; i < PLAY_DEPTH; i++, pl += ROW_BYTES) {
        c = cop_move(c, R_BPL1PT + i * 4,     (UWORD)(pl >> 16));
        c = cop_move(c, R_BPL1PT + i * 4 + 2, (UWORD)pl);
    }

    g_video.cop_play_pal[menu][buf] = c;
    for (b = 0; b < 8; b++) {
        c = cop_move(c, R_BPLCON3, BPLCON3_BANK(b));
        for (i = 0; i < 32; i++) c = cop_move(c, R_COLOR00 + i * 2, 0);
        c = cop_move(c, R_BPLCON3, BPLCON3_BANK(b) | BPLCON3_LOCT);
        for (i = 0; i < 32; i++) c = cop_move(c, R_COLOR00 + i * 2, 0);
    }
    c = cop_move(c, R_BPLCON3, BPLCON3_BANK(0));

    /* ---------------- kolory per linia ----------------
     * WAIT na koncu poprzedniej linii; dla linii $100 wypada WAIT $FF,$DF, a kolejne
     * (mlodszy bajt pozycji pionowej) dzialaja juz za granica 255 */
    g_video.cop_lines[menu][buf] = c;
    for (i = 0; i < lines; i++) {
        c = cop_wait(c, (UBYTE)(DISPLAY_VSTART + i - 1), 0xDF);
        c = cop_move(c, R_COLOR00, 0x000);
        c = cop_move(c, R_BPLCON3, BPLCON3_BANK(UI_RAINBOW >> 5));
        c = cop_move(c, R_COLOR00 + (UI_RAINBOW & 31) * 2, 0xFFF);
        c = cop_move(c, R_BPLCON3, BPLCON3_BANK(0));
    }

    if (menu) {                                         /* menu: caly ekran, bez HUD */
        *c++ = 0xFFFF; *c++ = 0xFFFE;
        return c;
    }

    /* ---------------- strefa HUD ---------------- */
    c = cop_wait(c, HUD_VSTART, 0x07);
    c = cop_move(c, R_BPLCON0, BPLCON0_BLANK);

    g_video.cop_hud_pal[buf] = c;
    c = cop_move(c, R_BPLCON3, BPLCON3_BANK(0));
    for (i = 0; i < 32; i++) c = cop_move(c, R_COLOR00 + i * 2, 0);
    c = cop_move(c, R_BPLCON3, BPLCON3_BANK(0) | BPLCON3_LOCT);
    for (i = 0; i < 32; i++) c = cop_move(c, R_COLOR00 + i * 2, 0);
    c = cop_move(c, R_BPLCON3, BPLCON3_BANK(0));

    pl = (ULONG)g_video.hud;
    for (i = 0; i < HUD_DEPTH; i++, pl += ROW_BYTES) {
        c = cop_move(c, R_BPL1PT + i * 4,     (UWORD)(pl >> 16));
        c = cop_move(c, R_BPL1PT + i * 4 + 2, (UWORD)pl);
    }
    c = cop_move(c, R_BPL1MOD, HUD_STRIDE - ROW_BYTES + MOD_ADJ);
    c = cop_move(c, R_BPL2MOD, HUD_STRIDE - ROW_BYTES + MOD_ADJ);

    c = cop_wait(c, 0xFF, 0xDF);                      /* przejscie przez linie 255 */
    c = cop_wait(c, (HUD_VSTART + HUD_GAP) & 0xFF, 0x07);
    c = cop_move(c, R_BPLCON0, BPLCON0_HUD);

    /* gradient tla HUD: 1 MOVE na linie (zapis bez LOCT ustawia tez dolne bity) */
    for (i = HUD_GAP; i < HUD_H; i++) {
        UWORD t = (UWORD)((i - HUD_GAP) * 15 / (HUD_H - HUD_GAP));
        c = cop_wait(c, (UBYTE)((HUD_VSTART + i) & 0xFF), 0xDF);
        c = cop_move(c, R_COLOR00, (UWORD)((1 << 8) | ((1 + t / 3) << 4) | (4 + t / 2)));
    }

    *c++ = 0xFFFF; *c++ = 0xFFFE;
    return c;
}

BOOL video_init(void)
{
    UBYTE i, m;
    for (i = 0; i < 2; i++) {
        g_video.play[i] = AllocMem(FULL_SIZE, MEMF_CHIP | MEMF_CLEAR);
        if (!g_video.play[i]) return FALSE;
        for (m = 0; m < 2; m++) {
            g_video.copper[m][i] = AllocMem(COPPER_WORDS * 2, MEMF_CHIP | MEMF_CLEAR);
            if (!g_video.copper[m][i]) return FALSE;
        }
    }
    /* tla torow (Chip RAM, Blitter kopiuje z nich) przydziela track.c */
    g_video.hud = AllocMem(HUD_SIZE, MEMF_CHIP | MEMF_CLEAR);
    if (!g_video.hud) return FALSE;

    for (i = 0; i < 2; i++)
        for (m = 0; m < 2; m++)
            build_copper(g_video.copper[m][i], i, m);
    g_video.front = 0;
    g_video.back  = 1;
    g_video.menu  = 1;                                  /* gra startuje od planszy tytulowej */
    return TRUE;
}

void video_free(void)
{
    UBYTE i, m;
    for (i = 0; i < 2; i++) {
        if (g_video.play[i]) FreeMem(g_video.play[i], FULL_SIZE);
        for (m = 0; m < 2; m++)
            if (g_video.copper[m][i]) FreeMem(g_video.copper[m][i], COPPER_WORDS * 2);
    }
    if (g_video.hud)        FreeMem(g_video.hud, HUD_SIZE);
}

void video_start(void)
{
    REG32(R_COP1LC) = (ULONG)g_video.copper[g_video.menu][g_video.front];
    REG16(R_COPJMP1) = 0;
    REG16(R_DMACON) = DMAF_SETCLR | DMAF_DMAEN | DMAF_BPLEN | DMAF_COPEN | DMAF_BLTEN | DMAF_SPREN;
}

static void write_palette(UWORD *pal_moves, UWORD index, const RGB24 *c)
{
    UWORD bank = index >> 5, i = index & 31;
    UWORD *bankbase = pal_moves + bank * (66 * 2);
    bankbase[(1 + i) * 2 + 1]  = rgb_hi(c);
    bankbase[(34 + i) * 2 + 1] = rgb_lo(c);
}

/* Listy menu maja wlasna palete (ilustracja tytulowa, video_set_menu_palette) - paleta toru
 * trafia do nich tylko, gdy wlasnej nie ma. Dawniej kazda zmiana palety toru (start turnieju,
 * schniecie) psula ilustracje na ekranie wyboru. */
static BOOL menu_own_pal;

void video_set_play_palette(const RGB24 *pal)
{
    UWORD i;
    UBYTE b, m;
    for (m = 0; m < (menu_own_pal ? 1 : 2); m++)
        for (b = 0; b < 2; b++)
            for (i = 0; i < 256; i++)
                write_palette(g_video.cop_play_pal[m][b], i, &pal[i]);
}

/* paleta tylko dla list Coppera menu (plansza tytulowa z wlasna paleta ilustracji) */
void video_set_menu_palette(const RGB24 *pal)
{
    menu_own_pal = TRUE;
    UWORD i;
    UBYTE b;
    for (b = 0; b < 2; b++)
        for (i = 0; i < 256; i++)
            write_palette(g_video.cop_play_pal[1][b], i, &pal[i]);
}

void video_set_hud_palette(const RGB24 *pal)
{
    UBYTE b, i;
    for (b = 0; b < 2; b++)
        for (i = 0; i < 32; i++)
            write_palette(g_video.cop_hud_pal[b], i, &pal[i]);
}

/* pojedynczy kolor HUD w obu listach (np. kolor flagi sedziego) */
void video_set_hud_color(UBYTE index, UBYTE r, UBYTE g, UBYTE b)
{
    RGB24 c;
    UBYTE i;
    c.r = r; c.g = g; c.b = b;
    for (i = 0; i < 2; i++)
        write_palette(g_video.cop_hud_pal[i], index, &c);
}

/* Color Palette Swapping: interpolacja sucho->mokro, tylko kolory toru.
 * Wywolywane rzadko (gdy zmienia sie wilgotnosc), koszt ~256 interpolacji. */
void video_blend_track_palette(UBYTE wet)
{
    RGB24 tmp[256];
    UWORD i;
    for (i = 0; i < 256; i++) {
        const RGB24 *d = &g_video.pal_dry[i], *w = &g_video.pal_wet[i];
        tmp[i].r = (UBYTE)(d->r + (((WORD)w->r - d->r) * wet >> 8));
        tmp[i].g = (UBYTE)(d->g + (((WORD)w->g - d->g) * wet >> 8));
        tmp[i].b = (UBYTE)(d->b + (((WORD)w->b - d->b) * wet >> 8));
    }
    video_set_play_palette(tmp);
}

/* Fala na trybunach: 32 kolory koszulek kibicow (140..171, tools/genart.py) przesuniete w kolko
 * o shift - klasyczne cyklowanie kolorow, 64 wpisy do list Coppera na klatke. shift 0 = jak bylo. */
#define CROWD_FIRST 140
#define CROWD_N     32
void video_crowd_cycle(UBYTE shift)
{
    UBYTE k, b, m;
    for (k = 0; k < CROWD_N; k++) {
        const RGB24 *c = &g_video.pal_dry[CROWD_FIRST + ((k + shift) & (CROWD_N - 1))];
        for (m = 0; m < (menu_own_pal ? 1 : 2); m++)
            for (b = 0; b < 2; b++) write_palette(g_video.cop_play_pal[m][b], CROWD_FIRST + k, c);
    }
}

/* To samo po kawalku: 64 kolory na klatke (cala paleta w 4 klatki). Pelne mieszanie
 * i wpis 1024 kolorow do list Coppera naraz kosztowalo ok. pol klatki - w biegu (schniecie)
 * i przy rownaniu toru gubilo klatke. */
static UBYTE lazy_wet;
static UWORD lazy_i = 256;

void video_blend_track_palette_lazy(UBYTE wet) { lazy_wet = wet; lazy_i = 0; }

void video_palette_tick(void)
{
    UWORD end;
    if (lazy_i >= 256) return;
    for (end = (UWORD)(lazy_i + 64); lazy_i < end; lazy_i++) {
        const RGB24 *d = &g_video.pal_dry[lazy_i], *w = &g_video.pal_wet[lazy_i];
        RGB24 c;
        UBYTE b, m;
        c.r = (UBYTE)(d->r + (((WORD)w->r - d->r) * lazy_wet >> 8));
        c.g = (UBYTE)(d->g + (((WORD)w->g - d->g) * lazy_wet >> 8));
        c.b = (UBYTE)(d->b + (((WORD)w->b - d->b) * lazy_wet >> 8));
        for (m = 0; m < (menu_own_pal ? 1 : 2); m++)
            for (b = 0; b < 2; b++) write_palette(g_video.cop_play_pal[m][b], lazy_i, &c);
    }
}

void video_set_sprite(UBYTE n, const UWORD *data)
{
    UBYTE m;
    for (m = 0; m < 2; m++) {
        UWORD *c = g_video.cop_sprites[m][g_video.back] + n * 4;
        c[1] = (UWORD)((ULONG)data >> 16);
        c[3] = (UWORD)(ULONG)data;
    }
}

/* Kolory 12-bit dla kazdej linii pola gry (tylko lista "back" - zmiana od nastepnej ramki).
 * Zapis bez LOCT: AGA kopiuje gorne 4 bity do dolnych. */
void video_set_lines(const UWORD *bg, const UWORD *fg)
{
    UWORD *c = g_video.cop_lines[g_video.menu][g_video.back], i;
    UWORD n = g_video.menu ? FULL_H : PLAY_H;
    for (i = 0; i < n; i++, c += LINE_WORDS) {
        c[3] = bg[i];
        c[7] = fg[i];
    }
}

void video_set_menu(BOOL menu) { g_video.menu = (UBYTE)(menu ? 1 : 0); }

UBYTE *video_backbuffer(void)
{
    return g_video.play[g_video.back];
}

/* Podwojne buforowanie: COP1LC jest przeladowywany przez Agnusa na poczatku
 * kazdej ramki, wiec zapis w dowolnym momencie zadziala dopiero od nastepnej
 * ramki - zero rozdarc obrazu, bez czekania na konkretna linie. */
void video_swap(void)
{
    UBYTE t;
    REG32(R_COP1LC) = (ULONG)g_video.copper[g_video.menu][g_video.back];
    t = g_video.front;
    g_video.front = g_video.back;
    g_video.back = t;
}
