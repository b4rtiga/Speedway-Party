/* track.c - geometria owalu (stadion), mapa przyczepnosci, generowanie tla i palet.
 *
 * Owal = dwie proste + dwa luki. Jazda przeciwnie do ruchu wskazowek zegara
 * (lewe skrety): prosta startowa na dole w prawo, luk prawy w gore, prosta
 * przeciwlegla w lewo, luk lewy w dol. Kat 0 = wschod, 64 = poludnie (y w dol). */
#include <exec/memory.h>
#include <proto/exec.h>

#include "game.h"
#include "video.h"
#include "text.h"
#include "blit.h"

/* Tory - MUSZA byc zgodne z PRESETS w tools/genart.py (kolejnosc i liczby).
 * Ograniczenia ekranu: HALF_L + R_OUT <= 156 (szerokosc), R_OUT <= 96 (motocykl
 * przy bandzie nie moze wejsc w HUD od linii 208). */
typedef struct {
    const char *name;
    WORD half_l, r_in, r_out;    /* pol prostej, kraweznik, banda */
    BYTE grip_adj;               /* przyczepnosc wzgledem granitu */
    BYTE deep;                   /* poczatek odsypiska pod banda (0..39, mniej = szersze) */
} TrackDef;

static const TrackDef track_defs[NUM_TRACKS] = {
    { "OWAL",    60, 40, 96,   0, 33 },     /* klasyczny owal z bakalaru */
};
static const TrackDef *def = &track_defs[0];

#define CX      160
#define CY      100
#define HALF_L  (def->half_l)
#define R_IN    (def->r_in)
#define R_OUT   (def->r_out)
#define R_MID   ((R_IN + R_OUT) / 2)
#define GATE_FIRST 7        /* pole A: 7 px od kraweznika */
#define TAPE_SHIFT 16       /* linia startu 16 px (5% ekranu) w prawo od srodka prostej - jak genart.py */
#define GATE_STEP  ((R_OUT - R_IN - 2 * GATE_FIRST) / 3)
#define PI_RMID ((LONG)R_MID * 355 / 113)

/* tla wszystkich torow w Chip RAM (Blitter kopiuje z nich) i ich palety */
static UBYTE *track_bg[NUM_TRACKS];
static RGB24  track_pal[NUM_TRACKS][2][256];     /* [tor][sucho/mokro] */

const char *track_name(UBYTE type) { return track_defs[type % NUM_TRACKS].name; }

/* indeksy palety pola gry */
#define C_LINE      1
#define C_TRACK     16      /* 16..47: 4 strefy x 8 odcieni granitu */
#define C_GRASS     64      /* 64..71 */
#define C_WALL      80      /* 80..87 bandy reklamowe */
#define C_KERB      88      /* 88..89 */

static UWORD isqrt32(ULONG v)
{
    ULONG r = 0, b = 1UL << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return (UWORD)r;
}

/* odleglosc od "kregoslupa" owalu (odcinek miedzy srodkami lukow) */
static UWORD radial(WORD x, WORD y)
{
    WORD dx = x - CX, ry = y - CY;
    if (dx > HALF_L) dx -= HALF_L; else if (dx < -HALF_L) dx += HALF_L; else dx = 0;
    return isqrt32((ULONG)((LONG)dx * dx + (LONG)ry * ry));
}

WORD track_lateral(WORD x, WORD y) { return (WORD)radial(x, y) - R_MID; }

/* Wektor od osi owalu do punktu (8.8 px). */
static void radial88(fix16 x, fix16 y, LONG *sx, LONG *dx, LONG *dy)
{
    LONG px = x >> 8, py = y >> 8;
    LONG lo = (LONG)(CX - HALF_L) << 8, hi = (LONG)(CX + HALF_L) << 8;
    *sx = px < lo ? lo : px > hi ? hi : px;
    *dx = px - *sx;
    *dy = py - ((LONG)CY << 8);
}

/* Przesuniecie w poprzek toru na zadana odleglosc od osi (bez ruchu wzdluz toru).
 * Uzywane przy odbiciu od bandy i kraweznika. */
void track_snap_lateral(fix16 *x, fix16 *y, WORD lat)
{
    LONG sx, dx, dy, r, t = (LONG)(R_MID + lat) << 8;
    radial88(*x, *y, &sx, &dx, &dy);
    r = (LONG)isqrt32((ULONG)(dx * dx + dy * dy));
    if (!r) return;
    *x = (sx + dx * t / r) << 8;
    *y = (((LONG)CY << 8) + dy * t / r) << 8;
}

/* Promien luku w punkcie s (dla linii lat), 0 na prostej. */
WORD track_bend_radius(LONG s, WORD lat)
{
    LONG len = g_tour.track.lap_length;
    s %= len; if (s < 0) s += len;
    if ((s >= 2 * HALF_L && s < 2 * HALF_L + PI_RMID) || s >= 4 * HALF_L + PI_RMID)
        return (WORD)(R_MID + lat);
    return 0;
}

