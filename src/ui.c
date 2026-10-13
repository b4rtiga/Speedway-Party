/* ui.c - ekrany tekstowe: tytul, ustawienia (edycja nazwisk), program/tabela,
 * wybor pol (biegi 14-15), podium; nakladki na tor: TASMA!, wynik biegu, polewanie.
 *
 * Rysowanie: tylko do bufora "back". Kazda zmiana tresci zwieksza `serial`;
 * bufor jest przerysowany, gdy jego drawn_serial != serial. Przy podwojnym
 * buforowaniu kazda zmiana kosztuje wiec dwa przerysowania (w dwoch kolejnych klatkach).
 * Pelne ekrany: czyszczenie Blitterem do koloru 0, ktory Copper zamienia w gradient.
 * Nakladki: panel w stalym prostokacie nad torem; zdjecie = kopia tla Blitterem. */
#include <exec/memory.h>
#include <proto/exec.h>

#include "game.h"
#include "video.h"
#include "blit.h"
#include "text.h"
#include "hw.h"
#include "system.h"
#include "ui.h"

/* kolory palety interfejsu (track.c) */
enum { U_PANEL = 96, U_PANEL2, U_BORDER, U_WHITE, U_RAINBOW, U_GOLD, U_GREY,
       U_HELMET, U_CURSOR = 107, U_SHADOW, U_SILVER, U_BRONZE, U_GREEN, U_RED };

#define PANEL_X 48
#define PANEL_Y 24
#define PANEL_W 224
#define PANEL_H 112
#define NAME_EDIT_MAX 12

typedef enum { OV_NONE = 0, OV_FALSE_START, OV_RESULT, OV_GRADING } Overlay;

static UBYTE serial = 1, drawn_serial[2];
static UBYTE buf_is_track[2];
static UBYTE tape_drawn[2] = { 0xFF, 0xFF };
static UBYTE blink;

/* menu ustawien */
static UBYTE setup_row;
static BOOL  editing;
static UBYTE pick_cursor, pick_prev;

#define BORDER_RACE 0x334          /* 12-bit: ciemny beton trybun */
static UWORD line_bg[FULL_H], line_fg[FULL_H], rainbow[48];
static UBYTE *title_img;            /* plansza tytulowa 320x256, 8 planow (Chip RAM) */

/* ------------------------------------------------------------------ */

void ui_invalidate(void) { serial++; if (!serial) serial = 1; }
BOOL ui_editing(void)    { return editing; }

static void rgb12_lerp_table(void)
{
    /* teczowy cykl 12-bit: R->Y->G->C->B->M->R, po 8 krokow */
    static const UBYTE key[7][3] = { {15,2,2},{15,14,2},{2,15,4},{2,14,15},{4,4,15},{14,3,15},{15,2,2} };
    UWORD i;
    for (i = 0; i < 48; i++) {
        UWORD k = i / 8, t = i % 8;
        UWORD r = (UWORD)(key[k][0] + ((WORD)key[k + 1][0] - key[k][0]) * (WORD)t / 8);
        UWORD g = (UWORD)(key[k][1] + ((WORD)key[k + 1][1] - key[k][1]) * (WORD)t / 8);
        UWORD b = (UWORD)(key[k][2] + ((WORD)key[k + 1][2] - key[k][2]) * (WORD)t / 8);
        rainbow[i] = (UWORD)((r << 8) | (g << 4) | b);
    }
}

void ui_init(void)
{
    rgb12_lerp_table();
    /* ilustracja z tools/genart.py; bez pliku plansza ma sam gradient i napisy */
    title_img = AllocMem(FULL_SIZE, MEMF_CHIP);
    if (title_img && !asset_load("PROGDIR:data/title.bpl", title_img, FULL_SIZE)) {
        FreeMem(title_img, FULL_SIZE);
        title_img = 0;
    }
}

void ui_free(void)
{
    if (title_img) FreeMem(title_img, FULL_SIZE);
}

static UBYTE *back(void) { return video_backbuffer(); }

/* pionowa kreska 2 px (CPU, bo Blitter tu rysuje tylko pelne slowa) */
static void vline2(UBYTE *buf, WORD x, WORD y, WORD h, UBYTE color)
{
    UBYTE mask = (UBYTE)(0xC0 >> (x & 7)), pl, c;
    WORD j;
    hw_wait_blit();
    for (j = 0; j < h; j++) {
        UBYTE *p = buf + (LONG)(y + j) * PLAY_STRIDE + (x >> 3);
        for (pl = 0, c = color; pl < PLAY_DEPTH; pl++, p += ROW_BYTES, c >>= 1)
            *p = (c & 1) ? (UBYTE)(*p | mask) : (UBYTE)(*p & ~mask);
    }
}

static void panel(UBYTE *buf, WORD x, WORD y, WORD w, WORD h, UBYTE fill, UBYTE border)
{
    blit_fill(buf, x, y, w, h, border);
    blit_fill(buf, x, y + 2, w, h - 4, fill);
    vline2(buf, x, y, h, border);
    vline2(buf, x + w - 2, y, h, border);
}

static void helmet_box(UBYTE *buf, WORD x, WORD y, UBYTE gate)
{
    blit_fill(buf, x, y, 16, 10, (UBYTE)(U_HELMET + gate));
}

/* pierwsze n znakow nazwiska (nazwiska maja 1 bajt na znak) */
static char *name_trunc(char *dst, const char *name, UBYTE n)
{
    while (*name && n--) *dst++ = *name++;
    *dst = 0;
    return dst;
}

static char *pad_to(char *start, char *end, UBYTE width)
{
    while (end - start < width) *end++ = ' ';
    *end = 0;
    return end;
}

