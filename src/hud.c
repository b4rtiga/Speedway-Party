/* hud.c - statyczny ekran HUD (5 bitplanow, 32 kolory, wlasna paleta z Coppera).
 * Kolejnosc wg kaskow, okrazenie lidera, swiatla startowe, sedzia z flaga.
 * Rysowane tylko przy zmianie stanu - HUD nie jest podwojnie buforowany. */
#include "game.h"
#include "video.h"
#include "system.h"

#define HUD_TOP 6               /* pierwszy widoczny wiersz (4 linie przerwy w Copperze) */

enum { H_BG = 0, H_WHITE = 1, H_HELMET = 2, H_RED_ON = 6, H_GREEN_ON = 7, H_RED_OFF = 8, H_GREEN_OFF = 9,
       H_SKIN = 10, H_DARK = 11, H_SKIN_D = 12, H_FLAG_W = 13, H_FLAG_K = 14, H_POLE = 15, H_FRAME = 16,
       H_JACKET_D = 18, H_SHIRT = 19, H_FLAG = 21, H_FLAG_SH = 25 };

/* portret sedziego 32x38, 5 planow przeplatanych (tools/genart.py) */
#define REF_W_BYTES 4
#define REF_H       38
#define REF_X       272
static UBYTE referee[REF_H * HUD_DEPTH * REF_W_BYTES];
static BOOL  have_referee;
/* uniesiona reka z piescia (tools/genreferee.py arm_image): 2 fazy machania, 0 = przezroczyste */
#define ARM_X      248
#define ARM_Y      14                    /* od HUD_TOP */
#define ARM_H      24
#define ARM_BYTES  4
static UBYTE ref_arm[2][ARM_H * HUD_DEPTH * ARM_BYTES];
static BOOL  have_arm;

/* Flagi sedziego (regulamin zuzlowy):
 *   szachownica - koniec biegu (od mety zwyciezcy)   zolta     - niebezpieczenstwo na torze
 *   czerwona    - przerwanie biegu (np. tasma)         czarna    - wykluczenie zawodnika
 *   kosa        - ostatnie okrazenie (zolta z krzyzem) narodowa  - awaryjny start (awaria maszyny)
 * Niebieskiej flagi (dublowanie) w klasycznym zuzlu sie nie uzywa - gra jej nie ma. */
typedef enum { FLAG_NONE = 0, FLAG_RED, FLAG_LAST_LAP, FLAG_CHECKERED, FLAG_YELLOW, FLAG_BLACK,
               FLAG_NATIONAL } Flag;

static struct { UBYTE order[4], lap, lights, flag, wave, menu; UWORD lag; } shown;
#ifdef SHOW_LAG
static UWORD shown_prof[4] = { 999, 999, 999, 999 };   /* diagnostyka: liczby profilu w HUD */
#endif

static void hplot(WORD x, WORD y, UBYTE c)
{
    UBYTE *p = g_video.hud + (LONG)y * HUD_STRIDE + (x >> 3);
    UBYTE bit = (UBYTE)(0x80 >> (x & 7)), pl;
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= HUD_H) return;
    for (pl = 0; pl < HUD_DEPTH; pl++, p += ROW_BYTES, c >>= 1)
        if (c & 1) *p |= bit; else *p &= (UBYTE)~bit;
}

/* Prostokat w HUD bajtami (maski na brzegach) - wielokrotnie szybciej niz piksel po pikselu;
 * HUD nie jest podwojnie buforowany, wiec rysowanie musi zdazyc, zanim wiazka dojdzie do HUD. */