/* Kierunek jazdy rownolegly do toru (przeciwnie do wskazowek zegara). */
angle8 track_tangent(fix16 x, fix16 y)
{
    LONG sx, dx, dy;
    radial88(x, y, &sx, &dx, &dy);
    return atan2_8(-dx, dy);
}

GripZone track_zone_at(WORD x, WORD y)
{
    UWORD r = radial(x, y);
    if (r < R_IN || r >= R_OUT) return GRIP_WALL;
    r = (UWORD)((r - R_IN) * 40 / (R_OUT - R_IN));   /* 0..39 niezaleznie od szerokosci */
    if (r < 10) return GRIP_INNER;
    if (r < 25) return GRIP_MIDDLE;
    if (r < def->deep) return GRIP_OUTER;
    return GRIP_DEEP_OUTER;                     /* odsypisko pod banda */
}

angle8 atan2_8(LONG dy, LONG dx)
{
    LONG ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    LONG r, a;
    BOOL swap = ay > ax;
    if (ax == 0 && ay == 0) return 0;
    r = swap ? (ax << 8) / ay : (ay << 8) / ax;          /* 0..256 */
    a = (r >> 3) + ((11 * r * (256 - r)) >> 16);         /* 0..32 = 0..45 st. */
    if (swap) a = 64 - a;
    if (dx < 0) a = 128 - a;
    if (dy < 0) a = -a;
    return (angle8)a;
}

LONG track_progress(WORD x, WORD y)
{
    WORD dx = x - CX;
    LONG s;
    if (dx >= -HALF_L && dx <= HALF_L) {
        if (y >= CY) s = dx + HALF_L;                                   /* prosta startowa */
        else         s = 2 * HALF_L + PI_RMID + (HALF_L - dx);          /* przeciwlegla */
    } else if (dx > HALF_L) {
        UBYTE a = (UBYTE)(64 - atan2_8(y - CY, x - (CX + HALF_L)));
        if (a > 128) a = (y < CY) ? 128 : 0;
        s = 2 * HALF_L + (LONG)a * PI_RMID / 128;
    } else {
        UBYTE a = (UBYTE)(192 - atan2_8(y - CY, x - (CX - HALF_L)));
        if (a > 128) a = (y > CY) ? 128 : 0;
        s = 4 * HALF_L + PI_RMID + (LONG)a * PI_RMID / 128;
    }
    return s;
}

void track_point(LONG s, WORD lateral, WORD *x, WORD *y)
{
    LONG len = g_tour.track.lap_length;
    WORD r = R_MID + lateral;
    s %= len; if (s < 0) s += len;
    if (s < 2 * HALF_L) { *x = CX - HALF_L + (WORD)s; *y = CY + r; return; }
    s -= 2 * HALF_L;
    if (s < PI_RMID) {
        angle8 a = (angle8)(64 - s * 128 / PI_RMID);
        *x = CX + HALF_L + (WORD)(((LONG)COS(a) * r) >> 14);
        *y = CY + (WORD)(((LONG)SIN(a) * r) >> 14);
        return;
    }
    s -= PI_RMID;
    if (s < 2 * HALF_L) { *x = CX + HALF_L - (WORD)s; *y = CY - r; return; }
    s -= 2 * HALF_L;
    {
        angle8 a = (angle8)(192 - s * 128 / PI_RMID);
        *x = CX - HALF_L + (WORD)(((LONG)COS(a) * r) >> 14);
        *y = CY + (WORD)(((LONG)SIN(a) * r) >> 14);
    }
}

/* strefa z tablicy (bez pierwiastka) - do uzycia co klatke */
GripZone track_zone_fast(WORD x, WORD y)
{
    WORD cx = x / GRIP_CELL, cy = y / GRIP_CELL;
    if (cx < 0 || cy < 0 || cx >= GRIP_W || cy >= GRIP_H) return GRIP_WALL;
    return (GripZone)g_tour.track.zone[cy][cx];
}

UBYTE track_pack_at(WORD x, WORD y)
{
    WORD cx = x / GRIP_CELL, cy = y / GRIP_CELL;
    if (cx < 0 || cy < 0 || cx >= GRIP_W || cy >= GRIP_H) return 0;
    return g_tour.track.pack[cy][cx];
}

UBYTE track_grip_at(WORD x, WORD y)
{
    WORD cx = x / GRIP_CELL, cy = y / GRIP_CELL;
    if (cx < 0 || cy < 0 || cx >= GRIP_W || cy >= GRIP_H) return 0;
    return g_tour.track.grip[cy][cx];
}

/* Przyczepnosc komorki = strefa + rodzaj toru - wilgoc + ubicie.
 * Mokry tor: slisko wszedzie, najbardziej przy krawezniku (tam polewaczka leje
 * najwiecej). Odsypisko ma niski grip, ale bonus "szeroka niesie" liczy race.c.
 * Wyjezdzona sciezka (pack): do +PACK_GRIP i przesycha szybciej (mniej wody). */
