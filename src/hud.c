/* hud.c - statyczny ekran HUD (5 bitplanow, 32 kolory, wlasna paleta z Coppera).
 * Kolejnosc wg kaskow, okrazenie lidera, swiatla startowe, sedzia z flaga.
 * Rysowane tylko przy zmianie stanu - HUD nie jest podwojnie buforowany. */
#include "game.h"
#include "video.h"

#define HUD_TOP 6               /* pierwszy widoczny wiersz (4 linie przerwy w Copperze) */

enum { H_BG = 0, H_WHITE = 1, H_HELMET = 2, H_RED_ON = 6, H_GREEN_ON = 7, H_RED_OFF = 8, H_GREEN_OFF = 9,
       H_SKIN = 10, H_DARK = 11, H_JACKET = 12, H_FLAG_W = 13, H_FLAG_K = 14, H_POLE = 15, H_FRAME = 16,
       H_JACKET_D = 18, H_SHIRT = 19, H_FLAG = 21, H_FLAG_SH = 25 };

/* portret sedziego 32x38, 5 planow przeplatanych (tools/genart.py) */
#define REF_W_BYTES 4
#define REF_H       38
#define REF_X       272
static UBYTE referee[REF_H * HUD_DEPTH * REF_W_BYTES];
static BOOL  have_referee;

typedef enum { FLAG_NONE = 0, FLAG_GREEN, FLAG_RED, FLAG_WHITE, FLAG_CHECKERED } Flag;

static struct { UBYTE order[4], lap, lights, flag, wave, menu; } shown;

static void hplot(WORD x, WORD y, UBYTE c)
{
    UBYTE *p = g_video.hud + (LONG)y * HUD_STRIDE + (x >> 3);
    UBYTE bit = (UBYTE)(0x80 >> (x & 7)), pl;
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= HUD_H) return;
    for (pl = 0; pl < HUD_DEPTH; pl++, p += ROW_BYTES, c >>= 1)
        if (c & 1) *p |= bit; else *p &= (UBYTE)~bit;
}

static void hrect(WORD x, WORD y, WORD w, WORD h, UBYTE c)
{
    WORD i, j;
    for (j = 0; j < h; j++) for (i = 0; i < w; i++) hplot(x + i, y + j, c);
}

/* cyfry 3x5 + '/', skalowane x3 */
static const UWORD font3x5[11] = {
    0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF, 0x12A4
};

/* cyfra 3x5 powiekszona scale razy, na tle bg */
static void hglyph_s(WORD x, WORD y, UBYTE g, UBYTE c, WORD scale, UBYTE bg)
{
    WORD r, k;
    for (r = 0; r < 5; r++)
        for (k = 0; k < 3; k++)
            hrect(x + k * scale, y + r * scale, scale, scale, (font3x5[g] >> (14 - r * 3 - k)) & 1 ? c : bg);
}

static void hglyph(WORD x, WORD y, UBYTE g, UBYTE c) { hglyph_s(x, y, g, c, 3, H_BG); }

static void draw_order(void)
{
    UBYTE i;
    /* shown.order: kolor kasku (pole) w bitach 0-3, numer zawodnika w bitach 4-7 */
    for (i = 0; i < 4; i++) {
        UBYTE v = shown.order[i];
        UBYTE gate = (UBYTE)(v & 0x0F), rider = (UBYTE)(v >> 4);
        hrect(8 + i * 22, HUD_TOP + 8, 18, 18, H_FRAME);
        if (v == 0xFF) { hrect(10 + i * 22, HUD_TOP + 10, 14, 14, H_BG); continue; }
        {
            UBYTE helmet = (UBYTE)(H_HELMET + gate);
            UBYTE ink = gate >= 2 ? H_FLAG_K : H_WHITE;      /* bialy/zolty kask: czarna cyfra */
            hrect(10 + i * 22, HUD_TOP + 10, 14, 14, helmet);
            hglyph_s(14 + i * 22, HUD_TOP + 12, (UBYTE)(rider + 1), ink, 2, helmet);
        }
    }
}

static void draw_lap(void)
{
    hglyph(110, HUD_TOP + 9, shown.lap, H_WHITE);
    hglyph(122, HUD_TOP + 9, 10, H_WHITE);
    hglyph(134, HUD_TOP + 9, LAPS_PER_HEAT, H_WHITE);
}

static void draw_lights(void)
{
    hrect(172, HUD_TOP + 8, 18, 18, shown.lights == 1 ? H_RED_ON : H_RED_OFF);
    hrect(196, HUD_TOP + 8, 18, 18, shown.lights == 2 ? H_GREEN_ON : H_GREEN_OFF);
}

/* gruba kreska (reka) */
static void hline_thick(WORD x0, WORD y0, WORD x1, WORD y1, UBYTE c)
{
    WORD i, n = (WORD)((x0 > x1 ? x0 - x1 : x1 - x0) + (y0 > y1 ? y0 - y1 : y1 - y0));
    for (i = 0; i <= n; i++) {
        WORD x = (WORD)(x0 + (x1 - x0) * i / (n ? n : 1));
        WORD y = (WORD)(y0 + (y1 - y0) * i / (n ? n : 1));
        hrect(x - 1, y - 1, 3, 3, c);
    }
}

static void set_flag_color(UBYTE flag)
{
    static const UBYTE rgb[5][3] = { {0,0,0}, {40,220,60}, {240,30,30}, {250,250,250}, {250,250,250} };
    video_set_hud_color(H_FLAG, rgb[flag][0], rgb[flag][1], rgb[flag][2]);
    video_set_hud_color(H_FLAG_SH, (UBYTE)(rgb[flag][0] * 3 / 5), (UBYTE)(rgb[flag][1] * 3 / 5),
                        (UBYTE)(rgb[flag][2] * 3 / 5));
}