static void hrect(WORD x, WORD y, WORD w, WORD h, UBYTE c)
{
    WORD x1, j, bx0, bx1, bx;
    UBYTE m0, m1;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > HUD_H) h = HUD_H - y;
    if (w <= 0 || h <= 0) return;
    x1 = x + w - 1;
    bx0 = x >> 3; bx1 = x1 >> 3;
    m0 = (UBYTE)(0xFF >> (x & 7));
    m1 = (UBYTE)(0xFF << (7 - (x1 & 7)));
    if (bx0 == bx1) m0 &= m1;
    for (j = 0; j < h; j++) {
        UBYTE *row = g_video.hud + (LONG)(y + j) * HUD_STRIDE, pl, col = c;
        for (pl = 0; pl < HUD_DEPTH; pl++, row += ROW_BYTES, col >>= 1) {
            BOOL set = (BOOL)(col & 1);
            row[bx0] = set ? (UBYTE)(row[bx0] | m0) : (UBYTE)(row[bx0] & ~m0);
            if (bx1 > bx0) {
                for (bx = bx0 + 1; bx < bx1; bx++) row[bx] = set ? 0xFF : 0x00;
                row[bx1] = set ? (UBYTE)(row[bx1] | m1) : (UBYTE)(row[bx1] & ~m1);
            }
        }
    }
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
    /* shown.order: kolor kasku w bitach 0-3, numer zawodnika w bitach 4-7;
     * na ekranie sam kolor kasku (staly kolor zawodnika w turnieju) */
    for (i = 0; i < 4; i++) {
        UBYTE v = shown.order[i];
        hrect(8 + i * 22, HUD_TOP + 8, 18, 18, H_FRAME);
        hrect(10 + i * 22, HUD_TOP + 10, 14, 14, v == 0xFF ? H_BG : (UBYTE)(H_HELMET + (v & 0x0F)));
    }
}

static void draw_lap(void)
{
    hglyph(110, HUD_TOP + 9, shown.lap, H_WHITE);
    hglyph(122, HUD_TOP + 9, 10, H_WHITE);
    hglyph(134, HUD_TOP + 9, LAPS_PER_HEAT, H_WHITE);
}

/* kolo (srodek cx,cy - miedzy pikselami, srednica 2r) liniami poziomymi */
static void hdisc(WORD cx, WORD cy, WORD r, UBYTE c)
{
    WORD dy;
    for (dy = 0; dy < r; dy++) {
        WORD yc = (WORD)(2 * dy + 1 - 2 * r), w = 0;      /* 2 * odleglosc srodka wiersza od srodka */
        while ((2 * w + 2) * (2 * w + 2) + yc * yc <= 4 * r * r) w++;
        hrect(cx - w, cy - r + dy, 2 * w, 1, c);
        hrect(cx - w, cy + r - 1 - dy, 2 * w, 1, c);
    }
}

/* okragla lampa sygnalizatora: obwodka, klosz, odblask gdy swieci */
static void hlamp(WORD cx, WORD cy, BOOL on, UBYTE c_on, UBYTE c_off)
{
    hdisc(cx, cy, 9, H_FRAME);
    hdisc(cx, cy, 7, on ? c_on : c_off);
    if (on) { hrect(cx - 4, cy - 4, 2, 2, H_WHITE); hplot(cx - 2, cy - 5, H_WHITE); }
}

static void draw_lights(void)
{
#ifdef SHOW_LAG
    return;                              /* diagnostyka: w miejscu swiatel profil */
#endif
    if (shown.lights == 3) {             /* w biegu swiatla juz niepotrzebne - tlo HUD */
        hrect(171, HUD_TOP + 7, 45, 21, 0);
        return;
    }
    hlamp(181, HUD_TOP + 17, shown.lights == 1, H_RED_ON, H_RED_OFF);
    hlamp(205, HUD_TOP + 17, shown.lights == 2, H_GREEN_ON, H_GREEN_OFF);
}

/* odcinek reki grubosci 3 px: najpierw cien przesuniety w prawo-dol, potem wierzch -
 * rekaw w czarno-biale pasy albo opalona skora */
static void arm_segment(WORD x0, WORD y0, WORD x1, WORD y1, BOOL sleeve)
{
    WORD dx = (WORD)(x0 > x1 ? x0 - x1 : x1 - x0), dy = (WORD)(y0 > y1 ? y0 - y1 : y1 - y0);
    WORD n = dx > dy ? dx : dy, i;
    UBYTE pass;
    if (!n) n = 1;
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i <= n; i++) {
            WORD x = (WORD)(x0 + (x1 - x0) * i / n), y = (WORD)(y0 + (y1 - y0) * i / n);
            if (!pass) hrect(x, y, 3, 3, sleeve ? H_FLAG_K : H_SKIN_D);
            else hrect(x - 1, y - 1, 3, 3, sleeve ? (((x >> 1) & 1) ? H_FLAG_K : H_SHIRT) : H_SKIN);
        }
}