#define PACK_GRIP     32          /* bonus przyczepnosci przy pelnym ubiciu */
#define PACK_WEAR     1           /* ubicie na klatke pod rozpedzonym motocyklem */
#define PACK_LOOSEN   1           /* spulchnienie od grudki szprycy */

static void cell_grip(WORD gx, WORD gy)
{
    static const UBYTE base[5] = { 210, 225, 200, 175, 0 };
    static const UBYTE wet_loss[5] = { 90, 70, 50, 30, 0 };
    Track *t = &g_tour.track;
    GripZone z = (GripZone)t->zone[gy][gx];
    UBYTE pk = t->pack[gy][gx];
    WORD wet = (WORD)((t->wetness * (256 - (pk >> 1))) >> 8);   /* ubita linia mniej mokra */
    WORD g = (WORD)(base[z] - (wet_loss[z] * wet >> 8));
    if (z != GRIP_WALL) g += def->grip_adj + ((pk * PACK_GRIP) >> 8);
    t->grip[gy][gx] = (UBYTE)(g < 0 ? 0 : g > 255 ? 255 : g);
}

static void rebuild_grip(void)
{
    WORD gx, gy;
    for (gy = 0; gy < GRIP_H; gy++)
        for (gx = 0; gx < GRIP_W; gx++)
            cell_grip(gx, gy);
}

/* ---------------- widoczne wyjezdzone sciezki ----------------
 * Ubicie ma 4 poziomy (pack >> 6). Przy zmianie poziomu komorka 8x8 tla toru jest
 * przemalowywana z nietknietej kopii (pristine): nawierzchnia ciemniejsza o poziom,
 * kamyczki znikaja. Zmienione komorki kopiuje Blitter (blok 16x8) do obu buforow
 * ekranu - kazdy bufor ma wlasna liste "brudnych" komorek. */
static UBYTE *pristine;                    /* oryginal tla wybranego toru (dowolna pamiec) */
static BYTE   pristine_type = -1;
static UBYTE  level_drawn[GRIP_H][GRIP_W];
static UBYTE  wear_dirty[2][GRIP_H][GRIP_W];
static UWORD  wear_dirty_n[2];             /* ile komorek czeka - 0 = nic do roboty */

static void plot(UBYTE *buf, WORD x, WORD y, UBYTE c);

static UWORD pack_at(WORD gx, WORD gy)
{
    if (gx < 0) gx = 0; else if (gx >= GRIP_W) gx = GRIP_W - 1;
    if (gy < 0) gy = 0; else if (gy >= GRIP_H) gy = GRIP_H - 1;
    return g_tour.track.pack[gy][gx];
}

/* Przemalowanie komorki: ubicie interpolowane miedzy srodkami sasiednich komorek
 * (bez widocznych kwadratow), przyciemnienie z ditheringiem, rzadkie ziarna koleiny. */
/* Przemalowanie komorki po wierszach: komorka 8x8 to w kazdym wierszu jeden bajt na plan,
 * wiec wiersz = 8 odczytow i 8 zapisow. Kolory wynikaja z ubicia interpolowanego miedzy
 * srodkami komorek (pk = ubicie komorki i sasiadow z chwili rozpoczecia). */
static void cell_rows(WORD gx, WORD gy, const UWORD pk[3][3], WORD r0, WORD r1)
{
    WORD r, k;
    for (r = r0; r < r1; r++) {
        WORD y = (WORD)(gy * GRIP_CELL + r);
        WORD ty = (WORD)(r < 4 ? r + 4 : r - 4), a = (WORD)(r < 4 ? 0 : 1);
        LONG off = (LONG)y * PLAY_STRIDE + gx;
        const UBYTE *src = pristine + off;
        UBYTE *dst = g_video.background + off;
        UBYTE in[PLAY_DEPTH], out[PLAY_DEPTH], pl;
        for (pl = 0; pl < PLAY_DEPTH; pl++) { in[pl] = src[pl * ROW_BYTES]; out[pl] = 0; }
        for (k = 0; k < GRIP_CELL; k++) {
            WORD x = (WORD)(gx * GRIP_CELL + k);
            WORD tx = (WORD)(k < 4 ? k + 4 : k - 4), c0 = (WORD)(k < 4 ? 0 : 1);
            UBYTE bit = (UBYTE)(0x80 >> k), c = 0;
            UWORD f = (UWORD)((pk[a][c0] * (8 - tx) * (8 - ty) + pk[a][c0 + 1] * tx * (8 - ty)
                             + pk[a + 1][c0] * (8 - tx) * ty + pk[a + 1][c0 + 1] * tx * ty) >> 6);
            UBYTE hash = (UBYTE)((x * 73 + y * 151 + ((x * y) >> 2)) & 0xFF);
            for (pl = 0; pl < PLAY_DEPTH; pl++) if (in[pl] & bit) c |= (UBYTE)(1 << pl);
            if (c >= 16 && c < 48) {                       /* nawierzchnia: ciemniej i gladziej */
                UBYTE base = (UBYTE)(16 + ((c - 16) & ~7)), sh = (UBYTE)(c - base);
                UBYTE d = (UBYTE)((f * 6 + hash) >> 8);    /* 0..6 z ditheringiem */
                c = (UBYTE)(base + (sh > d ? sh - d : 0));
                if (f > 190 && (hash & 15) == 0) c = (UBYTE)(48 + (hash >> 6 & 1));   /* koleina */
            } else if (c >= 52 && c < 56 && f > 120) {     /* kamyczki zepchniete z linii */
                c = 16 + 3 * 8 + 2;
            }
            for (pl = 0; pl < PLAY_DEPTH; pl++) if (c & (1 << pl)) out[pl] |= bit;
        }
        for (pl = 0; pl < PLAY_DEPTH; pl++) dst[pl * ROW_BYTES] = out[pl];
    }
}