/* Sedzia: portret z pliku + reka z machajaca flaga. Kolor flagi = wpis palety HUD
 * (zmiana jednego koloru w Copperze), szachownica rysowana kolorami 13/14. */
static void draw_referee(void)
{
    WORD hx = 258, hy = HUD_TOP + (shown.wave ? 24 : 26);  /* dlon; flaga siega HUD_TOP+2 */
    WORD i, j, y;

    hrect(224, HUD_TOP, SCREEN_W - 224, HUD_H - HUD_TOP - 4, H_BG);
    if (have_referee) {
        for (y = 0; y < REF_H; y++) {
            UBYTE pl, k;
            for (pl = 0; pl < HUD_DEPTH; pl++)
                for (k = 0; k < REF_W_BYTES; k++)
                    g_video.hud[(LONG)(HUD_TOP + y) * HUD_STRIDE + pl * ROW_BYTES + REF_X / 8 + k] =
                        referee[(y * HUD_DEPTH + pl) * REF_W_BYTES + k];
        }
    } else {
        hrect(276, HUD_TOP + 4, 20, 22, H_SKIN);
        hrect(272, HUD_TOP + 26, 28, 12, H_JACKET);
    }
    if (shown.flag == FLAG_NONE) return;

    set_flag_color(shown.flag);
    hline_thick(276, HUD_TOP + 28, hx + 2, hy + 2, H_FLAG_K);     /* rekaw koszulki w pasy: kontur */
    hline_thick(275, HUD_TOP + 27, hx + 1, hy + 1, H_SHIRT);
    hrect(hx - 1, hy - 1, 4, 4, H_SKIN);                          /* dlon */
    hrect(hx, hy - 22, 2, 26, H_POLE);                            /* drzewce */
    for (j = 0; j < 13; j++)
        for (i = 0; i < 20; i++) {
            /* falowanie: przesuniecie w pionie i cieniowanie zaleznie od kolumny */
            WORD ph = (WORD)((i + shown.wave * 5) % 10);
            WORD dy = (WORD)(ph < 5 ? ph / 2 : (9 - ph) / 2);
            UBYTE c;
            if (shown.flag == FLAG_CHECKERED) c = ((i >> 2) ^ (j >> 2)) & 1 ? H_FLAG_K : H_FLAG_W;
            else c = ph >= 5 ? H_FLAG_SH : H_FLAG;
            hplot(hx - 20 + i, hy - 22 + j + dy, c);
        }
}

/* poza biegiem: pusty HUD (sam gradient tla) */
static void enter_menu_mode(void)
{
    hrect(0, 0, SCREEN_W, HUD_H, H_BG);
    shown.menu = 1;
    shown.flag = FLAG_NONE;
    shown.wave = 0;
}

/* wartosci niemozliwe -> hud_update narysuje wszystko od nowa */
static void force_redraw(void)
{
    UBYTE i;
    for (i = 0; i < 4; i++) shown.order[i] = 0xFE;
    shown.lap = shown.lights = shown.flag = 0xFE;
    shown.menu = 0;
}

void hud_init(void)
{
    have_referee = asset_load("PROGDIR:data/referee.bin", referee, sizeof(referee));
    hrect(0, 0, SCREEN_W, HUD_H, H_BG);
    enter_menu_mode();
}

void hud_update(void)
{
    Heat *h = &g_tour.heat;
    UBYTE order[4], i, lap = 0, lights, flag, wave;
    BOOL changed = FALSE;

    if (g_tour.state != GS_HEAT && g_tour.state != GS_HEAT_RESULT) {
        if (!shown.menu) enter_menu_mode();
        return;
    }
    if (shown.menu) force_redraw();

    for (i = 0; i < 4; i++) order[i] = 0xFF;
    for (i = 0; i < MAX_RIDERS; i++) {
        Bike *b = &g_tour.bikes[i];
        if (b->state == BIKE_EXCLUDED) continue;
        order[b->position] = (UBYTE)(b->gate | (b->rider << 4));   /* kask + numer zawodnika */
        if (b->position == 0) lap = b->lap;
    }
    if (lap < 1) lap = 1;
    if (lap > LAPS_PER_HEAT) lap = LAPS_PER_HEAT;

    lights = h->phase == HEAT_RED_LIGHT ? 1 : (h->phase == HEAT_TAPE_UP || h->phase == HEAT_RACING) ? 2 : 0;
    if (h->phase == HEAT_FALSE_START)                       flag = FLAG_RED;
    else if (h->finished_count)                             flag = FLAG_CHECKERED;
    else if (h->phase == HEAT_RACING && lap == LAPS_PER_HEAT) flag = FLAG_WHITE;
    else if (h->phase == HEAT_TAPE_UP)                      flag = FLAG_GREEN;
    else                                                    flag = FLAG_NONE;
    wave = flag ? (UBYTE)((h->phase_timer >> 3) & 1) : 0;

    for (i = 0; i < 4; i++)
        if (order[i] != shown.order[i]) { shown.order[i] = order[i]; changed = TRUE; }
    if (changed) draw_order();
    if (lap != shown.lap) { shown.lap = lap; draw_lap(); }
    if (lights != shown.lights) { shown.lights = lights; draw_lights(); }
    if (flag != shown.flag || wave != shown.wave) { shown.flag = flag; shown.wave = wave; draw_referee(); }
}