#define ABSW(v) ((v) < 0 ? -(v) : (v))

/* "Kosa" (zolta flaga z czarnym krzyzem, ostatnie okrazenie): sedzia macha nia od chwili,
 * gdy prowadzacy na 3. okrazeniu wjezdza na prosta startowa (wyjscie z ostatniego luku),
 * do 2 s po tym, jak ostatni jadacy zawodnik przetnie linie konczaca 3. okrazenie. */
static BOOL kosa_shown(void)
{
    const Heat *h = &g_tour.heat;
    static UWORD all_in_frame;          /* klatka, w ktorej ostatni zaczal 4. okrazenie */
    BOOL all_in = TRUE;
    UBYTE i;
    if (race_leader_distance() < race_straight_entry(LAPS_PER_HEAT)) {   /* lider jeszcze przed prosta */
        all_in_frame = 0;
        return FALSE;
    }
    for (i = 0; i < MAX_RIDERS; i++) {
        const Bike *b = &g_tour.bikes[i];
        if (b->state < BIKE_EXCLUDED && !b->empty && b->lap < LAPS_PER_HEAT) all_in = FALSE;
    }
    if (!all_in) { all_in_frame = 0; return TRUE; }
    if (!all_in_frame) all_in_frame = (UWORD)(h->race_frames ? h->race_frames : 1);
    return (BOOL)(h->race_frames - all_in_frame < 2 * FPS);
}

static void set_flag_color(UBYTE flag)
{
    static const UBYTE rgb[7][3] = { {0,0,0}, {240,30,30}, {255,215,0}, {250,250,250},
                                     {255,215,0}, {28,28,32}, {250,250,250} };
    video_set_hud_color(H_FLAG, rgb[flag][0], rgb[flag][1], rgb[flag][2]);
    video_set_hud_color(H_FLAG_SH, (UBYTE)(rgb[flag][0] * 3 / 5), (UBYTE)(rgb[flag][1] * 3 / 5),
                        (UBYTE)(rgb[flag][2] * 3 / 5));
}

/* ------------------------------------------------------------------
 * Twarz sedziego w spoczynku: mruganie i rzut oka na boki (zrenice w szklach okularow).
 * Przerysowywany tylko fragment twarzy (wiersze FACE_Y0..FACE_Y1 portretu, 2 bajty
 * szerokosci) i tylko przy zmianie stanu - koszt pomijalny. Wspolrzedne szkiel jak
 * LENS_X / LENS_Y w tools/genreferee.py: x 10-13 i 18-21, wiersze 12-13.
 * ------------------------------------------------------------------ */
#define FACE_Y0  12
#define FACE_Y1  13
#define FACE_B0  1                   /* bajty portretu: piksele x 8..23 */
#define FACE_B1  2
#define H_LENS   22                  /* kolor szkiel (paleta HUD) */
enum { EYE_OPEN = 0, EYE_LEFT, EYE_RIGHT, EYE_SHUT };
static UBYTE face_drawn = 0xFF;      /* stan oczu; 0xFF = do narysowania */
static UWORD blink_at = 120, glance_at = 300, face_clock;
static UBYTE eyes_state, glance_left;

static void face_restore(void)
{
    WORD y;
    UBYTE pl, k;
    for (y = FACE_Y0; y <= FACE_Y1; y++)
        for (pl = 0; pl < HUD_DEPTH; pl++)
            for (k = FACE_B0; k <= FACE_B1; k++)
                g_video.hud[(LONG)(HUD_TOP + y) * HUD_STRIDE + pl * ROW_BYTES + REF_X / 8 + k] =
                    referee[(y * HUD_DEPTH + pl) * REF_W_BYTES + k];
}