/* gotowa komorka trafia do obu buforow ekranu (track_flush_wear) */
static void cell_done(WORD gx, WORD gy)
{
    if (!wear_dirty[0][gy][gx]) { wear_dirty[0][gy][gx] = 1; wear_dirty_n[0]++; }
    if (!wear_dirty[1][gy][gx]) { wear_dirty[1][gy][gx] = 1; wear_dirty_n[1]++; }
}

/* Komorki do przemalowania czekaja w kolejce, a track_tick() maluje po kilka WIERSZY na klatke
 * (pomiar w WinUAE: cala komorka naraz to ok. 100 linii wiazki, do 2 komorek w klatce
 * gubilo klatki w biegu). Na ekran komorka trafia dopiero gotowa. */
static UBYTE vis_pending[GRIP_H][GRIP_W];
static UWORD vis_n, vis_cursor;
static WORD  cur_gx = -1, cur_gy, cur_row;   /* komorka w trakcie malowania (-1 = brak) */
static UWORD cur_pk[3][3];
static UBYTE grip_rows_left, grip_row;     /* przeliczanie przyczepnosci po kawalku (schniecie) */

static void mark_visual(WORD gx, WORD gy)
{
    if ((UBYTE)(g_tour.track.pack[gy][gx] >> 6) == level_drawn[gy][gx] || vis_pending[gy][gx]) return;
    vis_pending[gy][gx] = 1;
    vis_n++;
}

static void all_cells_visual(void)
{
    WORD gx, gy;
    for (gy = 0; gy < GRIP_H; gy++)
        for (gx = 0; gx < GRIP_W; gx++)
            if (g_tour.track.zone[gy][gx] != GRIP_WALL) mark_visual(gx, gy);
}

/* raz na klatke: najwyzej rows wierszy komorek i 2 wiersze mapy przyczepnosci */
void track_tick(UWORD rows)
{
    UBYTE r;
    for (r = 0; r < 2 && grip_rows_left; r++, grip_rows_left--) {
        WORD gx;
        for (gx = 0; gx < GRIP_W; gx++) cell_grip(gx, grip_row);
        if (++grip_row >= GRIP_H) grip_row = 0;
    }
    if (!pristine) { vis_n = 0; return; }
    if (rows && (vis_n || cur_gx >= 0)) blit_wait();       /* Blitter moze jeszcze czytac tlo */
    while (rows) {
        if (cur_gx < 0) {                          /* nastepna komorka z kolejki */
            WORD gy, gx, k;
            UBYTE lvl;
            if (!vis_n) return;
            gy = (WORD)(vis_cursor / GRIP_W); gx = (WORD)(vis_cursor % GRIP_W);
            if (++vis_cursor >= GRIP_W * GRIP_H) vis_cursor = 0;
            if (!vis_pending[gy][gx]) continue;
            vis_pending[gy][gx] = 0;
            vis_n--;
            lvl = (UBYTE)(g_tour.track.pack[gy][gx] >> 6);
            if (lvl == level_drawn[gy][gx]) continue;   /* wrocila do narysowanego poziomu */
            level_drawn[gy][gx] = lvl;
            for (k = 0; k < 9; k++) cur_pk[k / 3][k % 3] = pack_at((WORD)(gx + k % 3 - 1), (WORD)(gy + k / 3 - 1));
            cur_gx = gx; cur_gy = gy; cur_row = 0;
        }
        {
            WORD n = (WORD)(GRIP_CELL - cur_row);
            if ((UWORD)n > rows) n = (WORD)rows;        /* rows bez znaku (0xFFFF = wszystko) */
            cell_rows(cur_gx, cur_gy, (const UWORD (*)[3])cur_pk, cur_row, (WORD)(cur_row + n));
            cur_row = (WORD)(cur_row + n);
            rows = (UWORD)(rows - n);
            if (cur_row >= GRIP_CELL) { cell_done(cur_gx, cur_gy); cur_gx = -1; }
        }
    }
}

/* wszystko od razu (przed biegiem, zrzuty) */
void track_tick_flush(void)
{
    while (vis_n || grip_rows_left || cur_gx >= 0) track_tick(0xFFFF);
}