static const char gate_letter[4] = { 'A', 'B', 'C', 'D' };

/* ======================= EKRAN TYTULOWY =========================== */

/* Plansza tytulowa: ilustracja (zuzlowiec w uslizgu ze szpryca) + napisy na niej.
 * Pokazywana na starcie gry przez kilka sekund albo do FIRE. */
static void draw_title(UBYTE *b)
{
    if (title_img) blit_copy(b, title_img, 0, 0, SCREEN_W, FULL_H);
    text_draw_big_center(b, 12, "SPEEDWAY", U_RAINBOW, U_SHADOW);
    text_draw_big_center(b, 38, "PARTY", U_GOLD, U_SHADOW);
    text_draw_center(b, 64, "AMIGA 1200 AGA", U_WHITE, U_SHADOW);
    if (blink) text_draw_center(b, 206, "NACIŚNIJ FIRE", U_RAINBOW, U_SHADOW);
    text_draw_center(b, 224, "TURNIEJ 15 BIEGÓW DLA 1-4 GRACZY", U_WHITE, U_SHADOW);
    text_draw_center(b, 240, "MIT © 2026 b4rtiga", U_GOLD, U_SHADOW);
}

BOOL ui_title_update(void)
{
    return (input_menu() & BTN_FIRE) != 0;
}

/* ========================= USTAWIENIA ============================= */

#define SETUP_ROWS   8
#define ROW_TRACK    5
#define ROW_COND     6
#define ROW_START    7
static const char *cond_names[3] = { "SUCHY", "WILGOTNY", "MOKRY" };
static const char *port_names[INPUT_NONE] = {
    "JOY 1", "JOY 0", "JOY 3 LPT", "JOY 4 LPT", "KLAW Z/X", "KLAW N/M", "STRZAŁKI"
};
static const char *level_names[3] = { "AMATOR", "PROFI", "MISTRZ" };