static void face_draw(UBYTE eyes)
{
    static const BYTE lens_x[2] = { 10, 18 };
    UBYTE e;
    WORD x0 = REF_X, y0 = HUD_TOP;
    face_restore();
    for (e = 0; e < 2; e++) {
        WORD lx = (WORD)(x0 + lens_x[e]);
        if (eyes == EYE_SHUT) {                          /* powieka: kreska w szkle */
            WORD i;
            for (i = 0; i < 4; i++) { hplot(lx + i, y0 + FACE_Y0, H_LENS); hplot(lx + i, y0 + FACE_Y1, H_DARK); }
        } else {                                         /* zrenica: srodek albo bok szkla */
            WORD px = (WORD)(lx + (eyes == EYE_LEFT ? 0 : eyes == EYE_RIGHT ? 3 : 1));
            hplot(px, y0 + FACE_Y1, H_DARK);
            if (eyes == EYE_OPEN) hplot(px + 1, y0 + FACE_Y1, H_DARK);
        }
    }
}

/* raz na klatke, gdy widac sedziego */
static void face_update(void)
{
    if (!have_referee) return;
    face_clock++;
    if (face_clock == blink_at) eyes_state = EYE_SHUT;
    if (face_clock == (UWORD)(blink_at + 4)) {
        eyes_state = EYE_OPEN;
        blink_at = (UWORD)(face_clock + rng_range(150, 300));      /* co 3-6 s */
    }
    if (face_clock == glance_at && eyes_state != EYE_SHUT) {
        glance_left = (UBYTE)(rng_next() & 1);
        eyes_state = glance_left ? EYE_LEFT : EYE_RIGHT;
    }
    if (face_clock == (UWORD)(glance_at + 40)) {
        if (eyes_state != EYE_SHUT) eyes_state = EYE_OPEN;
        glance_at = (UWORD)(face_clock + rng_range(200, 450));
    }
    if (eyes_state != face_drawn) { face_drawn = eyes_state; face_draw(eyes_state); }
}

/* Sedzia: portret z pliku + reka z machajaca flaga. Kolor flagi = wpis palety HUD
 * (zmiana jednego koloru w Copperze), szachownica rysowana kolorami 13/14. */
static void draw_referee(void)
{
    WORD hx = 256, hy = HUD_TOP + (shown.wave ? 19 : 21);  /* piesc na drzewcu; flaga od hy-19 */
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
        hrect(272, HUD_TOP + 26, 28, 12, H_FLAG_K);
    }
    if (shown.flag == FLAG_NONE) return;

    set_flag_color(shown.flag);
    /* reka w gorze: rekaw w pasy od barku do lokcia, opalone przedramie, piesc na drzewcu */
    hrect(hx, hy - 19, 2, 25, H_POLE);                            /* drzewce */
    if (have_arm) {
        /* malowana reka (jak portret) z maska: piksele 0 zostawiaja tlo i drzewce */
        const UBYTE *src = ref_arm[shown.wave & 1];
        for (y = 0; y < ARM_H; y++) {
            UBYTE k;
            for (k = 0; k < ARM_BYTES; k++) {
                UBYTE m = 0, pl;
                UBYTE *d = g_video.hud + (LONG)(HUD_TOP + ARM_Y + y) * HUD_STRIDE + ARM_X / 8 + k;
                for (pl = 0; pl < HUD_DEPTH; pl++) m |= src[(y * HUD_DEPTH + pl) * ARM_BYTES + k];
                if (!m) continue;
                for (pl = 0; pl < HUD_DEPTH; pl++, d += ROW_BYTES)
                    *d = (UBYTE)((*d & ~m) | src[(y * HUD_DEPTH + pl) * ARM_BYTES + k]);
            }
        }
    } else {                                                      /* bez pliku: reka kreskami */
        arm_segment(264, HUD_TOP + 31, hx + 2, hy + 2, FALSE);    /* przedramie */
        arm_segment(276, HUD_TOP + 31, 265, HUD_TOP + 32, TRUE);  /* ramie (rekaw) */
        hrect(hx - 1, hy - 1, 4, 4, H_SKIN);                      /* piesc */
        hrect(hx - 1, hy + 2, 4, 1, H_SKIN_D);
        hplot(hx + 2, hy - 1, H_SKIN_D); hplot(hx + 2, hy, H_SKIN_D);
    }
    for (j = 0; j < 13; j++)
        for (i = 0; i < 20; i++) {
            /* falowanie: przesuniecie w pionie i cieniowanie zaleznie od kolumny */
            WORD ph = (WORD)((i + shown.wave * 5) % 10);
            WORD dy = (WORD)(ph < 5 ? ph / 2 : (9 - ph) / 2);
            UBYTE c;
            if (shown.flag == FLAG_CHECKERED) c = ((i >> 2) ^ (j >> 2)) & 1 ? H_FLAG_K : H_FLAG_W;
            else if (shown.flag == FLAG_NATIONAL) c = j < 7 ? H_FLAG_W : H_RED_ON;     /* bialo-czerwona */
            else if (shown.flag == FLAG_LAST_LAP &&                  /* "kosa": zolta z czarnym krzyzem */
                     (ABSW(j - i * 12 / 19) <= 1 || ABSW(j - (12 - i * 12 / 19)) <= 1)) c = H_FLAG_K;
            else c = ph >= 5 ? H_FLAG_SH : H_FLAG;
            hplot(hx - 20 + i, hy - 19 + j + dy, c);
        }
}

