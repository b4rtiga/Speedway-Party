/* hud.c - statyczny ekran HUD (5 bitplanow, 32 kolory, wlasna paleta z Coppera).
 * Kolejnosc wg kaskow, okrazenie lidera, swiatla startowe, sedzia z flaga.
 * Rysowane tylko przy zmianie stanu - HUD nie jest podwojnie buforowany. */
#include "game.h"
#include "video.h"
#include "system.h"

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

/* Flagi sedziego (regulamin zuzlowy):
 *   szachownica - koniec biegu (od mety zwyciezcy)   zolta     - niebezpieczenstwo na torze
 *   czerwona    - przerwanie biegu (np. tasma)         czarna    - wykluczenie zawodnika
 *   biala       - ostatnie okrazenie lidera            niebieska - zaraz zostaniesz zdublowany
 *   narodowa    - awaryjny start (awaria maszyny startowej i swiatel) */
typedef enum { FLAG_NONE = 0, FLAG_RED, FLAG_LAST_LAP, FLAG_CHECKERED, FLAG_YELLOW, FLAG_BLUE, FLAG_BLACK,
               FLAG_NATIONAL } Flag;
#define LAP_WARN 90                     /* niebieska: strata do lidera > okrazenie - 90 px */

static struct { UBYTE order[4], lap, lights, flag, wave, menu; UWORD lag; } shown;

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
    /* shown.order: kolor kasku (pole) w bitach 0-3, numer zawodnika w bitach 4-7;
     * na ekranie sam kolor kasku (kolor = pole startowe, jak na zuzlu) */
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