/* Inny tor na kazdy turniej: 1-2 wyjezdzone sciezki w losowym miejscu przekroju
 * (przy krawezniku, srodkiem albo szeroko), lekko falujace wzdluz toru, o losowej sile.
 * Ubicie podnosi przyczepnosc i jest widoczne jak sciezki wyjezdzone w biegach. */
void track_random_surface(void)
{
    Track *t = &g_tour.track;
    WORD hw = t->half_width;
    UBYTE lines = (UBYTE)rng_range(1, 2), n;
    for (n = 0; n < lines; n++) {
        WORD lat0 = (WORD)((WORD)rng_range(0, (UWORD)(hw * 13 / 10)) - hw * 7 / 10);   /* -0.7..+0.6 szer. */
        WORD amp = (WORD)rng_range(0, (UWORD)(hw * 3 / 10));
        UBYTE waves = (UBYTE)rng_range(1, 3), ph = (UBYTE)rng_range(0, 255);
        UWORD strength = rng_range(210, 255);
        WORD w = (WORD)rng_range(6, 10);
        LONG s;
        for (s = 0; s < t->lap_length; s += 3) {
            UBYTE ang = (UBYTE)(s * 256 * waves / t->lap_length + ph);
            WORD lat = (WORD)(lat0 + ((LONG)amp * SIN(ang) >> 14)), d;
            if (lat < -hw + 2) lat = (WORD)(-hw + 2);
            if (lat > hw - 2) lat = (WORD)(hw - 2);
            for (d = -w; d <= w; d += 2) {
                WORD x, y, gx, gy;
                UWORD v = (UWORD)(strength * (UWORD)(w + 1 - (d < 0 ? -d : d)) / (UWORD)(w + 1));
                track_point(s, (WORD)(lat + d), &x, &y);
                gx = (WORD)(x / GRIP_CELL); gy = (WORD)(y / GRIP_CELL);
                if (gx < 0 || gy < 0 || gx >= GRIP_W || gy >= GRIP_H || t->zone[gy][gx] == GRIP_WALL) continue;
                if (v > t->pack[gy][gx]) t->pack[gy][gx] = (UBYTE)v;
            }
        }
    }
    /* rozmycie: kilka przebiegow 3x3 po komorkach toru - lagodne brzegi sciezek */
    {
        static UBYTE tmp[GRIP_H][GRIP_W];
        UBYTE pass;
        WORD gx, gy;
        for (pass = 0; pass < 3; pass++) {
            for (gy = 0; gy < GRIP_H; gy++)
                for (gx = 0; gx < GRIP_W; gx++) {
                    UWORD sum = 0, n = 0;
                    WORD dx, dy;
                    if (t->zone[gy][gx] == GRIP_WALL) { tmp[gy][gx] = t->pack[gy][gx]; continue; }
                    for (dy = -1; dy <= 1; dy++)
                        for (dx = -1; dx <= 1; dx++) {
                            WORD x = (WORD)(gx + dx), y = (WORD)(gy + dy);
                            if (x < 0 || y < 0 || x >= GRIP_W || y >= GRIP_H || t->zone[y][x] == GRIP_WALL) continue;
                            sum += t->pack[y][x];
                            n++;
                        }
                    tmp[gy][gx] = (UBYTE)(sum / n);
                }
            for (gy = 0; gy < GRIP_H; gy++)
                for (gx = 0; gx < GRIP_W; gx++) t->pack[gy][gx] = tmp[gy][gx];
        }
    }
    rebuild_grip();
    all_cells_visual();
}

/* wywolywane z ui_render dla bufora "back" (po zdjeciu starej szprycy) */
/* (kx, ky, kw, kh): prostokat z nakladka (np. wyniki biegu) - komorki pod nim czekaja, inaczej
 * kopia z tla zamazalaby nakladke; po jej zdjeciu panel i tak wraca z aktualnego tla. kw = 0: brak. */
void track_flush_wear(UBYTE bi, UBYTE *buf, WORD kx, WORD ky, WORD kw, WORD kh)
{
    WORD gx, gy;
    if (!wear_dirty_n[bi]) return;
    for (gy = 0; gy < GRIP_H; gy++)
        for (gx = 0; gx < GRIP_W; gx++)
            if (wear_dirty[bi][gy][gx]) {
                WORD x0 = (WORD)((gx * GRIP_CELL) & ~15), y0 = (WORD)(gy * GRIP_CELL);
                if (kw && x0 < kx + kw && x0 + 16 > kx && y0 < ky + kh && y0 + GRIP_CELL > ky) continue;
                wear_dirty[bi][gy][gx] = 0;
                wear_dirty_n[bi]--;
                blit_copy(buf, g_video.background, (WORD)((gx * GRIP_CELL) & ~15), (WORD)(gy * GRIP_CELL),
                          16, GRIP_CELL);
            }
}

