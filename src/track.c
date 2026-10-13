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
    { "GRANIT",  60, 40, 96,   0, 33 },     /* klasyczny */
    { "BAKAŁAR", 72, 36, 84,  15, 35 },     /* dlugie proste, ciasne luki, wezszy, przyczepny */
    { "PIASEK",  44, 40, 96, -15, 29 },     /* krotkie proste, duze luki, slisko, duze odsypisko */
};
static const TrackDef *def = &track_defs[0];

#define CX      160
#define CY      100
#define HALF_L  (def->half_l)
#define R_IN    (def->r_in)
#define R_OUT   (def->r_out)
#define R_MID   ((R_IN + R_OUT) / 2)
#define GATE_FIRST 7        /* pole A: 7 px od kraweznika */
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

UBYTE track_grip_at(WORD x, WORD y)
{
    WORD cx = x / GRIP_CELL, cy = y / GRIP_CELL;
    if (cx < 0 || cy < 0 || cx >= GRIP_W || cy >= GRIP_H) return 0;
    return g_tour.track.grip[cy][cx];
}

/* Przyczepnosc: mokry tor = slisko wszedzie, najbardziej przy krawezniku
 * (tam polewaczka leje najwiecej). Odsypisko ma niski grip, ale bonus
 * "szeroka niesie" liczy race.c zaleznie od predkosci. */
static void rebuild_grip(void)
{
    static const UBYTE base[5] = { 210, 225, 200, 150, 0 };
    static const UBYTE wet_loss[5] = { 90, 70, 50, 30, 0 };
    Track *t = &g_tour.track;
    WORD gx, gy;
    for (gy = 0; gy < GRIP_H; gy++)
        for (gx = 0; gx < GRIP_W; gx++) {
            GripZone z = (GripZone)t->zone[gy][gx];
            WORD g = (WORD)(base[z] - (wet_loss[z] * t->wetness >> 8));
            if (z != GRIP_WALL) g += def->grip_adj;
            t->grip[gy][gx] = (UBYTE)(g < 0 ? 0 : g > 255 ? 255 : g);
        }
}

void track_set_wetness(UBYTE wet)
{
    g_tour.track.wetness = wet;
    rebuild_grip();
    video_blend_track_palette(wet);
}

void track_apply_watering(void)
{
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
    rebuild_grip();
    video_blend_track_palette(t->wetness);
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

    t->tape_x = CX - 20;
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
}