static void draw_lights(void)
{
#ifdef SHOW_LAG
    return;                              /* diagnostyka: w miejscu swiatel profil */
#endif
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

#define ABSW(v) ((v) < 0 ? -(v) : (v))

/* najdalej jadacy zawodnik (dystans liczony od tasmy: okrazenia * dlugosc + pozycja) */
static LONG leader_distance(void)
{
    LONG lead = 0;
    UBYTE i;
    for (i = 0; i < MAX_RIDERS; i++) {
        const Bike *b = &g_tour.bikes[i];
        if (b->state == BIKE_EXCLUDED || b->empty) continue;
        if (b->race_distance > lead) lead = b->race_distance;
    }
    return lead;
}

/* dystans, na ktorym lider jadacy okrazenie (lap - 1) wjezdza na prosta startowa (wyjscie z ostatniego luku) */
static LONG straight_entry(UBYTE lap)
{
    const Track *t = &g_tour.track;
    return (LONG)(lap - 1) * t->lap_length + (t->lap_length - t->tape_s);
}

/* "Kosa" (zolta flaga z czarnym krzyzem, ostatnie okrazenie): sedzia macha nia od chwili,
 * gdy prowadzacy na 3. okrazeniu wjezdza na prosta startowa (wyjscie z ostatniego luku),
 * do 2 s po tym, jak ostatni jadacy zawodnik przetnie linie konczaca 3. okrazenie. */
static BOOL kosa_shown(void)
{
    const Track *t = &g_tour.track;
    const Heat *h = &g_tour.heat;
    static UWORD all_in_frame;          /* klatka, w ktorej ostatni zaczal 4. okrazenie */
    LONG lead = 0, entry = (LONG)(LAPS_PER_HEAT - 1) * t->lap_length + (t->lap_length - t->tape_s);
    BOOL all_in = TRUE;
    UBYTE i;
    for (i = 0; i < MAX_RIDERS; i++) {
        const Bike *b = &g_tour.bikes[i];
        if (b->state == BIKE_EXCLUDED || b->empty) continue;
        if (b->race_distance > lead) lead = b->race_distance;
        if (b->lap < LAPS_PER_HEAT) all_in = FALSE;
    }
    if (lead < entry) { all_in_frame = 0; return FALSE; }      /* lider jeszcze przed prosta */
    if (!all_in) { all_in_frame = 0; return TRUE; }
    if (!all_in_frame) all_in_frame = (UWORD)(h->race_frames ? h->race_frames : 1);
    return (BOOL)(h->race_frames - all_in_frame < 2 * FPS);
}

static void set_flag_color(UBYTE flag)
{
    static const UBYTE rgb[8][3] = { {0,0,0}, {240,30,30}, {255,215,0}, {250,250,250},
                                     {255,215,0}, {30,90,255}, {28,28,32}, {250,250,250} };
    video_set_hud_color(H_FLAG, rgb[flag][0], rgb[flag][1], rgb[flag][2]);
    video_set_hud_color(H_FLAG_SH, (UBYTE)(rgb[flag][0] * 3 / 5), (UBYTE)(rgb[flag][1] * 3 / 5),
                        (UBYTE)(rgb[flag][2] * 3 / 5));
}

/* ------------------------------------------------------------------
 * Twarz sedziego w spoczynku: mruganie, rzut oka na boki, czasem babel z nosa.
 * Przerysowywany tylko fragment twarzy (wiersze FACE_Y0..FACE_Y1 portretu, 2 bajty
 * szerokosci) i tylko przy zmianie stanu - koszt pomijalny. Wspolrzedne z REFEREE_HEAD
 * w tools/genart.py: szkla okularow x 9-12 i 17-20, wiersze 9-10; nos x 15-16, wiersz 13.
 * ------------------------------------------------------------------ */
#define FACE_Y0  8
#define FACE_Y1  21
#define FACE_B0  1                   /* bajty portretu: piksele x 8..23 */
#define FACE_B1  2
#define H_LENS   22                  /* kolor szkiel (paleta HUD) */
enum { EYE_OPEN = 0, EYE_LEFT, EYE_RIGHT, EYE_SHUT };
static UBYTE face_drawn = 0xFF;      /* eyes | bubble << 2; 0xFF = do narysowania */
static UWORD blink_at = 120, glance_at = 300, bubble_at = 1200, face_clock;
static UBYTE eyes_state, glance_left, bubble_phase;   /* bubble_phase: 0 brak, 1..4 rosnie, 5 peka */

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

static void face_draw(UBYTE eyes, UBYTE bubble)
{
    static const BYTE lens_x[2] = { 9, 17 };
    UBYTE e;
    WORD x0 = REF_X, y0 = HUD_TOP;
    face_restore();
    for (e = 0; e < 2; e++) {
        WORD lx = (WORD)(x0 + lens_x[e]);
        if (eyes == EYE_SHUT) {                          /* powieka: kreska w szkle */
            WORD i;
            for (i = 0; i < 4; i++) { hplot(lx + i, y0 + 9, H_LENS); hplot(lx + i, y0 + 10, H_DARK); }
        } else {                                         /* zrenica: srodek albo bok szkla */
            WORD px = (WORD)(lx + (eyes == EYE_LEFT ? 0 : eyes == EYE_RIGHT ? 3 : 1));
            hplot(px, y0 + 10, H_DARK);
            if (eyes == EYE_OPEN) hplot(px + 1, y0 + 10, H_DARK);
        }
    }
    if (bubble) {                                        /* babel z dziurki nosa: rosnie w dol */
        static const BYTE rad[6] = { 0, 1, 2, 2, 3, 0 };
        WORD cx = x0 + 16, cy = y0 + 13, r = rad[bubble], dx, dy;
        if (bubble == 5) {                               /* pekniecie: kilka kropli */
            hplot(cx - 3, cy + 2, H_WHITE); hplot(cx + 3, cy + 2, H_WHITE);
            hplot(cx - 2, cy + 5, H_WHITE); hplot(cx + 2, cy + 5, H_WHITE);
        } else {
            cy += r;                                     /* wisi pod nosem */
            for (dy = -r; dy <= r; dy++)
                for (dx = -r; dx <= r; dx++) {
                    WORD d2 = (WORD)(dx * dx + dy * dy);
                    if (d2 > r * r + r) continue;            /* poza babelkiem */
                    hplot(cx + dx, cy + dy, d2 > (r - 1) * (r - 1) + (r - 1) ? H_WHITE : H_FRAME);
                }
            if (r >= 2) hplot(cx - r / 2, cy - r / 2, H_WHITE);   /* blysk */
        }
    }
}

/* raz na klatke, gdy widac sedziego; calm = nie macha flaga (babel tylko wtedy) */
static void face_update(BOOL calm)
{
    UBYTE want;
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
    if (face_clock >= bubble_at && !bubble_phase && calm) bubble_phase = 1;
    if (bubble_phase) {
        UWORD t = (UWORD)(face_clock - bubble_at);
        bubble_phase = (UBYTE)(t < 10 ? 1 : t < 22 ? 2 : t < 34 ? 3 : t < 70 ? 4 : t < 74 ? 5 : 0);
        if (!bubble_phase) bubble_at = (UWORD)(face_clock + rng_range(1000, 2000));   /* co 20-40 s */
    } else if (face_clock >= bubble_at) {
        bubble_at = (UWORD)(face_clock + 100);           /* flaga w gorze - sprobuj pozniej */
    }
    want = (UBYTE)(eyes_state | (bubble_phase << 2));
    if (want != face_drawn) { face_drawn = want; face_draw(eyes_state, bubble_phase); }
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
            else if (shown.flag == FLAG_NATIONAL) c = j < 7 ? H_FLAG_W : H_RED_ON;     /* bialo-czerwona */
            else if (shown.flag == FLAG_LAST_LAP &&                  /* "kosa": zolta z czarnym krzyzem */
                     (ABSW(j - i * 12 / 19) <= 1 || ABSW(j - (12 - i * 12 / 19)) <= 1)) c = H_FLAG_K;
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
    {
        /* kolejnosc po pozycjach, bez wykluczonych i pustych pol - kwadraty od lewej bez dziur */
        UBYTE pos, n = 0;
        for (pos = 0; pos < MAX_RIDERS; pos++)
            for (i = 0; i < MAX_RIDERS; i++) {
                Bike *b = &g_tour.bikes[i];
                if (b->position != pos || b->state == BIKE_EXCLUDED || b->empty) continue;
                if (n == 0) lap = b->lap;
                order[n++] = (UBYTE)(b->gate | (b->rider << 4));   /* kask + numer zawodnika */
            }
    }
    if (lap < 1) lap = 1;
    if (lap > LAPS_PER_HEAT) lap = LAPS_PER_HEAT;

    lights = h->emergency ? 0 :                              /* awaria: swiatla nie dzialaja */
             h->phase == HEAT_RED_LIGHT ? 1 : (h->phase == HEAT_TAPE_UP || h->phase == HEAT_RACING) ? 2 : 0;
    flag = FLAG_NONE;
    wave = (UBYTE)((h->phase_timer >> 4) & 1);       /* machniecie co 16 klatek */
    if (h->phase == HEAT_FALSE_START) {
        /* czerwona: przerwanie biegu; potem czarna, jesli tasma skonczyla sie wykluczeniem */
        BOOL excl = h->false_starter < MAX_RIDERS && g_tour.bikes[h->false_starter].state == BIKE_EXCLUDED;
        flag = (excl && h->phase_timer > 75) ? FLAG_BLACK : FLAG_RED;
    } else if (h->finished_count ||
               ((h->phase == HEAT_RACING || h->phase == HEAT_FINISHED) && leader_distance() >= straight_entry(LAPS_PER_HEAT + 1))) {
        flag = FLAG_CHECKERED;                       /* od wjazdu lidera na ostatnia prosta do konca biegu */
    } else if (h->danger_timer) {
        flag = FLAG_YELLOW;
    } else if (h->phase == HEAT_RACING || h->phase == HEAT_TAPE_UP) {
        BOOL kosa = kosa_shown();                    /* liczy czas okna w kazdej klatce */
        LONG lead = 0;
        for (i = 0; i < MAX_RIDERS; i++)
            if (g_tour.bikes[i].state == BIKE_RACING && g_tour.bikes[i].race_distance > lead)
                lead = g_tour.bikes[i].race_distance;
        for (i = 0; i < MAX_RIDERS; i++)
            if (g_tour.bikes[i].state == BIKE_RACING &&
                lead - g_tour.bikes[i].race_distance > g_tour.track.lap_length - LAP_WARN)
                flag = FLAG_BLUE;                            /* ktos zaraz zostanie zdublowany */
        if (flag == FLAG_NONE && kosa) flag = FLAG_LAST_LAP;   /* ostatnie okrazenie: "kosa" */
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
        /* profil (linie wiazki): logika, rysowanie, sprite+HUD+dzwiek, razem */
        extern UWORD g_prof[4];
        static UWORD shown_prof[4] = { 999, 999, 999, 999 };
        UBYTE k;
        for (k = 0; k < 4; k++)
            if (g_prof[k] != shown_prof[k]) {
                UWORD v = (UWORD)(g_prof[k] % 1000);
                WORD x = (WORD)(176 + k * 26);
                shown_prof[k] = g_prof[k];
                hglyph_s(x, HUD_TOP + 9, (UBYTE)(v / 100), H_WHITE, 2, H_BG);
                hglyph_s(x + 8, HUD_TOP + 9, (UBYTE)(v / 10 % 10), H_WHITE, 2, H_BG);
                hglyph_s(x + 16, HUD_TOP + 9, (UBYTE)(v % 10), H_WHITE, 2, H_BG);
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
        shown.flag = flag; shown.wave = wave; draw_referee();
        face_drawn = 0xFF;                               /* portret przerysowany - twarz od nowa */
    }
    face_update((BOOL)(flag == FLAG_NONE));
}