/* bufor dostal cale tlo - nic nie trzeba dokopiowywac */
void track_forget_wear(UBYTE bi)
{
    WORD gx, gy;
    for (gy = 0; gy < GRIP_H; gy++)
        for (gx = 0; gx < GRIP_W; gx++)
            wear_dirty[bi][gy][gx] = 0;
    wear_dirty_n[bi] = 0;
}

void track_wear(WORD x, WORD y)
{
    WORD gx = x / GRIP_CELL, gy = y / GRIP_CELL;
    UBYTE *pk;
    if (gx < 0 || gy < 0 || gx >= GRIP_W || gy >= GRIP_H) return;
    pk = &g_tour.track.pack[gy][gx];
    if (*pk <= 255 - PACK_WEAR) { *pk += PACK_WEAR; cell_grip(gx, gy); mark_visual(gx, gy); }
}

void track_loosen(WORD x, WORD y)
{
    WORD gx = x / GRIP_CELL, gy = y / GRIP_CELL;
    UBYTE *pk;
    if (gx < 0 || gy < 0 || gx >= GRIP_W || gy >= GRIP_H) return;
    pk = &g_tour.track.pack[gy][gx];
    if (*pk >= PACK_LOOSEN) { *pk -= PACK_LOOSEN; cell_grip(gx, gy); mark_visual(gx, gy); }
}

void track_reset_wear(void)
{
    WORD gx, gy;
    for (gy = 0; gy < GRIP_H; gy++)
        for (gx = 0; gx < GRIP_W; gx++)
            g_tour.track.pack[gy][gx] = 0;
    rebuild_grip();
    all_cells_visual();
}

void track_set_wetness(UBYTE wet)
{
    g_tour.track.wetness = wet;
    rebuild_grip();
    video_blend_track_palette(wet);
}

void track_apply_watering(void)
{
    WORD gx, gy;
    /* rownarka zaciera wyjezdzone sciezki - zostaje ok. 1/3 ubicia */
    for (gy = 0; gy < GRIP_H; gy++)
        for (gx = 0; gx < GRIP_W; gx++)
            g_tour.track.pack[gy][gx] = (UBYTE)(g_tour.track.pack[gy][gx] / 3);
    all_cells_visual();
    g_tour.track.wetness = 255;
    g_tour.track.heats_since_watering = 0;
    rebuild_grip();
    video_blend_track_palette(255);
}

/* wywolywane po kazdym biegu i co kilka sekund w trakcie biegu */
void track_dry_step(void)
{
    Track *t = &g_tour.track;
    if (t->wetness == 0) return;
    t->wetness = t->wetness > 12 ? t->wetness - 12 : 0;
    grip_rows_left = GRIP_H;                       /* przyczepnosc przeliczy track_tick po 2 wiersze */
    video_blend_track_palette_lazy(t->wetness);    /* paleta po 64 kolory na klatke */
}

static void set_rgb(RGB24 *p, UBYTE r, UBYTE g, UBYTE b) { p->r = r; p->g = g; p->b = b; }