/* Rog sedziego (x 224..319, wiersze HUD_TOP..+37) dla obu faz machania: zapis z HUD do bufora
 * (save) albo z bufora do HUD. Kopia dlugimi slowami - ulamek kosztu rysowania. */
#define RC_X0    224
#define RC_LONGS ((SCREEN_W - RC_X0) / 32)              /* 3 dlugie slowa na plan */
#define RC_ROWS  REF_H
#define RC_FLAGS (FLAG_NATIONAL + 1)
static ULONG ref_cache[RC_FLAGS][2][RC_ROWS * HUD_DEPTH * RC_LONGS];   /* kazda flaga x faza machania */

static void ref_cache_io(UBYTE flag, UBYTE wave, BOOL save)
{
    ULONG *c = ref_cache[flag < RC_FLAGS ? flag : 0][wave & 1];
    WORD y;
    UBYTE pl, k;
    for (y = 0; y < RC_ROWS; y++)
        for (pl = 0; pl < HUD_DEPTH; pl++) {
            ULONG *h = (ULONG *)(g_video.hud + (LONG)(HUD_TOP + y) * HUD_STRIDE + pl * ROW_BYTES + RC_X0 / 8);
            for (k = 0; k < RC_LONGS; k++, c++) {
                if (save) *c = h[k]; else h[k] = *c;
            }
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

COLD void hud_init(void)
{
    have_referee = asset_load("PROGDIR:data/referee.bin", referee, sizeof(referee));
    have_arm = asset_load("PROGDIR:data/refarm.bin", ref_arm, sizeof(ref_arm));
    {
        UBYTE f, w;                                      /* wszystkie flagi x fazy: raz, przed gra */
        for (f = 0; f < RC_FLAGS; f++)
            for (w = 0; w < 2; w++) {
                shown.flag = f; shown.wave = w;
                draw_referee();
                ref_cache_io(f, w, TRUE);
            }
    }
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
    {
        /* kolejnosc po pozycjach, bez wykluczonych i pustych pol - kwadraty od lewej bez dziur */
        UBYTE pos, n = 0;
        for (pos = 0; pos < MAX_RIDERS; pos++) {
            const Bike *b = &g_tour.bikes[h->order[pos]];
            if (b->state >= BIKE_EXCLUDED || b->empty) continue;
            if (n == 0) lap = b->lap;
            order[n++] = (UBYTE)(b->rider | (b->rider << 4));      /* kolor kasku = zawodnik */
        }
    }
    if (lap < 1) lap = 1;
    if (lap > LAPS_PER_HEAT) lap = LAPS_PER_HEAT;

    /* 1 czerwone, 2 zielone, 3 schowane: 2 s po starcie i po biegu (w biegu juz niepotrzebne) */
    lights = (h->phase == HEAT_FINISHED || h->phase == HEAT_REPLAY || (h->phase == HEAT_RACING && h->race_frames >= 2 * FPS) ||
              (h->phase == HEAT_FALSE_START && h->stop_fall)) ? 3 :            /* upadek: bieg juz trwal */
             h->emergency ? 0 :                              /* awaria: swiatla nie dzialaja */
             h->phase == HEAT_RED_LIGHT ? 1 : (h->phase == HEAT_TAPE_UP || h->phase == HEAT_RACING) ? 2 : 0;
    flag = FLAG_NONE;
    wave = (UBYTE)((h->phase_timer >> 4) & 1);       /* machniecie co 16 klatek */
    if (h->phase == HEAT_FALSE_START || race_fall_active()) {
        /* czerwona: przerwanie biegu; potem czarna, jesli tasma skonczyla sie wykluczeniem */
        BOOL excl = h->false_starter < MAX_RIDERS && g_tour.bikes[h->false_starter].state == BIKE_EXCLUDED;
        flag = (excl && h->phase_timer > 75) ? FLAG_BLACK : FLAG_RED;
    } else if (h->finished_count ||
               ((h->phase == HEAT_RACING || h->phase == HEAT_FINISHED) && race_leader_distance() >= race_straight_entry(LAPS_PER_HEAT + 1))) {
        flag = FLAG_CHECKERED;                       /* od wjazdu lidera na ostatnia prosta do konca biegu */
    } else if (h->danger_timer) {
        flag = FLAG_YELLOW;
    } else if (h->phase == HEAT_RACING || h->phase == HEAT_TAPE_UP) {
        BOOL kosa = kosa_shown();                    /* liczy czas okna w kazdej klatce */
        if (kosa) flag = FLAG_LAST_LAP;              /* ostatnie okrazenie: "kosa" */
        if (flag == FLAG_NONE && h->emergency && h->race_frames < 60) flag = FLAG_NATIONAL;   /* machniecie = start */
    } else if (h->emergency && (h->phase == HEAT_READY || h->phase == HEAT_RED_LIGHT)) {
        flag = FLAG_NATIONAL;                                /* sedzia trzyma flage w gorze */
        wave = 0;
    }
    if (!flag) wave = 0;

    for (i = 0; i < 4; i++)
        if (order[i] != shown.order[i]) { shown.order[i] = order[i]; changed = TRUE; }
    if (changed) draw_order();
    if (lap != shown.lap) { shown.lap = lap; draw_lap(); }
    if (lights != shown.lights) { shown.lights = lights; draw_lights(); }
#ifdef SHOW_LAG
    {
        /* profil (linie wiazki, pod swiatlami): logika, rysowanie toru, duszki+HUD+dzwiek, Blitter */
        extern UWORD g_prof[4];
        UBYTE k;
        for (k = 0; k < 4; k++)
            if (g_prof[k] != shown_prof[k]) {
                UWORD v = (UWORD)(g_prof[k] % 1000);
                WORD x = (WORD)(146 + k * 26);
                shown_prof[k] = g_prof[k];
                hglyph_s(x, HUD_TOP + 21, (UBYTE)(v / 100), H_WHITE, 2, H_BG);
                hglyph_s(x + 8, HUD_TOP + 21, (UBYTE)(v / 10 % 10), H_WHITE, 2, H_BG);
                hglyph_s(x + 16, HUD_TOP + 21, (UBYTE)(v % 10), H_WHITE, 2, H_BG);
            }
    }
    /* diagnostyka: zgubione klatki w biegu (gra nie zdazyla w 1/50 s) - 3 cyfry obok okrazen */
    if (g_lag != shown.lag) {
        UWORD v = (UWORD)(g_lag % 1000);
        shown.lag = (UWORD)g_lag;
        hglyph_s(146, HUD_TOP + 9, (UBYTE)(v / 100), H_RED_ON, 2, H_BG);
        hglyph_s(154, HUD_TOP + 9, (UBYTE)(v / 10 % 10), H_RED_ON, 2, H_BG);
        hglyph_s(162, HUD_TOP + 9, (UBYTE)(v % 10), H_RED_ON, 2, H_BG);
    }
#endif
    if (flag != shown.flag || wave != shown.wave) {
        /* rog sedziego z kazda flaga w obu fazach machania narysowany raz przy starcie (hud_init) -
         * w biegu zmiana flagi i machniecie to sama kopia (rysowanie piksel po pikselu gubilo klatke) */
        if (flag != FLAG_NONE && flag != shown.flag) set_flag_color(flag);
        shown.flag = flag; shown.wave = wave;
        ref_cache_io(flag, wave, FALSE);
        face_drawn = 0xFF;                               /* portret przerysowany - twarz od nowa */
#ifdef SHOW_LAG
        shown_prof[3] = 999;                             /* rog sedziego zakryl 4. liczbe profilu */
#endif
    }
    face_update();
}