/* znaki do wyboru joystickiem */
static const UBYTE charset[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ\x80\x81\x82\x83\x84\x85\x86\x87\x88" "0123456789-.";

static WORD setup_row_y(UBYTE row) { return (WORD)(54 + row * 18); }

static void draw_setup(UBYTE *b)
{
    char s[48], *p;
    UBYTE i;

    text_draw_big_center(b, 12, "USTAWIENIA", U_RAINBOW, U_SHADOW);
    blit_fill(b, 16, setup_row_y(setup_row) - 4, 288, 17, editing ? U_GOLD : U_CURSOR);

    p = text_cat(s, "LICZBA GRACZY:   < ");
    p = text_uint(p, g_tour.num_humans, 1);
    text_cat(p, " >");
    text_draw(b, 40, setup_row_y(0), s, U_WHITE, U_SHADOW);

    for (i = 0; i < MAX_RIDERS; i++) {
        Rider *r = &g_tour.riders[i];
        BOOL human = i < g_tour.num_humans;
        WORD y = setup_row_y((UBYTE)(i + 1));
        p = text_uint(s, (ULONG)(i + 1), 1);
        text_cat(p, ".");
        text_draw(b, 24, y, s, U_GREY, 0);
        p = name_trunc(s, r->name, NAME_EDIT_MAX);
        if (editing && setup_row == i + 1 && blink) text_cat(p, "_");
        text_draw(b, 48, y, s, editing && setup_row == i + 1 ? U_SHADOW : U_WHITE, editing ? 0 : U_SHADOW);
        text_draw(b, 168, y, human ? "GRACZ" : "AI", human ? U_GREEN : U_GREY, U_SHADOW);
        text_draw(b, 216, y, human ? port_names[r->input] : level_names[r->ai_level], U_GOLD, U_SHADOW);
    }
    text_draw(b, 40, setup_row_y(ROW_TRACK), "TOR:", U_WHITE, U_SHADOW);
    p = text_cat(s, "< ");
    p = text_cat(p, track_name(g_tour.track.type));
    text_cat(p, " >");
    text_draw(b, 168, setup_row_y(ROW_TRACK), s, U_GOLD, U_SHADOW);
    text_draw(b, 40, setup_row_y(ROW_COND), "STAN TORU:", U_WHITE, U_SHADOW);
    p = text_cat(s, "< ");
    p = text_cat(p, cond_names[g_tour.start_condition]);
    text_cat(p, " >");
    text_draw(b, 168, setup_row_y(ROW_COND), s, U_GOLD, U_SHADOW);
    text_draw_center(b, setup_row_y(ROW_START), "START TURNIEJU", setup_row == ROW_START ? U_RAINBOW : U_WHITE, U_SHADOW);

    text_draw_center(b, 202, editing ? "PISZ Z KLAWIATURY / GÓRA-DÓŁ LITERA"
                                     : "GÓRA/DÓŁ: WYBÓR  LEWO/PRAWO: ZMIANA", U_GREY, 0);
    text_draw_center(b, 216, editing ? "PRAWO: DODAJ  LEWO: USUŃ  FIRE: OK"
                                     : "FIRE: EDYCJA NAZWISKA / START", U_GREY, 0);
    text_draw_center(b, 230, "ALT+A,C,E,L,N,O,S,X,Z = POLSKIE LITERY", U_GREY, 0);
    text_draw_center(b, 244, "ESC - WYJŚCIE Z GRY", U_GREY, 0);
}

static UBYTE name_len(const char *n) { UBYTE l = 0; while (n[l]) l++; return l; }

static UBYTE rawkey_char(UBYTE k)
{
    static const char row1[] = "1234567890-";
    static const char rowq[] = "QWERTYUIOP";
    static const char rowa[] = "ASDFGHJKL";
    static const char rowz[] = "ZXCVBNM";
    BOOL alt = key_down(KEY_LALT) || key_down(KEY_RALT);
    UBYTE c = 0;
    if (k >= 0x01 && k <= 0x0B) c = (UBYTE)row1[k - 0x01];
    else if (k >= 0x10 && k <= 0x19) c = (UBYTE)rowq[k - 0x10];
    else if (k >= 0x20 && k <= 0x28) c = (UBYTE)rowa[k - 0x20];
    else if (k >= 0x31 && k <= 0x37) c = (UBYTE)rowz[k - 0x31];
    else if (k == 0x40) c = ' ';
    else if (k == 0x39) c = '.';
    if (alt) {
        /* uklad "polski programisty": Alt+X = Ź, Alt+Z = Ż */
        static const char plain[] = "ACELNOSXZ";
        UBYTE i;
        for (i = 0; i < PL_COUNT; i++)
            if (c == (UBYTE)plain[i]) return (UBYTE)(PL_FIRST + i);
    }
    return c;
}

static UBYTE charset_next(UBYTE c, BYTE dir)
{
    UBYTE i, n = sizeof(charset) - 1;
    for (i = 0; i < n && charset[i] != c; i++) ;
    if (i == n) i = 0;
    return charset[(i + n + dir) % n];
}

static void edit_name(Rider *r, UBYTE m)
{
    UBYTE len = name_len(r->name), k;

    while ((k = key_get()) != 0xFF) {
        if (k == KEY_RETURN || k == KEY_ESC) { editing = FALSE; break; }
        if (k == KEY_BACKSPACE) { if (len) r->name[--len] = 0; continue; }
        if (len < NAME_EDIT_MAX) {
            UBYTE c = rawkey_char(k);
            if (c) { r->name[len++] = (char)c; r->name[len] = 0; }
        }
    }
    if (m & BTN_RIGHT && len < NAME_EDIT_MAX) { r->name[len++] = 'A'; r->name[len] = 0; }
    if (m & BTN_LEFT && len) r->name[--len] = 0;
    if (m & (BTN_UP | BTN_DOWN)) {
        if (!len) { r->name[len++] = 'A'; r->name[len] = 0; }
        else r->name[len - 1] = (char)charset_next((UBYTE)r->name[len - 1], (BYTE)(m & BTN_UP ? 1 : -1));
    }
    if (m & BTN_FIRE && !(m & BTN_DIRS)) editing = FALSE;
    if (!editing) {
        /* obetnij spacje z konca; puste nazwisko -> "ZAWODNIK" */
        while (len && r->name[len - 1] == ' ') r->name[--len] = 0;
        if (!len) text_cat(r->name, "ZAWODNIK");
    }
}

BOOL ui_setup_update(void)
{
    UBYTE m = input_menu();
    UBYTE k;
    BOOL changed = FALSE;

    if (editing) {
        Rider *r = &g_tour.riders[setup_row - 1];
        char before[NAME_LEN + 1];
        BOOL was = editing;
        name_trunc(before, r->name, NAME_LEN);
        edit_name(r, m);
        for (k = 0; k <= NAME_LEN && !changed; k++) if (before[k] != r->name[k]) changed = TRUE;
        if (changed || was != editing) ui_invalidate();
        return FALSE;
    }
    while (key_get() != 0xFF) ;      /* poza edycja ignorujemy pisanie */

    if (m & BTN_UP)   { setup_row = (UBYTE)((setup_row + SETUP_ROWS - 1) % SETUP_ROWS); changed = TRUE; }
    if (m & BTN_DOWN) { setup_row = (UBYTE)((setup_row + 1) % SETUP_ROWS); changed = TRUE; }

    if (m & (BTN_LEFT | BTN_RIGHT)) {
        BYTE d = (BYTE)(m & BTN_RIGHT ? 1 : -1);
        changed = TRUE;
        if (setup_row == 0) {
            WORD n = g_tour.num_humans + d;
            g_tour.num_humans = (UBYTE)(n < 1 ? 1 : n > MAX_RIDERS ? MAX_RIDERS : n);
        } else if (setup_row <= MAX_RIDERS) {
            Rider *r = &g_tour.riders[setup_row - 1];
            if (setup_row - 1 < g_tour.num_humans)
                r->input = (InputPort)((r->input + INPUT_NONE + d) % INPUT_NONE);
            else
                r->ai_level = (AiLevel)((r->ai_level + 3 + d) % 3);
        } else if (setup_row == ROW_TRACK) {
            track_select((UBYTE)((g_tour.track.type + NUM_TRACKS + d) % NUM_TRACKS));
        } else if (setup_row == ROW_COND) {
            g_tour.start_condition = (UBYTE)((g_tour.start_condition + 3 + d) % 3);
        }
    }
    if (m & BTN_FIRE) {
        if (setup_row == ROW_START) return TRUE;
        if (setup_row >= 1 && setup_row <= MAX_RIDERS) { editing = TRUE; changed = TRUE; }
    }
    if (changed) ui_invalidate();
    return FALSE;
}

/* ===================== PROGRAM / TABELA =========================== */

/* czas w klatkach -> "41.2" (dziesiate) albo "9.84" (setne) */
static char *fmt_time(char *p, UWORD frames, BOOL hundredths)
{
    p = text_uint(p, (ULONG)(frames / FPS), 1);
    p = text_cat(p, ".");
    if (hundredths) return text_uint(p, (ULONG)((frames % FPS) * 2), 2);
    return text_uint(p, (ULONG)((frames % FPS) * 2 / 10), 1);
}

/* "REKORD BIEGU 41.2 S NOWAK" / "REKORD OKR.  9.84 S KOWALSKI" */
static void record_text(char *s, BOOL lap)
{
    TrackRecord *r = records_for(g_tour.track.type);
    UWORD fr = lap ? r->lap_frames : r->heat_frames;
    char *p = text_cat(s, lap ? "REKORD OKR. " : "REKORD BIEGU ");
    if (!fr) { text_cat(p, "-"); return; }
    p = fmt_time(p, fr, lap);
    p = text_cat(p, " S ");
    name_trunc(p, lap ? r->lap_name : r->heat_name, 10);
}

static void wetness_text(char *s)
{
    UBYTE w = g_tour.track.wetness;
    char *p = text_cat(text_cat(s, "TOR "), track_name(g_tour.track.type));
    text_cat(p, w > 170 ? ": MOKRY" : w > 60 ? ": WILGOTNY" : ": SUCHY");
}

static void draw_standings(UBYTE *b, WORD y)
{
    UBYTE order[MAX_RIDERS], i, h;
    char s[48], *p;

    tournament_sort(order);
    text_draw(b, 16, y, "ZAWODNIK", U_GREY, 0);
    text_draw(b, 112, y, "123456789012345", U_GREY, 0);
    text_draw(b, 256, y, "PKT", U_GREY, 0);
    for (i = 0; i < MAX_RIDERS; i++) {
        Rider *r = &g_tour.riders[order[i]];
        WORD yy = (WORD)(y + 14 + i * 13);
        name_trunc(s, r->name, 11);
        text_draw(b, 16, yy, s, i == 0 ? U_GOLD : U_WHITE, U_SHADOW);
        for (h = 0; h < NUM_HEATS; h++) {
            UBYTE v = r->heat_points[h];
            s[h] = v == HP_NONE ? '.' : v == HP_EXCLUDED ? 'W' : (char)('0' + v);
        }
        s[NUM_HEATS] = 0;
        text_draw(b, 112, yy, s, U_WHITE, 0);
        p = text_uint(s, r->points, 1);
        pad_to(s, p, 1);
        text_draw(b, r->points >= 10 ? 256 : 264, yy, s, U_GOLD, U_SHADOW);
    }
}

static void draw_table(UBYTE *b)
{
    UBYTE hn = g_tour.current_heat, g;
    char s[48], *p;

    if (hn == RUNOFF_HEAT) text_cat(s, "BIEG DODATKOWY");
    else { p = text_cat(s, "BIEG "); text_uint(p, (ULONG)(hn + 1), 1); }
    text_draw_big_center(b, 10, s, U_RAINBOW, U_SHADOW);
    text_draw_center(b, 36, hn == RUNOFF_HEAT ? "REMIS - O MIEJSCE NA PODIUM"
                          : hn >= FIXED_HEATS ? "BIEG NOMINOWANY" : "PROGRAM ZAWODÓW", U_GOLD, 0);

    panel(b, 48, 52, 224, 64, U_PANEL, U_BORDER);
    for (g = 0; g < 4; g++) {
        Rider *r;
        WORD y = (WORD)(57 + g * 14);
        if (g_tour.heat_table[hn][g] == 0xFF) continue;          /* puste pole (bieg dodatkowy) */
        r = &g_tour.riders[g_tour.heat_table[hn][g]];
        helmet_box(b, 64, y, g);
        s[0] = gate_letter[g]; s[1] = 0;
        text_draw(b, 88, y + 1, s, U_GREY, 0);
        name_trunc(s, r->name, NAME_EDIT_MAX);
        text_draw(b, 104, y + 1, s, U_WHITE, U_SHADOW);
        text_draw(b, 216, y + 1, r->control == CTRL_HUMAN ? "GRACZ" : "AI", r->control == CTRL_HUMAN ? U_GREEN : U_GREY, 0);
    }

    draw_standings(b, 130);
    wetness_text(s);
    text_draw_center(b, 198, s, g_tour.track.wetness > 170 ? U_BORDER : U_GREY, 0);
    record_text(s, FALSE);
    text_draw_center(b, 211, s, U_GOLD, 0);
    record_text(s, TRUE);
    text_draw_center(b, 222, s, U_GOLD, 0);
    if (blink) text_draw_center(b, 238, "FIRE - POD TAŚMĘ!", U_RAINBOW, U_SHADOW);
}

BOOL ui_confirm(void)
{
    return (input_menu() & BTN_FIRE) || key_get() == KEY_RETURN;
}

/* ======================== WYBOR POL ================================ */

static void draw_pick(UBYTE *b)
{
    UBYTE hn = g_tour.current_heat, g, i;
    UBYTE *row = g_tour.heat_table[hn];
    char s[48], *p;

    p = text_cat(s, "BIEG ");
    text_uint(p, (ULONG)(hn + 1), 1);
    text_draw_big_center(b, 10, s, U_RAINBOW, U_SHADOW);
    text_draw_center(b, 36, "WYBÓR PÓL STARTOWYCH", U_GOLD, 0);

    if (g_tour.pick_index < MAX_RIDERS) {
        Rider *r = &g_tour.riders[g_tour.pick_order[g_tour.pick_index]];
        p = text_cat(s, "WYBIERA: ");
        p = name_trunc(p, r->name, NAME_EDIT_MAX);
        p = text_cat(p, " (");
        p = text_uint(p, r->points, 1);
        text_cat(p, r->control == CTRL_AI ? " PKT, AI)" : " PKT)");
        text_draw_center(b, 56, s, U_WHITE, U_SHADOW);
    }

    for (g = 0; g < 4; g++) {
        WORD x = (WORD)(16 + g * 80);
        BOOL cur = g_tour.pick_index < MAX_RIDERS && g == pick_cursor && row[g] == 0xFF
                   && g_tour.riders[g_tour.pick_order[g_tour.pick_index]].control == CTRL_HUMAN;
        panel(b, x, 78, 64, 96, cur ? U_CURSOR : U_PANEL, cur ? U_GOLD : U_BORDER);
        blit_fill(b, x + 16, 84, 32, 12, (UBYTE)(U_HELMET + g));
        s[0] = gate_letter[g]; s[1] = 0;
        text_draw_big(b, x + 24, 106, s, U_WHITE, U_SHADOW);
        if (row[g] != 0xFF) name_trunc(s, g_tour.riders[row[g]].name, 7);
        else text_cat(s, "WOLNE");
        text_draw(b, (WORD)(x + ((64 - text_len(s) * 8) / 2 & ~7)), 150, s,
                  row[g] != 0xFF ? U_GOLD : U_GREY, U_SHADOW);
    }

    text_draw(b, 16, 190, "KOLEJNOŚĆ WYBORU:", U_GREY, 0);
    for (i = 0; i < MAX_RIDERS; i++) {
        p = text_uint(s, (ULONG)(i + 1), 1);
        p = text_cat(p, ".");
        name_trunc(p, g_tour.riders[g_tour.pick_order[i]].name, 7);
        text_draw((UBYTE *)b, (WORD)(i * 80), 206, s, i == g_tour.pick_index ? U_RAINBOW : U_WHITE, 0);
    }
    text_draw_center(b, 234, "LEWO/PRAWO: POLE   FIRE: WYBIERZ", U_GREY, 0);
}

void ui_pick_update(void)
{
    static UWORD ai_wait;
    UBYTE *row = g_tour.heat_table[g_tour.current_heat];
    UBYTE rider, in, edge;

    if (g_tour.pick_index >= MAX_RIDERS) return;
    rider = g_tour.pick_order[g_tour.pick_index];

    if (g_tour.riders[rider].control == CTRL_AI) {
        if (++ai_wait > FPS) {                    /* chwila "namyslu" dla czytelnosci */
            ai_wait = 0;
            tournament_assign_gate(rider, tournament_ai_choose_gate(rider));
            ui_invalidate();
        }
        return;
    }
    in = input_read(g_tour.riders[rider].input);
    edge = (UBYTE)((in & ~pick_prev) | input_menu());
    pick_prev = in;

    if (row[pick_cursor] != 0xFF) {
        while (row[pick_cursor] != 0xFF) pick_cursor = (UBYTE)((pick_cursor + 1) & 3);
        ui_invalidate();
    }
    if (edge & BTN_RIGHT) { do pick_cursor = (UBYTE)((pick_cursor + 1) & 3); while (row[pick_cursor] != 0xFF); ui_invalidate(); }
    if (edge & BTN_LEFT)  { do pick_cursor = (UBYTE)((pick_cursor + 3) & 3); while (row[pick_cursor] != 0xFF); ui_invalidate(); }
    if (edge & BTN_FIRE) {
        tournament_assign_gate(rider, pick_cursor);
        ui_invalidate();
    }
}

/* =========================== PODIUM ================================ */

static void draw_podium(UBYTE *b)
{
    static const WORD bx[3] = { 128, 48, 208 }, top[3] = { 86, 110, 128 };
    static const UBYTE col[3] = { U_GOLD, U_SILVER, U_BRONZE };
    UBYTE order[MAX_RIDERS], i;
    char s[48], *p;

    tournament_sort(order);
    text_draw_big_center(b, 10, "KONIEC TURNIEJU", U_RAINBOW, U_SHADOW);

    for (i = 0; i < 3; i++) {
        Rider *r = &g_tour.riders[order[i]];
        blit_fill(b, bx[i], top[i], 64, 166 - top[i], col[i]);
        s[0] = (char)('1' + i); s[1] = 0;
        text_draw_big(b, bx[i] + 24, top[i] + 6, s, U_SHADOW, 0);
        name_trunc(s, r->name, 8);
        text_draw(b, (WORD)(bx[i] + ((64 - text_len(s) * 8) / 2 & ~7)), top[i] - 14, s, U_WHITE, U_SHADOW);
    }
    for (i = 0; i < MAX_RIDERS; i++) {
        Rider *r = &g_tour.riders[order[i]];
        WORD y = (WORD)(178 + i * 13);
        p = text_uint(s, (ULONG)(i + 1), 1);
        p = text_cat(p, ". ");
        p = name_trunc(p, r->name, NAME_EDIT_MAX);
        p = pad_to(s, p, r->points < 10 ? 17 : 16);
        p = text_uint(p, r->points, 1);
        p = text_cat(p, " PKT  ");
        p = text_uint(p, r->wins, 1);
        text_cat(p, " ZW.");
        text_draw(b, 40, y, s, i == 0 ? U_GOLD : U_WHITE, U_SHADOW);
    }
    if (blink) text_draw_center(b, 238, "FIRE - NOWY TURNIEJ", U_RAINBOW, U_SHADOW);
}

/* ========================== NAKLADKI ============================== */

static Overlay current_overlay(void)
{
    if (g_tour.state == GS_HEAT && g_tour.heat.phase == HEAT_FALSE_START) return OV_FALSE_START;
    if (g_tour.state == GS_HEAT_RESULT) return OV_RESULT;
    if (g_tour.state == GS_TRACK_GRADING) return OV_GRADING;
    return OV_NONE;
}

static void draw_false_start(UBYTE *b)
{
    Heat *h = &g_tour.heat;
    Bike *bk = &g_tour.bikes[h->false_starter];
    Rider *r = &g_tour.riders[bk->rider];
    char s[48], *p;

    panel(b, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, U_PANEL, U_RED);
    text_draw_big_center(b, PANEL_Y + 12, "TAŚMA!", U_RED, U_SHADOW);
    helmet_box(b, 80, PANEL_Y + 48, bk->gate);
    name_trunc(s, r->name, NAME_EDIT_MAX);
    text_draw(b, 104, PANEL_Y + 49, s, U_WHITE, U_SHADOW);
    text_draw_center(b, PANEL_Y + 68, bk->state == BIKE_EXCLUDED ? "WYKLUCZENIE!" : "OSTRZEŻENIE",
                     bk->state == BIKE_EXCLUDED ? U_RED : U_GOLD, U_SHADOW);
    p = text_cat(s, "POWTÓRKA STARTU (");
    p = text_uint(p, h->restarts, 1);
    text_cat(p, ")");
    text_draw_center(b, PANEL_Y + 90, s, U_GREY, 0);
}

static void draw_result(UBYTE *b)
{
    static const char *pts_txt[4] = { "3 PKT", "2 PKT", "1 PKT", "0 PKT" };
    Heat *h = &g_tour.heat;
    UBYTE line, i, g;
    char s[48], *p;

    panel(b, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, U_PANEL, U_BORDER);
    if (g_tour.current_heat == RUNOFF_HEAT) text_cat(s, "BIEG DODATKOWY");
    else { p = text_cat(s, "WYNIK BIEGU "); text_uint(p, h->number, 1); }
    text_draw_center(b, PANEL_Y + 6, s, U_GOLD, U_SHADOW);

    /* najpierw meta (finish_order), potem nieukonczeni (NU) i wykluczeni (W) */
    {
        UBYTE list[MAX_RIDERS], n = 0, k;
        for (i = 0; i < h->finished_count; i++)
            for (g = 0; g < 4; g++) if (g_tour.bikes[g].rider == h->finish_order[i]) list[n++] = g;
        for (g = 0; g < 4; g++) {
            BOOL listed = FALSE;
            if (g_tour.bikes[g].empty) continue;
            for (k = 0; k < n; k++) if (list[k] == g) listed = TRUE;
            if (!listed) list[n++] = g;
        }
        for (line = 0; line < n; line++) {
            Bike *bk = &g_tour.bikes[list[line]];
            WORD y = (WORD)(PANEL_Y + 24 + line * 14);
            BOOL fin = line < h->finished_count;
            p = text_uint(s, (ULONG)(line + 1), 1);
            text_cat(p, ".");
            text_draw(b, 64, y + 1, s, U_GREY, 0);
            helmet_box(b, 80, y, bk->gate);
            name_trunc(s, g_tour.riders[bk->rider].name, 10);
            text_draw(b, 104, y + 1, s, U_WHITE, U_SHADOW);
            if (g_tour.current_heat != RUNOFF_HEAT || !fin)     /* bieg dodatkowy: bez punktow */
                text_draw(b, 200, y + 1, fin ? pts_txt[line] : bk->state == BIKE_EXCLUDED ? "W" : "NU",
                          line == 0 && fin ? U_GOLD : U_WHITE, U_SHADOW);
        }
    }
    if (h->finished_count) {
        UWORD t = g_tour.bikes[0].finish_frame;
        for (g = 0; g < 4; g++)
            if (g_tour.bikes[g].rider == h->finish_order[0]) t = g_tour.bikes[g].finish_frame;
        p = text_cat(s, "CZAS: ");
        p = fmt_time(p, t, FALSE);
        text_cat(p, " S");
        text_draw_center(b, PANEL_Y + 84, s, g_tour.new_records & REC_HEAT ? U_GOLD : U_GREY, 0);
    }
    /* nowy rekord: na zmiane z "FIRE - DALEJ" */
    if (g_tour.new_records && !blink)
        text_draw_center(b, PANEL_Y + 98, g_tour.new_records & REC_HEAT ? "NOWY REKORD TORU!" : "REKORD OKRĄŻENIA!",
                         U_GOLD, U_SHADOW);
    else if (blink) text_draw_center(b, PANEL_Y + 98, "FIRE - DALEJ", U_RAINBOW, 0);
}

extern UWORD g_grading_progress;   /* 0..255, z main.c */

/* maly panel na murawie (w obrebie PANEL_*), zeby nie zaslaniac traktorow na torze */
#define GRADE_X 96
#define GRADE_Y 66
#define GRADE_W 128
#define GRADE_H 62

static void draw_grading(UBYTE *b)
{
    WORD w = (WORD)((96 * g_grading_progress / 255) & ~15);
    panel(b, GRADE_X, GRADE_Y, GRADE_W, GRADE_H, U_PANEL, U_BORDER);
    text_draw_center(b, GRADE_Y + 6, "PRZERWA NA TOR", U_GOLD, U_SHADOW);
    text_draw_center(b, GRADE_Y + 20, "POLEWANIE TORU", U_WHITE, U_SHADOW);
    blit_fill(b, 112, GRADE_Y + 34, 96, 10, U_BORDER);
    blit_fill(b, 112, GRADE_Y + 36, 96, 6, U_PANEL);
    if (w) blit_fill(b, 112, GRADE_Y + 36, w, 6, U_GREEN);
    text_draw_center(b, GRADE_Y + 48, "UWAGA: ŚLISKO!", U_GREY, 0);
}

/* ========================== RENDER ================================ */

static void plot(UBYTE *buf, WORD x, WORD y, UBYTE c)
{
    UBYTE *p = buf + (LONG)y * PLAY_STRIDE + (x >> 3);
    UBYTE bit = (UBYTE)(0x80 >> (x & 7)), pl;
    for (pl = 0; pl < PLAY_DEPTH; pl++, p += ROW_BYTES, c >>= 1)
        if (c & 1) *p |= bit; else *p &= (UBYTE)~bit;
}

static void restore_px(UBYTE *buf, WORD x, WORD y)
{
    LONG o = (LONG)y * PLAY_STRIDE + (x >> 3);
    UBYTE pl;
    for (pl = 0; pl < PLAY_DEPTH; pl++, o += ROW_BYTES) buf[o] = g_video.background[o];
}

static UBYTE tape_style(void)
{
    if (g_tour.state != GS_HEAT || g_tour.heat.emergency) return TAPE_GONE;   /* awaria: start z flagi */
    switch (g_tour.heat.phase) {
    case HEAT_TAPE_UP:  return g_tour.heat.tape_anim;
    case HEAT_RACING:
    case HEAT_FINISHED: return TAPE_GONE;
    default:            return 0;
    }
}

/* tasma: 0 = ciagla biala, 1 = rozmyta przerywana, 2 = rzadka, 3 = brak */
static void render_tape(UBYTE *buf)
{
    Track *t = &g_tour.track;
    UBYTE style = tape_style();
    WORD y, x = t->tape_x;
    if (tape_drawn[g_video.back] == style) return;
    hw_wait_blit();
    for (y = t->tape_y0; y <= t->tape_y1; y++) {
        WORD k;
        for (k = -3; k <= 4; k += 1) restore_px(buf, x + k, y);
        switch (style) {
        case 0:                                         /* napieta: 3 px bieli + cien */
            plot(buf, x - 1, y, 1); plot(buf, x, y, 1); plot(buf, x + 1, y, 1);
            plot(buf, x + 2, y, U_SHADOW);
            break;
        case 1: case 2: {                               /* zrywa sie: rozmyta, drgajaca */
            WORD j = (WORD)(((y >> 1) + style) & 1 ? 1 : -1);
            plot(buf, x - 1 + j, y, 1); plot(buf, x + j, y, 1); plot(buf, x + 1 + j, y, 1);
            plot(buf, x + 2 + j, y, U_SHADOW);
            break;
        }
        case 3: case 4:                                 /* gruba przerywana, odlatuje */
            if (((y + style) & 3) < 2) { plot(buf, x - 2, y, 1); plot(buf, x - 1, y, 1); plot(buf, x, y, 1); }
            break;
        case 5: case 6:                                 /* strzepy */
            if (((y + style) & 7) < 2) { plot(buf, x - 3, y, 1); plot(buf, x - 2, y, 1); }
            break;
        case 7:                                         /* ostatnie punkty */
            if ((y & 7) == 0) plot(buf, x - 3, y, 1);
            break;
        default: break;
        }
    }
    tape_drawn[g_video.back] = style;
}

/* ------------------------------------------------------------------ *
 * Okienko startu (prawy dolny rog): maszyna startowa z boku - slupki, lampka,
 * tasma i kaski zawodnikow. Na zielonym tasma wylatuje w gore, a okienko znika
 * w chwili ruszenia (TAPE_GONE klatek). Przy awarii maszyny: napis o starcie z flagi.
 * ------------------------------------------------------------------ */
#define SW_X 208
#define SW_Y 138                   /* ponizej obszaru nakladek (PANEL_Y + PANEL_H = 136) */
#define SW_W 112
#define SW_H 66
#define SW_NONE 0xFF
static UBYTE startwin_drawn[2] = { SW_NONE, SW_NONE };

/* stan okienka: SW_NONE albo (klatka odlotu << 2) | lampka (0 zgaszona, 1 czerwona, 2 zielona); 3 = awaria */
static UBYTE startwin_state(void)
{
    Heat *h = &g_tour.heat;
    if (g_tour.state != GS_HEAT) return SW_NONE;
    if (h->emergency)
        return (h->phase == HEAT_PRE || h->phase == HEAT_READY || h->phase == HEAT_RED_LIGHT) ? 3 : SW_NONE;
    switch (h->phase) {
    case HEAT_PRE:
    case HEAT_READY:     return 0;
    case HEAT_RED_LIGHT: return 1;
    case HEAT_TAPE_UP:   return (UBYTE)((h->phase_timer << 2) | 2);
    default:             return SW_NONE;      /* ruszyli: okienko znika */
    }
}

static void draw_startwin(UBYTE *b, UBYTE st)
{
    static const UBYTE lamp_col[3] = { U_PANEL, U_RED, U_GREEN };
    WORD x, y, k = (WORD)(st >> 2), g;
    panel(b, SW_X, SW_Y, SW_W, SW_H, U_PANEL, U_BORDER);
    if (st == 3) {                                           /* awaria maszyny startowej */
        text_draw(b, SW_X + 8, SW_Y + 10, "AWARIA", U_RED, U_SHADOW);
        text_draw(b, SW_X + 8, SW_Y + 24, "MASZYNY", U_RED, U_SHADOW);
        text_draw(b, SW_X, SW_Y + 44, "START Z FLAGI", U_WHITE, U_SHADOW);
        return;
    }
    blit_fill(b, SW_X, SW_Y + 2, SW_W, 22, 129);             /* trybuna */
    blit_fill(b, SW_X, SW_Y + 24, SW_W, SW_H - 26, 24);      /* nawierzchnia */
    hw_wait_blit();
    for (y = SW_Y + 4; y < SW_Y + 24; y += 3)                /* kibice */
        for (x = SW_X + 3; x < SW_X + SW_W - 3; x += 2)
            plot(b, x, y, (UBYTE)(140 + ((x * 7 + y * 13) % 32)));
    for (y = SW_Y + 24; y < SW_Y + SW_H - 2; y++)            /* faktura nawierzchni */
        for (x = SW_X + 2; x < SW_X + SW_W - 2; x++)
            if (((x * 37 + y * 91) & 7) == 0) plot(b, x, y, (UBYTE)(19 + ((x + y) & 3)));
    for (g = 0; g < 4; g++) {                                /* kaski za tasma */
        const Bike *bk = &g_tour.bikes[g];
        WORD cx = (WORD)(SW_X + 26 + g * 20), cy = SW_Y + 44, dx, dy;
        if (bk->state == BIKE_EXCLUDED) continue;
        for (dy = -5; dy <= 5; dy++)
            for (dx = -5; dx <= 5; dx++)
                if (dx * dx + dy * dy <= 26)
                    plot(b, cx + dx, cy + dy, dy == -1 ? U_SHADOW : (UBYTE)(U_HELMET + g));   /* gogle */
    }
    for (y = SW_Y + 6; y < SW_Y + SW_H - 4; y++)             /* slupki maszyny */
        for (x = 0; x < 3; x++) {
            UBYTE c = ((y >> 2) & 1) ? U_RED : U_WHITE;
            plot(b, SW_X + 8 + x, y, c);
            plot(b, SW_X + SW_W - 11 + x, y, c);
        }
    for (x = SW_X + 8; x < SW_X + SW_W - 8; x++) {           /* poprzeczka i lampka */
        plot(b, x, SW_Y + 6, U_GREY);
        plot(b, x, SW_Y + 7, U_GREY);
    }
    for (y = SW_Y + 8; y < SW_Y + 13; y++)
        for (x = SW_X + 52; x < SW_X + 60; x++) plot(b, x, y, lamp_col[st & 3]);

    /* tasma: dwie nitki; przy odlocie ramiona unosza konce, a srodek wygina sie w dol */
    {
        WORD ty = (WORD)(SW_Y + 40 - k * k * 3), half = (SW_W - 22) / 2, mid = SW_X + SW_W / 2;
        for (x = SW_X + 11; x < SW_X + SW_W - 11; x++) {
            WORD d = (WORD)(x - mid), bow = (WORD)(k * 3 * (half * half - d * d) / (half * half));
            WORD yy = (WORD)(ty + bow);
            if (yy >= SW_Y + 3 && yy < SW_Y + SW_H - 6) {
                plot(b, x, yy, U_WHITE);
                plot(b, x, yy + 4, U_WHITE);
            }
        }
    }
}

static void update_startwin(UBYTE *b, UBYTE bi)
{
    UBYTE st = startwin_state();
    if (st == startwin_drawn[bi]) return;
    if (st == SW_NONE) blit_copy(b, g_video.background, SW_X, SW_Y, SW_W, SW_H);
    else draw_startwin(b, st);
    startwin_drawn[bi] = st;
}

static BOOL state_uses_track(GameState s)
{
    return s == GS_HEAT || s == GS_HEAT_RESULT || s == GS_TRACK_GRADING;
}

static void update_line_colors(void)
{
    UWORD i;
    if (state_uses_track(g_tour.state)) {
        /* ramka obrazu (overscan) w kolorze betonu trybun - bez czarnych pasow po bokach toru */
        for (i = 0; i < PLAY_H; i++) { line_bg[i] = BORDER_RACE; line_fg[i] = 0xFFF; }
    } else {
        UWORD ph = (UWORD)(g_frame >> 1);
        for (i = 0; i < FULL_H; i++) {
            /* tlo: granat -> fiolet -> granat; tekst: przesuwajaca sie tecza */
            UWORD t = (UWORD)((i < FULL_H / 2 ? i : FULL_H - 1 - i) * 13 / 16);
            line_bg[i] = (UWORD)(((t / 26) << 8) | ((t / 52) << 4) | (2 + t / 18));
            line_fg[i] = rainbow[(i / 3 + ph) % 48];
        }
    }
    video_set_lines(line_bg, line_fg);
}

void ui_render(void)
{
    UBYTE *b = back();
    UBYTE bi = g_video.back;
    UBYTE new_blink = (UBYTE)((g_frame >> 4) & 1);

    if (new_blink != blink) { blink = new_blink; ui_invalidate(); }
    video_set_menu(!state_uses_track(g_tour.state));   /* menu: caly ekran bez HUD */
    update_line_colors();

    if (state_uses_track(g_tour.state)) {
        if (!buf_is_track[bi]) {
            blit_copy(b, g_video.background, 0, 0, SCREEN_W, PLAY_H);
            buf_is_track[bi] = 1;
            tape_drawn[bi] = 0xFF;
            roost_forget_buffer(bi);
            track_forget_wear(bi);
            startwin_drawn[bi] = SW_NONE;
            drawn_serial[bi] = 0;
        }
        track_flush_wear(bi, b);                          /* wyjezdzone sciezki */
        update_startwin(b, bi);                           /* przed nakladkami - nie tnie panelu */
        if (drawn_serial[bi] != serial) {
            blit_copy(b, g_video.background, PANEL_X, PANEL_Y, PANEL_W, PANEL_H);
            switch (current_overlay()) {
            case OV_FALSE_START: draw_false_start(b); break;
            case OV_RESULT:      draw_result(b); break;
            case OV_GRADING:     draw_grading(b); break;
            default: break;
            }
            drawn_serial[bi] = serial;
        }
        render_tape(b);
        return;
    }

    if (drawn_serial[bi] == serial && buf_is_track[bi] == 0) return;
    blit_clear(b);
    buf_is_track[bi] = 0;
    roost_forget_buffer(bi);
    drawn_serial[bi] = serial;
    switch (g_tour.state) {
    case GS_TITLE:     draw_title(b); break;
    case GS_SETUP:     draw_setup(b); break;
    case GS_TABLE:     draw_table(b); break;
    case GS_GATE_PICK: draw_pick(b); break;
    case GS_PODIUM:    draw_podium(b); break;
    default: break;
    }
}