static void build_palettes(void)
{
    RGB24 *d = g_video.pal_dry, *w = g_video.pal_wet;
    static RGB24 hud[32];
    WORD i;

    set_rgb(&d[0], 8, 10, 20);
    set_rgb(&d[C_LINE], 255, 255, 255);
    /* granit: 4 strefy (kraweznik ubity ciemniejszy, pod banda luzny jasny) x 8 odcieni */
    for (i = 0; i < 32; i++) {
        WORD zone = i >> 3, n = i & 7;
        set_rgb(&d[C_TRACK + i], (UBYTE)(150 + zone * 14 + n * 5), (UBYTE)(88 + zone * 9 + n * 4), (UBYTE)(52 + zone * 4 + n * 3));
    }
    for (i = 0; i < 8; i++) set_rgb(&d[C_GRASS + i], (UBYTE)(30 + i * 6), (UBYTE)(110 + i * 12), (UBYTE)(40 + i * 4));
    set_rgb(&d[C_WALL + 0], 230, 30, 40);  set_rgb(&d[C_WALL + 1], 255, 255, 255);
    set_rgb(&d[C_WALL + 2], 20, 80, 220);  set_rgb(&d[C_WALL + 3], 255, 210, 0);
    set_rgb(&d[C_KERB], 250, 250, 250);    set_rgb(&d[C_KERB + 1], 200, 20, 20);

    /* interfejs 96..112 (100 = teczowy, nadpisywany co linie przez Copper) */
    set_rgb(&d[96], 18, 24, 64);    set_rgb(&d[97], 36, 52, 128);   set_rgb(&d[98], 130, 160, 255);
    set_rgb(&d[99], 255, 255, 255); set_rgb(&d[100], 255, 255, 255); set_rgb(&d[101], 255, 200, 40);
    set_rgb(&d[102], 150, 150, 175); set_rgb(&d[103], 235, 25, 25);  set_rgb(&d[104], 30, 90, 255);
    set_rgb(&d[105], 240, 240, 240); set_rgb(&d[106], 255, 220, 0);  set_rgb(&d[107], 170, 30, 110);
    set_rgb(&d[108], 0, 0, 0);       set_rgb(&d[109], 200, 200, 215); set_rgb(&d[110], 205, 125, 60);
    set_rgb(&d[111], 60, 220, 90);   set_rgb(&d[112], 255, 60, 50);

    /* duszki 240..255 */
    set_rgb(&d[241], 20, 20, 24);    set_rgb(&d[242], 190, 195, 205);  set_rgb(&d[243], 30, 30, 60);
    set_rgb(&d[244], 235, 25, 25);   set_rgb(&d[245], 30, 90, 255);    set_rgb(&d[246], 250, 250, 250);
    set_rgb(&d[247], 255, 220, 0);   set_rgb(&d[248], 255, 255, 255);  set_rgb(&d[249], 255, 120, 20);

    /* mokra wersja: granit ciemniejszy i lekko chlodniejszy (polysk wody), reszta bez zmian */
    for (i = 0; i < 256; i++) w[i] = d[i];
    for (i = C_TRACK; i < C_TRACK + 32; i++)
        set_rgb(&w[i], (UBYTE)(d[i].r * 5 / 9), (UBYTE)(d[i].g * 5 / 9), (UBYTE)(d[i].b * 5 / 9 + 14));

    /* HUD: 0 tlo (gradient z Coppera), 1 bialy, 2-5 kaski, 6/7 swiatla ON, 8/9 swiatla OFF,
     * 10-15 portret sedziego, 16 ramki */
    set_rgb(&hud[0], 16, 24, 64);   set_rgb(&hud[1], 255, 255, 255);
    set_rgb(&hud[2], 235, 25, 25);  set_rgb(&hud[3], 30, 90, 255);
    set_rgb(&hud[4], 240, 240, 240); set_rgb(&hud[5], 255, 220, 0);
    set_rgb(&hud[6], 255, 40, 30);  set_rgb(&hud[7], 40, 255, 60);
    set_rgb(&hud[8], 70, 10, 10);   set_rgb(&hud[9], 10, 60, 15);
    set_rgb(&hud[10], 240, 190, 150); set_rgb(&hud[11], 20, 20, 30);
    set_rgb(&hud[12], 40, 40, 120); set_rgb(&hud[13], 255, 255, 255);
    set_rgb(&hud[14], 10, 10, 10);  set_rgb(&hud[15], 120, 70, 30);
    set_rgb(&hud[16], 120, 140, 200);
    video_set_hud_palette(hud);
}

/* track.pal: 256 RGB sucho + 256 RGB mokro; hud.pal: 32 RGB */
static char asset_name[32];
static const char *track_file(UBYTE type, const char *ext)
{
    char *p = text_cat(asset_name, "PROGDIR:data/track");
    *p++ = (char)('0' + type);
    text_cat(p, ext);
    return asset_name;
}

/* trackN.pal: 256 RGB sucho + 256 RGB mokro (bez pliku: palety zastepcze z build_palettes) */
static void load_track_palette(UBYTE type)
{
    static UBYTE buf[256 * 3 * 2];
    UWORD i;
    if (!asset_load(track_file(type, ".pal"), buf, sizeof(buf))) {
        for (i = 0; i < 256; i++) {
            track_pal[type][0][i] = g_video.pal_dry[i];
            track_pal[type][1][i] = g_video.pal_wet[i];
        }
        return;
    }
    for (i = 0; i < 256; i++) {
        set_rgb(&track_pal[type][0][i], buf[i * 3], buf[i * 3 + 1], buf[i * 3 + 2]);
        set_rgb(&track_pal[type][1][i], buf[768 + i * 3], buf[768 + i * 3 + 1], buf[768 + i * 3 + 2]);
    }
}

static void load_hud_palette(void)
{
    static UBYTE buf[32 * 3];
    UWORD i;
    if (asset_load("PROGDIR:data/hud.pal", buf, 32 * 3)) {
        RGB24 hud[32];
        for (i = 0; i < 32; i++) set_rgb(&hud[i], buf[i * 3], buf[i * 3 + 1], buf[i * 3 + 2]);
        video_set_hud_palette(hud);
    }
}

static void plot(UBYTE *buf, WORD x, WORD y, UBYTE c)
{
    UBYTE *p = buf + (LONG)y * PLAY_STRIDE + (x >> 3);
    UBYTE bit = (UBYTE)(0x80 >> (x & 7)), pl;
    for (pl = 0; pl < PLAY_DEPTH; pl++, p += ROW_BYTES, c >>= 1)
        if (c & 1) *p |= bit; else *p &= (UBYTE)~bit;
}

void track_render(UBYTE *dst)
{
    WORD x, y;
    for (y = 0; y < PLAY_H; y++)
        for (x = 0; x < SCREEN_W; x++) {
            UWORD r = radial(x, y);
            UBYTE n = (UBYTE)(rng_next() & 7), c;
            if (r < R_IN - 2)       c = (UBYTE)(C_GRASS + (((x + y) >> 3) & 1) * 3 + (n & 1));     /* murawa w pasy */
            else if (r < R_IN)      c = (UBYTE)(C_KERB + ((track_progress(x, y) >> 3) & 1));       /* kraweznik */
            else if (r < R_OUT) {
                UWORD z = (r - R_IN) * 4 / (R_OUT - R_IN);
                c = (UBYTE)(C_TRACK + z * 8 + n);                                                  /* granit */
            }
            else if (r < R_OUT + 3) c = (UBYTE)(C_WALL + ((track_progress(x, y) >> 4) & 3));       /* banda */
            else                    c = (UBYTE)(C_GRASS + 4 + (n & 3));
            plot(dst, x, y, c);
        }
}

/* Przelaczenie toru: geometria, mapa przyczepnosci, tasma i pola, tlo i palety. */
void track_select(UBYTE type)
{
    Track *t = &g_tour.track;
    WORD gx, gy, i;
    UWORD c;

    type %= NUM_TRACKS;
    def = &track_defs[type];
    t->type = type;
    t->half_width = (WORD)((R_OUT - R_IN) / 2);
    t->lap_length = 4 * HALF_L + 2 * PI_RMID;
    for (gy = 0; gy < GRIP_H; gy++)
        for (gx = 0; gx < GRIP_W; gx++)
            t->zone[gy][gx] = (UBYTE)track_zone_at(gx * GRIP_CELL + GRIP_CELL / 2, gy * GRIP_CELL + GRIP_CELL / 2);

    /* waypointy dla AI: srodek toru, offsety = dopuszczalny zakres linii jazdy */
    t->num_wp = MAX_WAYPOINTS;
    for (i = 0; i < MAX_WAYPOINTS; i++) {
        track_point(t->lap_length * i / MAX_WAYPOINTS, 0, &t->wp[i].x, &t->wp[i].y);
        t->wp[i].inner_ofs = (BYTE)-(R_MID - R_IN - 4);
        t->wp[i].outer_ofs = (BYTE)(R_OUT - R_MID - 4);
    }

    t->tape_x = CX + TAPE_SHIFT;             /* linia startu na prostej startowej, za srodkiem */
    t->tape_y0 = CY + R_IN;
    t->tape_y1 = CY + R_OUT - 1;
    t->tape_s = track_progress(t->tape_x, t->tape_y0 + 4);
    t->gate_x = t->tape_x - 9;
    for (i = 0; i < 4; i++)
        t->gate_y[i] = (WORD)(CY + R_IN + GATE_FIRST + i * GATE_STEP);

    for (c = 0; c < 256; c++) {
        g_video.pal_dry[c] = track_pal[type][0][c];
        g_video.pal_wet[c] = track_pal[type][1][c];
    }
    g_video.background = track_bg[type];
    if (pristine) {
        ULONG n;
        if (pristine_type >= 0)                           /* poprzedni tor wraca do oryginalu */
            for (n = 0; n < PLAY_SIZE; n++) track_bg[pristine_type][n] = pristine[n];
        for (n = 0; n < PLAY_SIZE; n++) pristine[n] = track_bg[type][n];
        pristine_type = (BYTE)type;
    }
    for (gy = 0; gy < GRIP_H; gy++)                       /* inny tor = rowna nawierzchnia */
        for (gx = 0; gx < GRIP_W; gx++) {
            t->pack[gy][gx] = 0;
            level_drawn[gy][gx] = 0;
            wear_dirty[0][gy][gx] = wear_dirty[1][gy][gx] = 0;
        }
    wear_dirty_n[0] = wear_dirty_n[1] = 0;
    rebuild_grip();
    video_blend_track_palette(t->wetness);
}

/* Wszystkie tory wczytywane z gory - po przejeciu sprzetu nie ma juz dos.library. */
BOOL track_init(void)
{
    UBYTE i;
    g_tour.track.wetness = 0;
    build_palettes();                     /* zastepcze palety + paleta HUD */
    load_hud_palette();
    pristine = AllocMem(PLAY_SIZE, 0);    /* bez niej sciezki dzialaja, tylko ich nie widac */
    for (i = 0; i < NUM_TRACKS; i++) {
        track_bg[i] = AllocMem(PLAY_SIZE, MEMF_CHIP | MEMF_CLEAR);
        if (!track_bg[i]) return FALSE;
        def = &track_defs[i];
        load_track_palette(i);
        /* pikselart z tools/genart.py; bez pliku - zastepcza grafika generowana tutaj */
        if (!asset_load(track_file(i, ".bpl"), track_bg[i], PLAY_SIZE))
            track_render(track_bg[i]);
    }
    track_select(0);
    return TRUE;
}

void track_free(void)
{
    UBYTE i;
    for (i = 0; i < NUM_TRACKS; i++)
        if (track_bg[i]) FreeMem(track_bg[i], PLAY_SIZE);
    if (pristine) FreeMem(pristine, PLAY_SIZE);
}
