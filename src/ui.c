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
#include "hof.h"

/* kolory palety interfejsu (track.c) */
enum { U_PANEL = 96, U_PANEL2, U_BORDER, U_WHITE, U_RAINBOW, U_GOLD, U_GREY,
       U_HELMET, U_CURSOR = 107, U_SHADOW, U_SILVER, U_BRONZE, U_GREEN, U_RED };

#define PANEL_X 48
#define PANEL_Y 24
#define PANEL_W 224
#define PANEL_H 112
#define NAME_EDIT_MAX 13

typedef enum { OV_NONE = 0, OV_FALSE_START, OV_RESULT, OV_GRADING, OV_PAUSE, OV_HOLD } Overlay;

static UBYTE serial = 1, drawn_serial[2];
static UBYTE panel_ov[2];                /* nakladka narysowana w panelu bufora (OV_NONE = pusty) */
static UBYTE buf_is_track[2];
static UBYTE tape_drawn[2] = { 0xFF, 0xFF };
static UBYTE blink;
static UBYTE blink_dirty[2];        /* tylko migajacy napis do odswiezenia (bez przerysowania ekranu) */

/* menu ustawien */
static UBYTE setup_row;
static BOOL  editing;
static UBYTE pick_cursor, pick_prev;

#define BORDER_RACE 0x334          /* 12-bit: ciemny beton trybun */
static UWORD line_bg[FULL_H], line_fg[FULL_H], rainbow[48];
static UBYTE *title_img;            /* plansza tytulowa 320x256, 8 planow (Chip RAM) */
static UBYTE *girls_img;            /* podprowadzajace (data/girls.bin) */
#define GIRL_W      48
#define GIRL_H      96
#define GIRL_BYTES  (GIRL_W / 8)
#define GIRL_SIZE   ((ULONG)GIRL_H * PLAY_DEPTH * GIRL_BYTES)
static UBYTE *sw_cache;             /* tlo okienka startu bez tasmy (wiersze okienka, uklad ekranu) */
/* okienko maszyny startowej: na srodku ekranu, na murawie wewnatrz owalu (zawodnicy przy
 * starcie stoja na dolnej prostej, y >= 140). Lezy w obszarze nakladek - gdy nakladka jest
 * na ekranie (czekamy na start, falstart, pauza), okienko sie chowa. */
#define SW_X 96                    /* 96..223: srodek ekranu, wyrownane do slowa (Blitter) */
#define SW_Y 67                    /* srodek murawy (CY = 100) */
#define SW_W 128
#define SW_H 66


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

/* Plansza tytulowa (ilustracja i jej paleta) - wczytywana jako pierwsza, zeby pokazac ja
 * z napisem WCZYTYWANIE, zanim dojdzie reszta danych z dyskietki. */
static BOOL title_loaded;
COLD void ui_init_title(void)
{
    if (title_loaded) return;
    title_loaded = TRUE;
    rgb12_lerp_table();
    /* ilustracja z tools/genart.py; bez pliku plansza ma sam gradient i napisy */
    title_img = AllocMem(FULL_SIZE, MEMF_CHIP);
    if (title_img && !asset_load("PROGDIR:data/title.bpl", title_img, FULL_SIZE)) {
        FreeMem(title_img, FULL_SIZE);
        title_img = 0;
    }
    if (title_img) {
        /* ilustracja ma wlasna palete (kolory napisow menu bez zmian) - dla list Coppera menu;
         * przy pierwszej zmianie wilgotnosci menu wraca do palety toru (napisy te same) */
        static UBYTE raw[256 * 3];
        static RGB24 pal[256];
        UWORD i;
        if (asset_load("PROGDIR:data/title.pal", raw, sizeof(raw))) {
            for (i = 0; i < 256; i++) { pal[i].r = raw[i * 3]; pal[i].g = raw[i * 3 + 1]; pal[i].b = raw[i * 3 + 2]; }
            video_set_menu_palette(pal);
        }
    }
}

static void update_line_colors(void);

/* Plansza wczytywania: ilustracja i napisy jak na planszy tytulowej, zamiast NACISNIJ FIRE
 * stale WCZYTYWANIE. Rysowana procesorem (Blitter nalezy jeszcze do systemu) do bufora "back",
 * potem na ekran (video_swap, video_start). Wywolywac po system_show_early - najpierw przed
 * wczytaniem ilustracji (sam teczowy napis na gradiencie: paleta ilustracji jeszcze nie
 * wczytana, a teczowe napisy i tlo ustawia Copper liniami), potem drugi raz z ilustracja. */
COLD void ui_loading_screen(void)
{
    UBYTE *b = video_backbuffer();
    rgb12_lerp_table();                          /* kolory teczy dla napisow (Copper, linie) */
    if (!title_img) {
        ULONG *d = (ULONG *)b, n;
        for (n = 0; n < FULL_SIZE / 4; n++) d[n] = 0;      /* kolor 0 = gradient z Coppera */
        text_draw_big_center(b, 100, "SPEEDWAY PARTY", U_RAINBOW, 0);
        text_draw_center(b, 140, T("WCZYTYWANIE...", "LOADING..."), U_RAINBOW, 0);
        video_set_menu(TRUE);
        update_line_colors();
        video_swap();
        video_start();
        return;
    }
    {
        const ULONG *s = (const ULONG *)title_img;
        ULONG *d = (ULONG *)b, n;
        for (n = 0; n < FULL_SIZE / 4; n++) d[n] = s[n];
    }
    text_draw_big_center(b, 12, "SPEEDWAY", U_RAINBOW, U_SHADOW);
    text_draw_big_center(b, 38, "PARTY", U_GOLD, U_SHADOW);
    text_draw_center(b, 64, "AMIGA 1200", U_WHITE, U_SHADOW);
    text_draw_center(b, 206, T("WCZYTYWANIE...", "LOADING..."), U_WHITE, U_SHADOW);
    text_draw_center(b, 240, "MIT © 2026 b4rtiga", U_GREY, 0);
    video_set_menu(TRUE);
    update_line_colors();                        /* gradient tla i teczowe napisy (lista "back") */
    video_swap();
    video_start();
}

COLD void ui_init(void)
{
    ui_init_title();
    sw_cache = AllocMem((ULONG)SW_H * PLAY_STRIDE, MEMF_ANY);   /* bez pamieci: pelne przerysowanie */
    girls_img = AllocMem(4 * GIRL_SIZE, MEMF_ANY);
    if (girls_img && !asset_load("PROGDIR:data/girls.bin", girls_img, 4 * GIRL_SIZE)) {
        FreeMem(girls_img, 4 * GIRL_SIZE);
        girls_img = 0;                  /* bez pliku: same tablice */
    }
}

COLD void ui_free(void)
{
    if (title_img) FreeMem(title_img, FULL_SIZE);
    if (sw_cache) FreeMem(sw_cache, (ULONG)SW_H * PLAY_STRIDE);
    if (girls_img) FreeMem(girls_img, 4 * GIRL_SIZE);
}


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

static void helmet_box(UBYTE *buf, WORD x, WORD y, UBYTE color)
{
    blit_fill(buf, x, y, 16, 10, (UBYTE)(U_HELMET + color));
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
#ifdef HOST_SIM
const char *g_endcard[2];               /* trailer (tools/shots): ostatnia plansza - ilustracja i 2 linie */
#endif

static COLD void draw_title(UBYTE *b)
{
    if (title_img) blit_copy(b, title_img, 0, 0, SCREEN_W, FULL_H);
#ifdef HOST_SIM
    if (g_endcard[0]) {
        text_draw_big_center(b, 14, g_endcard[0], U_GOLD, U_SHADOW);        /* na ciemnym niebie */
        text_draw_big_center(b, 40, g_endcard[1], U_GOLD, U_SHADOW);
        return;
    }
#endif
    text_draw_big_center(b, 12, "SPEEDWAY", U_RAINBOW, U_SHADOW);
    text_draw_big_center(b, 38, "PARTY", U_GOLD, U_SHADOW);
    text_draw_center(b, 64, "AMIGA 1200", U_WHITE, U_SHADOW);
    if (blink) text_draw_center(b, 206, T("NACIŚNIJ FIRE", "PRESS FIRE"), U_RAINBOW, U_SHADOW);
    text_draw_center(b, 240, "MIT © 2026 b4rtiga", U_GREY, 0);
}

/* ==================== WYBOR PO PLANSZY TYTULOWEJ ==================== */

/* Na tle ilustracji tytulowej: TURNIEJ (dalej do ustawien), INSTRUKCJA, REKORDY albo CREDITS. */
#define MENU_ITEMS 4
#define MENU_Y0    146                      /* pierwsza pozycja; kolejne co MENU_DY */
#define MENU_DY    20
static UBYTE menu_row;

void ui_menu_enter(UBYTE row) { menu_row = (UBYTE)(row < MENU_ITEMS ? row : 0); }

COLD UBYTE ui_menu_update(void)
{
    UBYTE m = input_menu();
    if (m & (BTN_UP | BTN_LEFT))    { menu_row = (UBYTE)((menu_row + MENU_ITEMS - 1) % MENU_ITEMS); ui_invalidate(); }
    if (m & (BTN_DOWN | BTN_RIGHT)) { menu_row = (UBYTE)((menu_row + 1) % MENU_ITEMS); ui_invalidate(); }
    if (ui_confirm()) return (UBYTE)(menu_row + 1);
    return 0;
}

static COLD void draw_menu(UBYTE *b)
{
    static const char *items[MENU_ITEMS] = { T("TURNIEJ", "TOURNAMENT"), T("INSTRUKCJA", "HOW TO PLAY"), T("REKORDY", "RECORDS"), "CREDITS" };
    UBYTE i;
    if (title_img) blit_copy(b, title_img, 0, 0, SCREEN_W, FULL_H);
    text_draw_big_center(b, 12, "SPEEDWAY", U_RAINBOW, U_SHADOW);
    text_draw_big_center(b, 38, "PARTY", U_GOLD, U_SHADOW);
    text_draw_center(b, 64, "AMIGA 1200", U_WHITE, U_SHADOW);
    for (i = 0; i < MENU_ITEMS; i++) {
        WORD y = (WORD)(MENU_Y0 + i * MENU_DY);
        if (i == menu_row) blit_fill(b, 96, y - 4, 128, 17, U_CURSOR);
        text_draw_center(b, y, items[i], i == menu_row ? U_RAINBOW : U_WHITE, U_SHADOW);
    }
    text_draw_center(b, 240, "MIT © 2026 b4rtiga", U_GREY, 0);
}

/* ======================== PLANSZA TWORCOW ========================= */

static COLD void draw_credits(UBYTE *b)
{
    text_draw_big_center(b, 10, "SPEEDWAY PARTY", U_RAINBOW, U_SHADOW);
    text_draw_center(b, 62, T("AUTOR GRY: b4rtiga", "GAME BY: b4rtiga"), U_WHITE, U_SHADOW);
    text_draw_center(b, 86, T("PRZY POMOCY SZTUCZNEJ INTELIGENCJI:", "MADE WITH THE HELP OF AI:"), U_GREY, 0);
    text_draw_center(b, 98, T("CLAUDE (ANTHROPIC) ORAZ GPT-6 ASTRA", "CLAUDE (ANTHROPIC) AND GPT-6 ASTRA"), U_WHITE, U_SHADOW);
    text_draw_center(b, 122, T("KOD ŹRÓDŁOWY (LICENCJA MIT):", "SOURCE CODE (MIT LICENSE):"), U_GREY, 0);
    text_draw_center(b, 134, "github.com/b4rtiga/Speedway-Party", U_GOLD, U_SHADOW);
    text_draw_center(b, 160, T("DZIĘKUJĘ ZA ZAINTERESOWANIE GRĄ!", "THANK YOU FOR PLAYING!"), U_WHITE, U_SHADOW);
    text_draw_center(b, 176, T("ŻYCZĘ UDANEJ ZABAWY", "HAVE FUN"), U_WHITE, U_SHADOW);
    text_draw_center(b, 188, T("I SZEROKIEJ NA OSTATNIM ŁUKU!", "AND GO WIDE ON THE LAST BEND!"), U_GOLD, U_SHADOW);
    if (blink) text_draw_center(b, 238, T("FIRE - POWRÓT", "FIRE - BACK"), U_RAINBOW, U_SHADOW);
}

/* ======================== REKORDY TORU ========================== */

static char *fmt_time(char *p, UWORD frames, BOOL hundredths);
static UBYTE rawkey_char(UBYTE k);

/* tabela TOP 5: "1. MAREK    23.34 S" - puste miejsca jako kreski */
static COLD void draw_hof_list(UBYTE *b, WORD y, const char *title, const HofEntry *list)
{
    UBYTE i, k;
    char s[24], *p;
    text_draw_center(b, y, title, U_GOLD, U_SHADOW);
    for (i = 0; i < HOF_N; i++) {
        const HofEntry *e = &list[i];
        WORD ry = (WORD)(y + 16 + i * 13);
        p = text_uint(s, (ULONG)(i + 1), 1);
        text_cat(p, ".");
        text_draw(b, 88, ry, s, U_GREY, 0);
        if (!e->frames) { text_draw(b, 112, ry, "-------", U_GREY, 0); text_draw(b, 184, ry, "--.--", U_GREY, 0); continue; }
        for (k = 0; k < HOF_NAME; k++) s[k] = e->name[k];
        s[HOF_NAME] = 0;
        text_draw(b, 112, ry, s, i == 0 ? U_GOLD : U_WHITE, U_SHADOW);
        p = fmt_time(s, e->frames, TRUE);
        text_cat(p, " S");
        text_draw(b, 184, ry, s, i == 0 ? U_GOLD : U_WHITE, U_SHADOW);
    }
}

static COLD void draw_records(UBYTE *b)
{
    const HofTrack *h = &g_hof[g_tour.track.type % NUM_TRACKS];
    char s[32];
    text_draw_big_center(b, 8, T("REKORDY", "RECORDS"), U_RAINBOW, U_SHADOW);
    text_cat(text_cat(s, T("TOR: ", "TRACK: ")), track_name(g_tour.track.type));
    text_draw_center(b, 34, s, U_WHITE, U_SHADOW);
    draw_hof_list(b, 54, T("NAJLEPSZE BIEGI", "BEST HEATS"), h->heat);
    draw_hof_list(b, 140, T("NAJLEPSZE OKRĄŻENIA", "BEST LAPS"), h->lap);
    if (blink) text_draw_center(b, 238, T("FIRE - POWRÓT", "FIRE - BACK"), U_RAINBOW, U_SHADOW);
}

/* ---- wpis imienia: gracz, ktory wjechal do TOP 5 ---- */

static void rect_px(UBYTE *b, WORD x, WORD y, WORD w, WORD h, UBYTE c);

/* znaki pod GORA/DOL: spacja, A-Z, polskie litery (0x80..0x88), cyfry, '-' */
static const char entry_chars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ" "\x80\x81\x82\x83\x84\x85\x86\x87\x88" "0123456789-";
static UBYTE entry_rider, entry_pos;
static char  entry_buf[HOF_NAME + 1];

COLD void ui_name_entry_enter(UBYTE rider)
{
    entry_rider = rider;
    entry_pos = 0;
    hof_default_name(entry_buf, g_tour.riders[rider].name);
}

const char *ui_name_entry(void) { return entry_buf; }

static char entry_step(char c, BYTE dir)
{
    UBYTE i, n = sizeof(entry_chars) - 1;
    for (i = 0; i < n && entry_chars[i] != c; i++) ;
    if (i == n) i = 0;
    return entry_chars[(i + n + dir) % n];
}

/* GORA/DOL - znak, LEWO/PRAWO - pozycja, pisanie z klawiatury (Alt = polskie litery),
 * Backspace - cofnij, FIRE/Return - zatwierdz */
COLD BOOL ui_name_entry_update(void)
{
    UBYTE m = input_menu(), k;
    BOOL changed = FALSE, done = FALSE;
    while ((k = key_get()) != 0xFF) {
        UBYTE c;
        if (k == KEY_RETURN) { done = TRUE; continue; }
        if (k == KEY_BACKSPACE) {
            if (entry_pos) entry_pos--;
            entry_buf[entry_pos] = ' ';
            changed = TRUE;
            continue;
        }
        c = rawkey_char(k);
        if (c && hof_valid_char((char)c)) {
            entry_buf[entry_pos] = (char)c;
            if (entry_pos < HOF_NAME - 1) entry_pos++;
            changed = TRUE;
        }
    }
    if (m & BTN_UP)    { entry_buf[entry_pos] = entry_step(entry_buf[entry_pos], 1); changed = TRUE; }
    if (m & BTN_DOWN)  { entry_buf[entry_pos] = entry_step(entry_buf[entry_pos], -1); changed = TRUE; }
    if ((m & BTN_LEFT) && entry_pos > 0)             { entry_pos--; changed = TRUE; }
    if ((m & BTN_RIGHT) && entry_pos < HOF_NAME - 1) { entry_pos++; changed = TRUE; }
    if (m & BTN_FIRE) done = TRUE;
    if (changed) ui_invalidate();
    return done;
}

/* miejsce gracza czekajace na imie: "BIEG - 2. MIEJSCE  23.34 S" */
static COLD void entry_place(UBYTE *b, WORD y, const char *what, const HofEntry *list)
{
    UBYTE i;
    char s[32], *p;
    for (i = 0; i < HOF_N; i++)
        if (list[i].frames && (UBYTE)list[i].name[HOF_NAME] == (UBYTE)(entry_rider + 1)) {
            p = text_cat(s, what);
            p = text_cat(p, T("", "PLACE "));          /* PL: "2. MIEJSCE", EN: "PLACE 2" */
            p = text_uint(p, (ULONG)(i + 1), 1);
            p = text_cat(p, T(". MIEJSCE  ", "  "));
            p = fmt_time(p, list[i].frames, TRUE);
            text_cat(p, " S");
            text_draw_center(b, y, s, i == 0 ? U_GOLD : U_WHITE, U_SHADOW);
            return;
        }
}

static COLD void draw_name_entry(UBYTE *b)
{
    const HofTrack *h = &g_hof[g_tour.track.type % NUM_TRACKS];
    char s[NAME_LEN + 1];
    UBYTE i;
    text_draw_big_center(b, 10, T("REKORDY", "RECORDS"), U_RAINBOW, U_SHADOW);
    text_draw_center(b, 38, T("WJECHAŁEŚ DO PIĄTKI NAJLEPSZYCH!", "YOU MADE THE TOP FIVE!"), U_GOLD, U_SHADOW);
    helmet_box(b, 96, 58, entry_rider);
    name_trunc(s, g_tour.riders[entry_rider].name, 12);
    text_draw(b, 120, 59, s, U_WHITE, U_SHADOW);
    entry_place(b, 82, T("BIEG - ", "HEAT - "), h->heat);
    entry_place(b, 96, T("OKRĄŻENIE - ", "LAP - "), h->lap);
    text_draw_center(b, 120, T("WPISZ IMIĘ:", "ENTER YOUR NAME:"), U_GREY, 0);
    for (i = 0; i < HOF_NAME; i++) {
        WORD x = (WORD)(56 + i * 32);        /* tlo kursora od x-8: wyrownane do slowa */
        char c[2];
        c[0] = entry_buf[i]; c[1] = 0;
        if (i == entry_pos && blink) blit_fill(b, x - 8, 136, 32, 28, U_CURSOR);
        text_draw_big(b, x, 140, c, i == entry_pos ? U_RAINBOW : U_WHITE, U_SHADOW);
        rect_px(b, x, 161, 16, 1, U_GREY);   /* kreska pod polem - widac tez spacje */
    }
    text_draw_small_center(b, 186, T("GÓRA/DÓŁ - ZNAK, LEWO/PRAWO - POZYCJA", "UP/DOWN - LETTER, LEFT/RIGHT - POSITION"), U_GREY);
    text_draw_small_center(b, 197, T("MOŻNA TEŻ PISAĆ Z KLAWIATURY", "OR TYPE ON THE KEYBOARD"), U_GREY);
    text_draw_center(b, 224, T("FIRE - ZAPISZ", "FIRE - SAVE"), U_WHITE, U_SHADOW);
}

/* ========================== INSTRUKCJA =========================== */

/* Trzy strony tekstu fontem gry (najwyzej 40 znakow w wierszu); na trzeciej schemat owalu
 * z idealna linia. Tresc zgodna z mechanika (race.c, start.c, track.c, tournament.c). */
#define HELP_PAGES 3
static UBYTE help_page;
static void rect_px(UBYTE *b, WORD x, WORD y, WORD w, WORD h, UBYTE c);

static const char *const help_title[HELP_PAGES] = { T("ZASADY TURNIEJU", "TOURNAMENT RULES"), T("START I STEROWANIE", "START AND CONTROLS"), T("LINIA JAZDY", "RACING LINE") };
static const char *const help_text0[] = {
    T("TURNIEJ TO 15 BIEGÓW PO 4 OKRĄŻENIA.", "THE TOURNAMENT HAS 15 HEATS OF 4 LAPS."),
    T("W KAŻDYM BIEGU JADĄ CZTEREJ ZAWODNICY,", "FOUR RIDERS RACE IN EACH HEAT."),
    T("PUNKTY ZA MIEJSCA: 3 - 2 - 1 - 0.", "POINTS FOR PLACES: 3 - 2 - 1 - 0."),
    T("KAŻDY STARTUJE Z KAŻDEGO POLA A-D.", "EVERYONE STARTS FROM EACH GATE A-D."),
    T("W BIEGACH NOMINOWANYCH LIDER TABELI", "IN THE NOMINATED HEATS THE LEADER"),
    T("WYBIERA POLE PIERWSZY. REMIS O PODIUM", "PICKS A GATE FIRST. A TIE FOR THE"),
    T("ROZSTRZYGA BIEG DODATKOWY.", "PODIUM IS DECIDED BY A RUN-OFF."),
    T("PO BIEGACH 4, 7, 11 I 13 TOR JEST", "AFTER HEATS 4, 7, 11 AND 13 THE TRACK"),
    T("RÓWNANY I POLEWANY, POTEM PRZESYCHA.", "IS GRADED AND WATERED, THEN DRIES."),
    "",
    T("FLAGI SĘDZIEGO:", "REFEREE FLAGS:"),
    T("CZERWONA - PRZERWANIE BIEGU", "RED - HEAT STOPPED"),
    T("CZARNA - WYKLUCZENIE ZAWODNIKA", "BLACK - RIDER EXCLUDED"),
    T("ŻÓŁTA Z KRZYŻEM - OSTATNIE OKRĄŻENIE", "YELLOW WITH CROSS - LAST LAP"),
    T("SZACHOWNICA - KONIEC BIEGU", "CHEQUERED - END OF THE HEAT"),
    0 };
static const char *const help_text1[] = {
    T("GAZ JEST ZAWSZE ODKRĘCONY DO KOŃCA.", "THE THROTTLE IS ALWAYS WIDE OPEN."),
    T("LEWO - ŁAMIESZ MOTOCYKL W UŚLIZG,", "LEFT - LAY THE BIKE INTO A SLIDE,"),
    T("       SKRĘCASZ W ŁUK (DO 40 STOPNI).", "       TURNING INTO THE BEND (40 DEG)."),
    T("PRAWO - KONTRA, PROSTUJE MOTOCYKL.", "RIGHT - COUNTER-STEER, STRAIGHTEN UP."),
    "",
    T("START SPOD TAŚMY:", "TAPE START:"),
    T("1. TRZYMAJ LEWO LUB PRAWO.", "1. HOLD LEFT OR RIGHT."),
    T("2. PATRZ NA ŚWIATŁA I OKIENKO TAŚMY.", "2. WATCH THE LIGHTS AND THE TAPE."),
    T("3. PUŚĆ OD RAZU NA ZIELONYM!", "3. RELEASE AT ONCE ON GREEN!"),
    T("IM SZYBCIEJ PUŚCISZ, TYM LEPSZY START.", "THE FASTER YOU RELEASE, THE BETTER."),
    T("ZA WCZEŚNIE - TAŚMA I OSTRZEŻENIE,", "TOO EARLY - TAPE AND A WARNING,"),
    T("DRUGA TAŚMA - WYKLUCZENIE Z BIEGU.", "SECOND TAPE - EXCLUDED FROM THE HEAT."),
    T("ZA PÓŹNO - KOŁO MIELI W MIEJSCU.", "TOO LATE - THE WHEEL SPINS IN PLACE."),
    "",
    T("ESC - PAUZA.", "ESC - PAUSE."),
    0 };
static const char *const help_text2[] = {
    T("NAJLEPSZA LINIA JAZDY JEST DYNAMICZNA:", "THE BEST RACING LINE KEEPS CHANGING:"),
    T("NA KAŻDYCH ZAWODACH TOR UKŁADA SIĘ", "AT EVERY MEETING THE TRACK WEARS IN"),
    T("INACZEJ. SZUKAJ JEJ OD PIERWSZEGO BIEGU.", "DIFFERENTLY. FIND IT FROM HEAT ONE."),
    T("NA NIEJ UŚLIZG MNIEJ HAMUJE.", "ON IT A SLIDE COSTS LESS SPEED."),
    T("NA PROSTEJ JEDŹ PROSTO: UŚLIZG HAMUJE.", "STRAIGHTS: NO SLIDING, IT SLOWS YOU."),
    T("W ŁUKU PULSUJ PRZYCISKIEM, NIE TRZYMAJ.", "IN BENDS TAP THE BUTTON, DON'T HOLD IT."),
    T("LUŹNE ODSYPISKO POD BANDĄ ZAKOPUJE", "LOOSE DIRT BY THE FENCE BOGS DOWN"),
    T("WOLNYCH. SZYBKO I W LEKKIM UŚLIZGU -", "SLOW RIDERS. FAST AND IN A LIGHT SLIDE"),
    T("SZEROKA NIESIE. OMIJAJ SZPRYCĘ I BANDĘ.", "IT CARRIES YOU. AVOID ROOST AND FENCE."),
    0 };
static const char *const *const help_text[HELP_PAGES] = { help_text0, help_text1, help_text2 };

void ui_help_enter(UBYTE page) { help_page = (UBYTE)(page < HELP_PAGES ? page : 0); }

COLD BOOL ui_help_update(void)
{
    UBYTE m = input_menu();
    if ((m & BTN_LEFT) && help_page > 0)               { help_page--; ui_invalidate(); }
    if ((m & BTN_RIGHT) && help_page < HELP_PAGES - 1) { help_page++; ui_invalidate(); }
    return ui_confirm();
}

/* Schemat owalu na stronie 3: banda, kraweznik i kierunek jazdy (najlepsza linia jest
 * inna w kazdym turnieju - track_random_surface - wiec schemat jej nie rysuje).
 * s = 0..255 wokol owalu od srodka dolnej prostej, przeciwnie do wskazowek zegara;
 * r = odleglosc od osi owalu (promien luku). */
#define HD_CX   160
#define HD_CY   82
#define HD_HALF 58                              /* polowa prostej */
static COLD void help_oval_point(WORD s, WORD r, WORD *x, WORD *y)
{
    if (s < 32 || s >= 224) {                   /* dolna prosta: w prawo */
        WORD t = (WORD)(s < 32 ? s : s - 256);
        *x = (WORD)(HD_CX + t * HD_HALF / 32); *y = (WORD)(HD_CY + r);
    } else if (s < 96) {                        /* prawy luk: w gore */
        angle8 a = (angle8)(64 - (s - 32) * 2);
        *x = (WORD)(HD_CX + HD_HALF + (((LONG)COS(a) * r) >> 14));
        *y = (WORD)(HD_CY + (((LONG)SIN(a) * r) >> 14));
    } else if (s < 160) {                       /* gorna prosta: w lewo */
        *x = (WORD)(HD_CX + HD_HALF - (s - 96) * HD_HALF / 32); *y = (WORD)(HD_CY - r);
    } else {                                    /* lewy luk: w dol */
        angle8 a = (angle8)(192 - (s - 160) * 2);
        *x = (WORD)(HD_CX - HD_HALF + (((LONG)COS(a) * r) >> 14));
        *y = (WORD)(HD_CY + (((LONG)SIN(a) * r) >> 14));
    }
}

static COLD void draw_help_oval(UBYTE *b)
{
    WORD s, x, y;
    for (s = 0; s < 256; s++) {
        help_oval_point(s, 36, &x, &y); blit_plot(b, x, y, U_WHITE);        /* banda */
        help_oval_point(s, 13, &x, &y); blit_plot(b, x, y, U_RED);          /* kraweznik */
    }
    for (s = 0; s < 4; s++)                                            /* strzalka: jazda w prawo */
        rect_px(b, HD_CX - 4 + s * 2, HD_CY + 20 + s, 2, (WORD)(7 - 2 * s), U_GREEN);
}

static COLD void draw_help(UBYTE *b)
{
    const char *const *t = help_text[help_page];
    char s[32], *p;
    WORD y = (WORD)(help_page == 2 ? 124 : 52);
    text_draw_big_center(b, 6, T("INSTRUKCJA", "HOW TO PLAY"), U_RAINBOW, U_SHADOW);
    p = text_cat(s, help_title[help_page]);
    p = text_cat(p, "  ");
    p = text_uint(p, (ULONG)(help_page + 1), 1);
    p = text_cat(p, "/");
    text_uint(p, HELP_PAGES, 1);
    text_draw_center(b, 32, s, U_GOLD, 0);
    if (help_page == 2) draw_help_oval(b);
    for (; *t; t++, y += 11) text_draw_center(b, y, *t, U_WHITE, U_SHADOW);
    text_draw_small_center(b, 240, T("LEWO/PRAWO - STRONA   FIRE - POWRÓT", "LEFT/RIGHT - PAGE   FIRE - BACK"), U_GREY);
}

/* ======================== GAZETA PO ZAWODACH ======================= */

/* "GLOS OWALU": kremowy papier i grafitowy tekst (kolor 0 i 100 ustawia Copper - ta sama
 * paleta co reszta menu), czerwony akcent, linie i raster z pikseli. Dane z news_summary(). */
#define NEWS_PAPER 0xEDB                     /* 12-bit: kremowy papier (kolor 0) */
#define NEWS_INK   0x222                     /* grafit (kolor 100 = U_RAINBOW na tym ekranie) */
static const char *const news_head[5][2] = {
    { T("TYTUŁ PO", "TITLE AFTER"),    T("DOGRYWCE!", "A RUN-OFF!") },
    { T("ZWYCIĘSTWO", "VICTORY"),  T("Z REKORDEM!", "WITH A RECORD!") },
    { T("BEZ STRATY", "A PERFECT"),  T("PUNKTU!", "MAXIMUM!") },
    { T("JEDEN PUNKT", "ONE POINT"), T("RÓŻNICY!", "IN IT!") },
    { T("OWAL MA", "THE OVAL HAS"),     T("ZWYCIĘZCĘ!", "A WINNER!") } };
static const char *const news_body[5][2] = {
    { T("O ZWYCIĘSTWIE ZDECYDOWAŁ", "THE TITLE WAS DECIDED"),       T("BIEG DODATKOWY.", "IN A RUN-OFF.") },
    { T("TRIUMFATOR ZAPISAŁ SIĘ TAKŻE", "THE WINNER ALSO MADE IT"),   T("W TABELI REKORDÓW TORU.", "INTO THE TRACK RECORDS.") },
    { T("W KAŻDYM SWOIM STARCIE", "THE WINNER WON"),         T("ZWYCIĘZCA BYŁ PIERWSZY.", "EVERY SINGLE RACE.") },
    { T("TAK NIEWIELE DZIELIŁO PIERWSZE", "ONLY THE SMALLEST MARGIN"), T("I DRUGIE MIEJSCE.", "SEPARATED FIRST AND SECOND.") },
    { T("TURNIEJ ROZSTRZYGNIĘTY.", "THE TOURNAMENT IS DECIDED."),        T("OTO KOŃCOWA KLASYFIKACJA.", "HERE ARE THE FINAL STANDINGS.") } };
static const char *const news_fact[5] = { T("FALSTARTY: ", "FALSE STARTS: "), T("WYKLUCZENIA: ", "EXCLUSIONS: "), T("ZWYCIĘSTWA LIDERA: ", "WINNER'S HEAT WINS: "), T("GRACZE PRZY STOLE: ", "HUMAN PLAYERS: "), T("DEFEKTY: ", "DEFECTS: ") };

/* liczba wyrownana do prawej krawedzi x1 */
static COLD void text_uint_right(UBYTE *b, WORD x1, WORD y, ULONG v, UBYTE c)
{
    char s[8];
    text_uint(s, v, 1);
    text_draw(b, (WORD)(x1 - text_len(s) * FONT_W), y, s, c, 0);
}

static COLD void draw_news(UBYTE *b)
{
    const NewsSummary *n = news_summary();
    char s[48], *p;
    UBYTE i;
    WORD x;
    text_draw_big_center(b, 4, T("GŁOS OWALU", "OVAL HERALD"), U_SHADOW, 0);
    p = text_cat(s, T("WYDANIE SPECJALNE / TURNIEJ NR ", "SPECIAL EDITION / TOURNAMENT NO. "));
    text_uint(p, n->number, 2);
    text_draw_center(b, 28, s, U_RAINBOW, 0);
    rect_px(b, 8, 40, SCREEN_W - 16, 2, U_SHADOW);
    rect_px(b, 8, 44, SCREEN_W - 16, 1, U_SHADOW);
    for (x = 8; x < SCREEN_W - 8; x += 4) rect_px(b, x, 46, 2, 1, U_RAINBOW);   /* raster */

    text_draw_big(b, 16, 52, news_head[n->rule][0], U_SHADOW, 0);
    text_draw_big(b, 16, 74, news_head[n->rule][1], U_RED, 0);
    p = name_trunc(s, n->name[0], NEWS_NAME);
    text_cat(p, T(" WYGRYWA TURNIEJ", " WINS THE TOURNAMENT"));
    text_draw(b, 16, 100, s, U_SHADOW, 0);
    text_draw(b, 16, 114, news_body[n->rule][0], U_RAINBOW, 0);
    text_draw(b, 16, 125, news_body[n->rule][1], U_RAINBOW, 0);

    rect_px(b, 16, 139, SCREEN_W - 32, 1, U_SHADOW);
    text_draw(b, 16, 143, T("MIEJSCE / ZAWODNIK", "PLACE / RIDER"), U_RAINBOW, 0);
    text_draw(b, 216, 143, T("PKT", "PTS"), U_RAINBOW, 0);
    text_draw(b, 256, 143, T("WYGR.", "WINS"), U_RAINBOW, 0);
    for (i = 0; i < MAX_RIDERS; i++) {
        WORD y = (WORD)(156 + i * 11);
        p = text_uint(s, (ULONG)(i + 1), 1);
        p = text_cat(p, ". ");
        name_trunc(p, n->name[i], NEWS_NAME);
        text_draw(b, 16, y, s, i == 0 ? U_RED : U_SHADOW, 0);
        text_uint_right(b, 240, y, n->points[i], U_SHADOW);
        text_uint_right(b, 296, y, n->wins[i], U_SHADOW);
    }
    rect_px(b, 16, 201, SCREEN_W - 32, 1, U_SHADOW);
    for (i = 0; i < 2; i++) {
        if (n->facts[i] == NEWS_FACT_NONE) continue;
        p = text_cat(s, news_fact[n->facts[i]]);
        text_uint(p, n->fact_value[i], 1);
        text_draw(b, 16, (WORD)(206 + i * 11), s, U_SHADOW, 0);
    }
    if (blink) text_draw_center(b, 240, T("FIRE - NASTĘPNY TURNIEJ", "FIRE - NEXT TOURNAMENT"), U_RED, 0);
}

COLD BOOL ui_title_update(void)
{
    return (input_menu() & BTN_FIRE) != 0;
}

/* ========================= USTAWIENIA ============================= */

#define SETUP_ROWS   7
#define ROW_OPTIONS  5
#define ROW_START    6
static const char *cond_names[4] = { T("SUCHY", "DRY"), T("WILGOTNY", "DAMP"), T("MOKRY", "WET"), T("LOSOWY", "RANDOM") };
static const char *port_names[INPUT_NONE] = {
    "JOY 1", "JOY 0", "JOY 3 LPT", "JOY 4 LPT", T("KLAW Z/X", "KEYS Z/X"), T("KLAW N/M", "KEYS N/M"), T("STRZAŁKI", "ARROWS")
};
static const char *level_names[3] = { T("NOWICJUSZ", "ROOKIE"), T("PROFESJONALISTA", "PROFESSIONAL"), T("MISTRZ", "MASTER") };

/* znaki do wyboru joystickiem */
static const UBYTE charset[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ\x80\x81\x82\x83\x84\x85\x86\x87\x88" "0123456789-.";

COLD void ui_setup_enter(void)
{
    setup_row = ROW_START;
    editing = FALSE;
}

void ui_setup_focus_options(void) { setup_row = ROW_OPTIONS; }

static WORD setup_row_y(UBYTE row) { return (WORD)(50 + row * 18); }

static const char *yes_no(BOOL v) { return v ? T("TAK", "YES") : T("NIE", "NO"); }

static COLD void draw_setup(UBYTE *b)
{
    char s[48], *p;
    UBYTE i;

    text_draw_big_center(b, 12, T("TURNIEJ", "TOURNAMENT"), U_RAINBOW, U_SHADOW);
    blit_fill(b, 16, setup_row_y(setup_row) - 4, 288, 17, editing ? U_GOLD : U_CURSOR);

    p = text_cat(s, T("LICZBA GRACZY:   < ", "PLAYERS:         < "));
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
        text_draw(b, 168, y, human ? T("GRACZ", "HUMAN") : "AI", human ? U_GREEN : U_GREY, U_SHADOW);
        text_draw(b, human ? 216 : 192, y, human ? port_names[r->input] : level_names[r->ai_level], U_GOLD, U_SHADOW);   /* PROFESJONALISTA = 15 znakow */
    }
    text_draw_center(b, setup_row_y(ROW_OPTIONS), T("USTAWIENIA", "SETTINGS"), setup_row == ROW_OPTIONS ? U_RAINBOW : U_WHITE, U_SHADOW);
    text_draw_center(b, setup_row_y(ROW_START), T("START TURNIEJU", "START TOURNAMENT"), setup_row == ROW_START ? U_RAINBOW : U_WHITE, U_SHADOW);
    /* skrot opcji pod lista */
    p = text_cat(s, T("TOR: ", "TRACK: "));
    p = text_cat(p, cond_names[g_tour.track_choice]);
    p = text_cat(p, T("  DEFEKTY: ", " DEFECTS: "));
    p = text_cat(p, yes_no(g_defects_enabled));
    p = text_cat(p, T("  MUZYKA: ", " MUSIC: "));
    text_cat(p, yes_no(g_music_enabled));
    text_draw_center(b, 182, s, U_GREY, 0);

    /* podpowiedzi o klawiszach malym fontem */
    text_draw_small_center(b, 216, editing ? T("PISZ Z KLAWIATURY / GÓRA-DÓŁ LITERA", "TYPE ON KEYBOARD / UP-DOWN LETTER")
                                           : T("GÓRA/DÓŁ: WYBÓR  LEWO/PRAWO: ZMIANA", "UP/DOWN: SELECT  LEFT/RIGHT: CHANGE"), U_GREY);
    text_draw_small_center(b, 227, editing ? T("PRAWO: DODAJ  LEWO: USUŃ  FIRE: OK", "RIGHT: ADD  LEFT: DELETE  FIRE: OK")
                                           : T("FIRE: NAZWISKO / USTAWIENIA / START", "FIRE: NAME / SETTINGS / START"), U_GREY);
    text_draw_small_center(b, 238, T("ESC - POWRÓT DO MENU", "ESC - BACK TO MENU"), U_GREY);
#ifdef SHOW_LAG
    {
        /* diagnostyka muzyki: wypelnione bufory, obiegi utworu, wolny Chip RAM (KiB) */
        extern ULONG g_music_fills, g_music_loops, g_chip_free, g_kb_bytes;
        extern UBYTE g_kb_last;
        p = text_cat(s, "M ");
        p = text_uint(p, g_music_fills, 1);
        p = text_cat(p, " L ");
        p = text_uint(p, g_music_loops, 1);
        p = text_cat(p, " C ");
        p = text_uint(p, g_chip_free >> 10, 1);
        p = text_cat(p, " K ");
        p = text_uint(p, g_kb_bytes, 1);
        p = text_cat(p, " ");
        text_uint(p, g_kb_last, 1);
        text_draw(b, 0, 0, s, U_RED, 0);
    }
#endif
}

static UBYTE name_len(const char *n) { UBYTE l = 0; while (n[l]) l++; return l; }

static COLD UBYTE rawkey_char(UBYTE k)
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

static COLD UBYTE charset_next(UBYTE c, BYTE dir)
{
    UBYTE i, n = sizeof(charset) - 1;
    for (i = 0; i < n && charset[i] != c; i++) ;
    if (i == n) i = 0;
    return charset[(i + n + dir) % n];
}

static COLD void edit_name(Rider *r, UBYTE m)
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
        if (!len) text_cat(r->name, T("ZAWODNIK", "RIDER"));
    }
}

COLD UBYTE ui_setup_update(void)
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
        return 0;
    }
    while (key_get() != 0xFF) ;      /* poza edycja ignorujemy pisanie */

    if (m & BTN_UP)   { setup_row = (UBYTE)((setup_row + SETUP_ROWS - 1) % SETUP_ROWS); changed = TRUE; }
    if (m & BTN_DOWN) { setup_row = (UBYTE)((setup_row + 1) % SETUP_ROWS); changed = TRUE; }

    if (m & (BTN_LEFT | BTN_RIGHT)) {
        BYTE d = (BYTE)(m & BTN_RIGHT ? 1 : -1);
        changed = TRUE;
        if (setup_row == 0) {
            WORD n = g_tour.num_humans + d;
            g_tour.num_humans = (UBYTE)(n < 0 ? 0 : n > MAX_RIDERS ? MAX_RIDERS : n);   /* 0 = pokaz: same boty */
            tournament_default_names();
        } else if (setup_row <= MAX_RIDERS) {
            Rider *r = &g_tour.riders[setup_row - 1];
            if (setup_row - 1 < g_tour.num_humans)
                r->input = (InputPort)((r->input + INPUT_NONE + d) % INPUT_NONE);
            else
                r->ai_level = (AiLevel)((r->ai_level + 3 + d) % 3);
        }
    }
    if (m & BTN_FIRE) {
        if (setup_row == ROW_START) return SETUP_START;
        if (setup_row == ROW_OPTIONS) return SETUP_OPTIONS;
        if (setup_row >= 1 && setup_row <= MAX_RIDERS) { editing = TRUE; changed = TRUE; }
    }
    if (changed) ui_invalidate();
    return 0;
}

/* ===================== USTAWIENIA TURNIEJU ========================= */

/* Z ekranu TURNIEJ (wiersz USTAWIENIA): stan toru na start (albo losowy), defekty, muzyka.
 * LEWO/PRAWO zmienia, FIRE zmienia albo wraca (POWROT), ESC wraca do ekranu TURNIEJ. */
#define OPT_ROWS    4
#define OPT_TRACK   0
#define OPT_DEFECTS 1
#define OPT_MUSIC   2
#define OPT_BACK    3
static UBYTE opt_row;

void ui_options_enter(void) { opt_row = OPT_TRACK; }

static WORD opt_row_y(UBYTE row) { return (WORD)(64 + row * 24); }

static COLD void draw_options(UBYTE *b)
{
    static const char *const label[3] = { T("STAN TORU:", "TRACK:"), T("DEFEKTY:", "DEFECTS:"), T("MUZYKA:", "MUSIC:") };
    static const char *const about[OPT_ROWS] = {
        T("STAN NAWIERZCHNI NA START TURNIEJU", "TRACK CONDITION AT THE START"),
        T("AWARIE MOTOCYKLI: OK. 1 NA TURNIEJ", "ENGINE FAILURES: ABOUT 1 PER EVENT"),
        T("MUZYKA W MENU", "MUSIC IN THE MENUS"),
        T("POWRÓT DO EKRANU TURNIEJU", "BACK TO THE TOURNAMENT SCREEN") };
    char s[24], *p;
    UBYTE i;
    text_draw_big_center(b, 12, T("USTAWIENIA", "SETTINGS"), U_RAINBOW, U_SHADOW);
    blit_fill(b, 16, opt_row_y(opt_row) - 4, 288, 17, U_CURSOR);
    for (i = 0; i < 3; i++) {
        text_draw(b, 40, opt_row_y(i), label[i], U_WHITE, U_SHADOW);
        p = text_cat(s, "< ");
        p = text_cat(p, i == OPT_TRACK ? cond_names[g_tour.track_choice] :
                        yes_no(i == OPT_DEFECTS ? g_defects_enabled : g_music_enabled));
        text_cat(p, " >");
        text_draw(b, 168, opt_row_y(i), s, U_GOLD, U_SHADOW);
    }
    text_draw_center(b, opt_row_y(OPT_BACK), T("POWRÓT", "BACK"), opt_row == OPT_BACK ? U_RAINBOW : U_WHITE, U_SHADOW);
    text_draw_center(b, 172, about[opt_row], U_GREY, 0);
    text_draw_small_center(b, 216, T("GÓRA/DÓŁ: WYBÓR  LEWO/PRAWO: ZMIANA", "UP/DOWN: SELECT  LEFT/RIGHT: CHANGE"), U_GREY);
    text_draw_small_center(b, 227, T("FIRE: ZMIANA / POWRÓT", "FIRE: CHANGE / BACK"), U_GREY);
    text_draw_small_center(b, 238, T("ESC - POWRÓT", "ESC - BACK"), U_GREY);
}

COLD BOOL ui_options_update(void)
{
    UBYTE m = input_menu();
    BOOL changed = FALSE;
    BYTE d = 0;
    while (key_get() != 0xFF) ;
    if (m & BTN_UP)   { opt_row = (UBYTE)((opt_row + OPT_ROWS - 1) % OPT_ROWS); changed = TRUE; }
    if (m & BTN_DOWN) { opt_row = (UBYTE)((opt_row + 1) % OPT_ROWS); changed = TRUE; }
    if (m & BTN_RIGHT) d = 1;
    if (m & BTN_LEFT) d = -1;
    if (m & BTN_FIRE) {
        if (opt_row == OPT_BACK) return TRUE;
        d = 1;
    }
    if (d) {
        changed = TRUE;
        if (opt_row == OPT_TRACK) g_tour.track_choice = (UBYTE)((g_tour.track_choice + 4 + d) % 4);
        else if (opt_row == OPT_DEFECTS) g_defects_enabled = (BOOL)!g_defects_enabled;
        else if (opt_row == OPT_MUSIC) g_music_enabled = (BOOL)!g_music_enabled;
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
static COLD void record_text(char *s, BOOL lap)
{
    TrackRecord *r = records_for(g_tour.track.type);
    UWORD fr = lap ? r->lap_frames : r->heat_frames;
    char *p = text_cat(s, lap ? T("REKORD OKR. ", "LAP RECORD ") : T("REKORD BIEGU ", "HEAT RECORD "));
    if (!fr) { text_cat(p, "-"); return; }
    p = fmt_time(p, fr, lap);
    p = text_cat(p, " S ");
    name_trunc(p, lap ? r->lap_name : r->heat_name, 10);
}

static COLD void wetness_text(char *s)
{
    UBYTE w = g_tour.track.wetness;
    text_cat(text_cat(s, T("TOR: ", "TRACK: ")), w > 170 ? T("MOKRY", "WET") : w > 60 ? T("WILGOTNY", "DAMP") : T("SUCHY", "DRY"));
}

/* cyfry 3x5 do naglowka tabeli (numery biegow) - bity: 5 wierszy po 3 */
static const UWORD tiny_digit[10] = {
    0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF
};


/* prostokat z dokladnoscia do piksela (blit_fill dziala tylko na pelnych slowach 16 px) */
static void rect_px(UBYTE *b, WORD x, WORD y, WORD w, WORD h, UBYTE c)
{
    WORD x1 = x + w - 1, bx0 = x >> 3, bx1 = x1 >> 3, bx, j;
    UBYTE m0 = (UBYTE)(0xFF >> (x & 7)), m1 = (UBYTE)(0xFF << (7 - (x1 & 7)));
    if (w <= 0 || h <= 0) return;
    if (bx0 == bx1) m0 &= m1;
    hw_wait_blit();
    for (j = 0; j < h; j++) {
        UBYTE *row = b + (LONG)(y + j) * PLAY_STRIDE, pl, col = c;
        for (pl = 0; pl < PLAY_DEPTH; pl++, row += ROW_BYTES, col >>= 1) {
            BOOL set = (BOOL)(col & 1);
            row[bx0] = set ? (UBYTE)(row[bx0] | m0) : (UBYTE)(row[bx0] & ~m0);
            if (bx1 > bx0) {
                for (bx = bx0 + 1; bx < bx1; bx++) row[bx] = set ? 0xFF : 0x00;
                row[bx1] = set ? (UBYTE)(row[bx1] | m1) : (UBYTE)(row[bx1] & ~m1);
            }
        }
    }
}

static COLD void tiny_number(UBYTE *b, WORD x, WORD y, UBYTE v, UBYTE c)
{
    UBYTE d[2], n = 0, i, k;
    if (v >= 10) d[n++] = (UBYTE)(v / 10);
    d[n++] = (UBYTE)(v % 10);
    x -= (WORD)(n * 4 - 1) / 2;                          /* x = srodek liczby */
    for (i = 0; i < n; i++, x += 4)
        for (k = 0; k < 15; k++)
            if (tiny_digit[d[i]] & (0x4000 >> k)) blit_plot(b, (WORD)(x + k % 3), (WORD)(y + k / 3), c);
}

/* Tabela turnieju: wiersz = zawodnik (kolejnosc wg punktow), kolumna = bieg,
 * w komorce punkty z biegu (W = wykluczenie), puste = jeszcze nie jechal. */
#define TB_X     8
#define TB_NAME  14
#define TB_GRID  98                 /* pierwsza kolumna biegow */
#define TB_CELL  12
#define TB_PTS   (TB_GRID + NUM_HEATS * TB_CELL)
#define TB_HEAD  9
#define TB_ROW   13
static COLD void draw_standings(UBYTE *b, WORD y)
{
    UBYTE order[MAX_RIDERS], i, h, next = g_tour.current_heat;
    WORD bottom = (WORD)(y + TB_HEAD + MAX_RIDERS * TB_ROW);
    char s[48], *p;

    tournament_sort(order);
    rect_px(b, TB_X, y - 2, SCREEN_W - 2 * TB_X, bottom - y + 3, U_BORDER);
    rect_px(b, TB_X + 1, y - 1, SCREEN_W - 2 * TB_X - 2, bottom - y + 1, U_PANEL);
    if (next < NUM_HEATS)                                 /* najblizszy bieg podswietlony */
        rect_px(b, (WORD)(TB_GRID + next * TB_CELL + 1), y - 1, TB_CELL - 1, bottom - y + 1, U_CURSOR);
    text_draw(b, TB_NAME, y, T("ZAWODNIK", "RIDER"), U_GREY, 0);
    text_draw(b, TB_PTS + 4, y, T("PKT", "PTS"), U_GREY, 0);
    for (h = 0; h < NUM_HEATS; h++)
        tiny_number(b, (WORD)(TB_GRID + h * TB_CELL + TB_CELL / 2), (WORD)(y + 2), (UBYTE)(h + 1),
                    h == next ? U_GOLD : U_GREY);
    /* siatka */
    for (h = 0; h <= NUM_HEATS; h++)
        rect_px(b, (WORD)(TB_GRID + h * TB_CELL), y - 1, 1, bottom - y + 1, U_BORDER);
    for (i = 0; i < MAX_RIDERS; i++)
        rect_px(b, TB_X + 1, (WORD)(y + TB_HEAD + i * TB_ROW), SCREEN_W - 2 * TB_X - 2, 1, U_BORDER);

    for (i = 0; i < MAX_RIDERS; i++) {
        Rider *r = &g_tour.riders[order[i]];
        WORD yy = (WORD)(y + TB_HEAD + i * TB_ROW + 2);
        name_trunc(s, r->name, 10);
        text_draw(b, TB_NAME, yy, s, i == 0 ? U_GOLD : U_WHITE, U_SHADOW);
        for (h = 0; h < NUM_HEATS; h++) {
            UBYTE v = r->heat_points[h];
            if (v == HP_NONE) continue;
            s[0] = v == HP_EXCLUDED ? T('W', 'X') : v == HP_DEFECT ? 'D' : (char)('0' + v); s[1] = 0;
            text_draw(b, (WORD)(TB_GRID + h * TB_CELL + 3), yy, s,
                      v >= HP_DEFECT ? U_BORDER : v == 3 ? U_GOLD : U_WHITE, 0);
        }
        p = text_uint(s, r->points, 1);
        *p = 0;
        text_draw(b, (WORD)(SCREEN_W - TB_X - 6 - text_len(s) * 8), yy, s, U_GOLD, U_SHADOW);
    }
}

static COLD void draw_table(UBYTE *b)
{
    UBYTE hn = g_tour.current_heat, g;
    char s[48], *p;

    if (hn == RUNOFF_HEAT) text_cat(s, T("BIEG DODATKOWY", "RUN-OFF"));
    else { p = text_cat(s, T("BIEG ", "HEAT ")); text_uint(p, (ULONG)(hn + 1), 1); }
    text_draw_big_center(b, 10, s, U_RAINBOW, U_SHADOW);
    text_draw_center(b, 36, hn == RUNOFF_HEAT ? T("REMIS - O MIEJSCE NA PODIUM", "TIE - RUN-OFF FOR THE PODIUM")
                          : hn >= FIXED_HEATS ? T("BIEG NOMINOWANY", "NOMINATED HEAT") : T("PROGRAM ZAWODÓW", "RACE PROGRAMME"), U_GOLD, 0);

    panel(b, 48, 52, 224, 64, U_PANEL, U_BORDER);
    for (g = 0; g < 4; g++) {
        Rider *r;
        WORD y = (WORD)(57 + g * 14);
        if (g_tour.heat_table[hn][g] == 0xFF) continue;          /* puste pole (bieg dodatkowy) */
        r = &g_tour.riders[g_tour.heat_table[hn][g]];
        helmet_box(b, 64, y, g_tour.heat_table[hn][g]);   /* kolor kasku zawodnika */
        s[0] = gate_letter[g]; s[1] = 0;
        text_draw(b, 88, y + 1, s, U_GREY, 0);
        name_trunc(s, r->name, NAME_EDIT_MAX);
        text_draw(b, 104, y + 1, s, U_WHITE, U_SHADOW);
        text_draw(b, 216, y + 1, r->control == CTRL_HUMAN ? T("GRACZ", "HUMAN") : "AI", r->control == CTRL_HUMAN ? U_GREEN : U_GREY, 0);
    }

    draw_standings(b, 130);
    wetness_text(s);
    text_draw_center(b, 198, s, g_tour.track.wetness > 170 ? U_BORDER : U_GREY, 0);
    record_text(s, FALSE);
    text_draw_center(b, 211, s, U_GOLD, 0);
    record_text(s, TRUE);
    text_draw_center(b, 222, s, U_GOLD, 0);
    if (blink) text_draw_center(b, 238, T("FIRE - DALEJ", "FIRE - CONTINUE"), U_RAINBOW, U_SHADOW);
}

/* ===================== PREZENTACJA PRZED BIEGIEM ====================== */

/* Podprowadzajace: grafika 48x96 z tools/gengirls.py (data/girls.bin, 4 postacie planarnie,
 * 6 bajtow szerokosci). Kopia z maska: przezroczyste piksele (kolor 0) zostawiaja tlo,
 * wiec dlonie nachodza na tablice. Kolory w palecie toru (180..191, 221..239).
 *
 * Postac i tablica maja kolor kasku zawodnika; kazda stoi na polu, z ktorego startuje jej
 * zawodnik, i kolysze sie: cala gora (tablica, rece, glowa, tulow, biodra) przesuwa sie razem
 * o -2..2 px, uda o polowe, lydki i stopy stoja - kazda w innej fazie; cykl 1 s, 5 wychylen jak
 * klatki animacji (sway_tab, sway_dx). Dwa razy jedna z nich posyla buziaka (usta w dziobek
 * i odlatujace serduszko). Gora postaci z tablica jest zapamietywana (pres_cache) przy rysowaniu
 * ekranu, a przy ruchu tylko kopiowana wierszami z przesunieciem - bez czyszczenia i rysowania
 * tablicy od nowa (to na A1200 gubilo klatki i kolysanie zwalnialo). */
#define PRES_Y      72                      /* gora postaci */
#define PRES_TIME   (3 * FPS)               /* prezentacja: 3 s albo FIRE */
#define SWAY_PHASES 36                      /* cykl kolysania: 0,72 s */
#define UPPER_ROWS  75                      /* wiersze postaci przerysowywane przy ruchu (do kolan) */
static UWORD present_t;                     /* klatki od wejscia na ekran prezentacji */
static UWORD present_drawn[2][4];           /* narysowane w buforze: wychylenie + mina (0xFFFF = brak) */
/* Wychylenie w pikselach (-2..2) w kolejnych klatkach cyklu: 5 faz jak klatki animacji,
 * w skrajnych dluzej. Gora przesuwa sie razem, wiec sylwetka nie "faluje" wiersz po wierszu. */
static const BYTE sway_tab[SWAY_PHASES] = {
    0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, -1, -1, -1, -1, -2, -2, -2, -2, -2, -2, -2, -1, -1, -1, -1 };
static WORD gate_x(UBYTE g) { return (WORD)(16 + g * 80); }
/* faza podprowadzajacej g: kazda w innym miejscu cyklu */
static UBYTE sway_phase(UBYTE g) { return (UBYTE)((present_t + g * 9) % SWAY_PHASES); }

/* Buziak: dwa razy w czasie prezentacji, u dwoch roznych podprowadzajacych, losowo (wlasny
 * generator - bez ruszania g_rng, zeby trailer i zrzuty mialy ten sam bieg). Usta postaci
 * (data/girls.bin): wiersz 24. */
#define KISS_LEN    40                      /* czas buziaka w klatkach */
#define MOUTH_Y     24
static const UBYTE mouth_x[4] = { 24, 22, 24, 24 };          /* lewy piksel ust kazdej postaci */
static const UWORD kiss_at[2] = { 20, 85 };
static UBYTE kiss_girl = 0xFF, kiss_used;   /* pole A..D z buziakiem; pola, ktore juz go daly */
static UWORD kiss_t0;
static ULONG ex_seed;

static UBYTE ex_rand(UBYTE n)
{
    ex_seed = ex_seed * 1103515245UL + 12345UL;
    return (UBYTE)((ex_seed >> 16) % n);
}

void ui_present_start(void)
{
    UBYTE i, g;
    present_t = 0;
    kiss_girl = 0xFF;
    kiss_used = 0;
    ex_seed = g_rng ^ 0x5EED;
    for (i = 0; i < 2; i++) for (g = 0; g < 4; g++) present_drawn[i][g] = 0xFFFF;
}

/* raz na klatke w GS_PRESENT; zwraca TRUE po czasie prezentacji */
BOOL ui_present_update(void)
{
    UBYTE i;
    if (present_t < 0xFFF0) present_t++;
    if (kiss_girl != 0xFF && present_t - kiss_t0 >= KISS_LEN) kiss_girl = 0xFF;
    for (i = 0; i < 2; i++)
        if (present_t == kiss_at[i]) {
            UBYTE hn = g_tour.current_heat, g = ex_rand(4), k;
            for (k = 0; k < 4 && (g_tour.heat_table[hn][g] == 0xFF || (kiss_used & (1 << g))); k++) g = (UBYTE)((g + 1) & 3);
            kiss_used |= (UBYTE)(1 << g);
            kiss_girl = g;
            kiss_t0 = present_t;
        }
    return (BOOL)(present_t >= PRES_TIME);
}

/* przesuniecie wiersza y postaci przy wychyleniu amp (piksele): gora do konca spodniczki (0..58)
 * o cale wychylenie, uda (59..72) o polowe, lydki i stopy stoja - granice schodkow sa stale */
static WORD sway_dx(WORD y, BYTE amp)
{
    if (y <= 58) return amp;
    if (y <= 72) return (WORD)(amp / 2);
    return 0;
}

/* wiersze y0..y1-1 postaci (x0 podzielne przez 8), kazdy przesuniety o sway_dx */
static void draw_girl_rows(UBYTE *b, WORD x0, WORD py, UBYTE color, WORD y0, WORD y1, BYTE amp)
{
    const UBYTE *src = girls_img + color * GIRL_SIZE + (LONG)y0 * PLAY_DEPTH * GIRL_BYTES;
    WORD y;
    UBYTE pl, k;
    hw_wait_blit();
    for (y = y0; y < y1; y++, src += PLAY_DEPTH * GIRL_BYTES) {
        WORD dx = sway_dx(y, amp), xs = (WORD)(x0 + dx), col0 = (WORD)(xs >> 3);
        UBYTE sh = (UBYTE)(xs & 7);
        for (k = 0; k <= GIRL_BYTES; k++) {              /* przesuniecie bitowe: o 1 bajt szerzej */
            UBYTE mask = 0, data[PLAY_DEPTH];
            UBYTE *d;
            if (col0 + k < 0 || col0 + k >= ROW_BYTES) continue;
            for (pl = 0; pl < PLAY_DEPTH; pl++) {
                const UBYTE *r = src + pl * GIRL_BYTES;
                UBYTE hi = k < GIRL_BYTES ? r[k] : 0, lo = k ? r[k - 1] : 0;
                data[pl] = (UBYTE)(sh ? (hi >> sh) | (lo << (8 - sh)) : hi);
                mask |= data[pl];
            }
            if (!mask) continue;
            d = b + (LONG)(py + y) * PLAY_STRIDE + col0 + k;
            for (pl = 0; pl < PLAY_DEPTH; pl++, d += ROW_BYTES)
                *d = (UBYTE)((*d & ~mask) | data[pl]);
        }
    }
}

/* Sponsorzy z banerow na murawie (tools/genart.py: 3DPJOY i DEV84) w malej wersji - same napisy
 * literami 3x5 jak na banerze 3DPJOY - na tabliczce w srodku tablicy: pola A i C - 3DPJOY
 * (czarny na bialym, czerwona kropka przycisku), B i D - DEV84 (cyjan 121 z cieniem 122 na czarnym).
 * Kolory tylko z czesci palety wspolnej z plansza tytulowa. */
static const char *const logo_rows[2][5] = {
    { "###.##..###...#.###.#.#", "..#.#.#.#.#...#.#.#.#.#", ".##.#.#.###...#.#.#..#.", "..#.#.#.#...#.#.#.#..#.", "###.##..#....#..###..#." },
    { "##..###.#.#.###.#.#", "#.#.#...#.#.#.#.#.#", "#.#.##..#.#.###.###", "#.#.#...#.#.#.#...#", "##..###..#..###...#" } };

static void draw_logo(UBYTE *b, WORD cx, WORD cy, UBYTE which)
{
    const char *const *rows = logo_rows[which];
    WORD w = 0, x0, y0, r, c, pass;
    while (rows[0][w]) w++;
    x0 = (WORD)(cx - w / 2); y0 = (WORD)(cy - 2);
    rect_px(b, x0 - 3, y0 - 3, w + 6, 11, which ? U_GREY : U_SHADOW);      /* ramka tabliczki */
    rect_px(b, x0 - 2, y0 - 2, w + 4, 9, which ? U_SHADOW : U_WHITE);
    if (!which) rect_px(b, x0 + w - 1, y0 - 2, 2, 1, U_RED);              /* przycisk joysticka */
    for (pass = which ? 0 : 1; pass < 2; pass++)                           /* DEV84: najpierw cien */
        for (r = 0; r < 5; r++)
            for (c = 0; c < w; ) {
                WORD n = 1;
                if (rows[r][c] != '#') { c++; continue; }
                while (rows[r][c + n] == '#') n++;                          /* odcinek */
                rect_px(b, x0 + c + (pass ? 0 : 1), y0 + r + (pass ? 0 : 1), n, 1,
                        (UBYTE)(which ? (pass ? 121 : 122) : U_SHADOW));
                c = (WORD)(c + n);
            }
}

/* Tablica nad glowa (52x26, trzymana w dloniach), rysowana wierszami (rect_px): cien, czarny
 * kontur z zaokraglonymi rogami, pole w kolorze kasku z gradientem (polysk u gory, ciemniejszy dol,
 * przejscia ditheringiem w szachownice), wypukla krawedz (jasna z lewej, ciemna z prawej), odblask
 * i nity po bokach. Pole startowe jest podpisane pod podprowadzajaca. */
#define U_HELMET_LT 113                     /* + kask: jasny odcien (track.c, tools/genart.py) */
#define U_HELMET_DK 117                     /* + kask: ciemny odcien */
#define BOARD_W     52
#define BOARD_H     26
static const UBYTE board_inset[3] = { 3, 1, 1 };                     /* zaokraglenie rogow */

static WORD board_row_inset(WORD r)
{
    if (r < 3) return board_inset[r];
    if (r >= BOARD_H - 3) return board_inset[BOARD_H - 1 - r];
    return 0;
}

static void draw_board(UBYTE *b, WORD x0, WORD y0, UBYTE color, UBYTE sponsor)
{
    WORD bx = (WORD)(x0 - 2), by = (WORD)(y0 + 2 - BOARD_H), cy = (WORD)(by + BOARD_H / 2);
    UBYTE base = (UBYTE)(U_HELMET + color), lt = (UBYTE)(U_HELMET_LT + color), dk = (UBYTE)(U_HELMET_DK + color);
    WORD r, i;

    for (r = 0; r < BOARD_H; r++) {                                  /* cien i kontur */
        i = board_row_inset(r);
        rect_px(b, bx + i + 2, by + r + 2, BOARD_W - 2 * i, 1, U_SHADOW);
        rect_px(b, bx + i, by + r, BOARD_W - 2 * i, 1, U_SHADOW);
    }
    for (r = 1; r < BOARD_H - 1; r++) {                              /* pole: gradient + krawedz */
        WORD a = board_row_inset(r - 1), c = board_row_inset(r + 1), w;
        UBYTE col, dith = 0;
        i = board_row_inset(r);
        if (a > i) i = a;
        if (c > i) i = c;
        i++;
        w = (WORD)(BOARD_W - 2 * i);
        if (r <= 2) col = lt;
        else if (r <= 5) { col = base; dith = lt; }                 /* przejscia w szachownice */
        else if (r < BOARD_H - 8) col = base;
        else if (r < BOARD_H - 5) { col = base; dith = dk; }
        else col = dk;
        rect_px(b, bx + i, by + r, w, 1, col);
        if (dith) {
            WORD x;
            for (x = (WORD)(bx + i + ((r + i) & 1)); x < bx + i + w; x += 2) rect_px(b, x, by + r, 1, 1, dith);
        }
        if (r > 1 && r < BOARD_H - 2) {
            rect_px(b, bx + i, by + r, 1, 1, lt);                    /* jasna lewa krawedz */
            rect_px(b, bx + i + w - 1, by + r, 1, 1, dk);            /* ciemna prawa */
        }
    }
    rect_px(b, bx + 6, by + 2, 14, 1, U_WHITE);                      /* odblask */
    rect_px(b, bx + 4, by + 3, 3, 1, U_WHITE);
    for (i = 0; i < 2; i++) {                                         /* nity */
        WORD nx = (WORD)(i ? bx + BOARD_W - 5 : bx + 3);
        rect_px(b, nx, cy - 1, 2, 2, U_SILVER);
        rect_px(b, nx + 1, cy, 1, 1, U_SHADOW);
    }
    draw_logo(b, (WORD)(bx + BOARD_W / 2), cy, sponsor);
}

/* serduszko 5x4 (buziak): wiersze bitowo, bit 4 = lewy piksel */
static const UBYTE heart_rows[4] = { 0x0A, 0x1F, 0x0E, 0x04 };

/* buziak w klatce t: usta w dziobek, po chwili serduszko odlatuje w gore; x0 z wychyleniem gory */
static void draw_kiss(UBYTE *b, WORD x0, UBYTE girl, UWORD t)
{
    WORD mx = (WORD)(x0 + mouth_x[girl]), my = (WORD)(PRES_Y + MOUTH_Y);
    rect_px(b, mx, my, 2, 1, U_RED);
    if (t >= 6) {
        WORD s = (WORD)(t - 6), hx = (WORD)(mx + 3 + s / 3), hy = (WORD)(my - 4 - s / 2), r, c;
        for (r = 0; r < 4; r++)
            for (c = 0; c < 5; c++)
                if (heart_rows[r] & (0x10 >> c)) rect_px(b, hx + c, hy + r, 1, 1, r == 1 && c == 1 ? U_WHITE : U_RED);
    }
}

/* stan gory postaci na polu g: wychylenie (0..4) i klatka buziaka - zmiana = przerysowanie */
static UWORD girl_key(UBYTE g, UWORD *t)
{
    UWORD k = (UWORD)(sway_tab[sway_phase(g)] + 2);
    *t = 0xFFFF;
    if (kiss_girl == g) {
        *t = (UWORD)(present_t - kiss_t0);
        k |= (UWORD)((*t < 6 ? 1 : *t - 4) << 3);     /* serduszko leci co klatke */
    }
    return k;
}

/* Pamiec gory postaci: wiersze od gory tablicy (PRES_Y - CACHE_TOP) do kolan, 8 bajtow (64 px od
 * x0 - 8) na plan, narysowane bez wychylenia. Tlo pod postaciami to kolor 0 (Copper), wiec wiersz
 * mozna zapisac w calosci, bez maski i bez czyszczenia. */
#define CACHE_TOP   (BOARD_H - 2)
#define CACHE_ROWS  (CACHE_TOP + UPPER_ROWS)
static UBYTE pres_cache[4][CACHE_ROWS][PLAY_DEPTH][8];

/* gora postaci (do kolan) z tablica - wolno, rect_px i kopia z maska (rysowanie ekranu) */
static void draw_girl_upper(UBYTE *b, UBYTE g, UBYTE color, BYTE amp)
{
    WORD x0 = gate_x(g);
    rect_px(b, x0 - 8, PRES_Y - CACHE_TOP, 64, CACHE_ROWS, 0);
    draw_board(b, (WORD)(x0 + amp), PRES_Y, color, (UBYTE)(g & 1));
    if (girls_img) draw_girl_rows(b, x0, PRES_Y, color, 0, UPPER_ROWS, amp);
}

static void cache_girl_upper(const UBYTE *b, UBYTE g)
{
    WORD r;
    UBYTE pl, k;
    const UBYTE *s = b + (LONG)(PRES_Y - CACHE_TOP) * PLAY_STRIDE + ((gate_x(g) - 8) >> 3);
    hw_wait_blit();
    for (r = 0; r < CACHE_ROWS; r++, s += PLAY_STRIDE)
        for (pl = 0; pl < PLAY_DEPTH; pl++)
            for (k = 0; k < 8; k++) pres_cache[g][r][pl][k] = s[pl * ROW_BYTES + k];
}

/* gora postaci z pamieci: kazdy wiersz przesuniety o wychylenie (tablica i rece jak glowa) */
static void fast_girl_upper(UBYTE *b, UBYTE g, BYTE amp)
{
    WORD r;
    UBYTE pl, k;
    UBYTE *d = b + (LONG)(PRES_Y - CACHE_TOP) * PLAY_STRIDE + ((gate_x(g) - 8) >> 3);
    hw_wait_blit();
    for (r = 0; r < CACHE_ROWS; r++, d += PLAY_STRIDE) {
        WORD dx = r < CACHE_TOP ? amp : sway_dx((WORD)(r - CACHE_TOP), amp);
        UBYTE n = (UBYTE)(dx < 0 ? -dx : dx);
        for (pl = 0; pl < PLAY_DEPTH; pl++) {
            const UBYTE *s = pres_cache[g][r][pl];
            UBYTE *o = d + pl * ROW_BYTES;
            if (!n) for (k = 0; k < 8; k++) o[k] = s[k];
            else if (dx > 0) for (k = 0; k < 8; k++) o[k] = (UBYTE)((s[k] >> n) | (k ? s[k - 1] << (8 - n) : 0));
            else for (k = 0; k < 8; k++) o[k] = (UBYTE)((s[k] << n) | (k < 7 ? s[k + 1] >> (8 - n) : 0));
        }
    }
}

/* kolysanie i buziak: gora postaci przerysowana przy zmianie (w kazdym buforze osobno) */
static void present_girls(UBYTE *b, UBYTE bi)
{
    UBYTE hn = g_tour.current_heat, g;
    UWORD t, k;
    for (g = 0; g < 4; g++) {
        UBYTE r = g_tour.heat_table[hn][g];
        BYTE amp = sway_tab[sway_phase(g)];
        if (r == 0xFF) continue;
        k = girl_key(g, &t);
        if (present_drawn[bi][g] == k) continue;
        fast_girl_upper(b, g, amp);
        if (t != 0xFFFF) draw_kiss(b, (WORD)(gate_x(g) + amp), r, t);
        present_drawn[bi][g] = k;
    }
}

static COLD void draw_present(UBYTE *b, UBYTE bi)
{
    UBYTE hn = g_tour.current_heat, g;
    char s[48], *p;
    if (hn == RUNOFF_HEAT) text_cat(s, T("BIEG DODATKOWY", "RUN-OFF"));
    else { p = text_cat(s, T("BIEG ", "HEAT ")); text_uint(p, (ULONG)(hn + 1), 1); }
    text_draw_big_center(b, 8, s, U_RAINBOW, U_SHADOW);
    for (g = 0; g < 4; g++) {
        WORD cx = (WORD)(gate_x(g) + GIRL_W / 2);
        UBYTE r = g_tour.heat_table[hn][g];
        if (r == 0xFF) continue;                         /* puste pole (bieg dodatkowy) */
        draw_girl_upper(b, g, r, 0);                    /* bez wychylenia - do pamieci */
        cache_girl_upper(b, g);
        present_drawn[bi][g] = 2;                        /* wychylenie 0, bez buziaka */
        if (girls_img) draw_girl_rows(b, gate_x(g), PRES_Y, r, UPPER_ROWS, GIRL_H, 0);   /* lydki i stopy */
        name_trunc(s, g_tour.riders[r].name, 9);
        text_draw(b, (WORD)(cx - text_len(s) * 4), PRES_Y + GIRL_H + 8, s, U_WHITE, U_SHADOW);
        s[0] = gate_letter[g]; s[1] = 0;
        text_draw(b, cx - 4, PRES_Y + GIRL_H + 22, s, U_GREY, 0);
    }
    text_draw_center(b, 238, T("FIRE - POD TAŚMĘ!", "FIRE - TO THE TAPE!"), U_RAINBOW, U_SHADOW);   /* bez mrugania */
}

BOOL ui_confirm(void)
{
    /* FIRE (joystick albo Return z ustawienia STRZALKI) i Return z kolejki klawiszy - oba naraz,
     * inaczej jeden Enter potrafil zatwierdzic dwa ekrany pod rzad */
    BOOL fire = (BOOL)((input_menu() & BTN_FIRE) != 0);
    BOOL ret = (BOOL)(key_get() == KEY_RETURN);
    return (BOOL)(fire || ret);
}

/* ======================== WYBOR POL ================================ */

static COLD void draw_pick(UBYTE *b)
{
    UBYTE hn = g_tour.current_heat, g, i;
    UBYTE *row = g_tour.heat_table[hn];
    char s[48], *p;

    p = text_cat(s, T("BIEG ", "HEAT "));
    text_uint(p, (ULONG)(hn + 1), 1);
    text_draw_big_center(b, 10, s, U_RAINBOW, U_SHADOW);
    text_draw_center(b, 36, T("WYBÓR PÓL STARTOWYCH", "GATE SELECTION"), U_GOLD, 0);

    if (g_tour.pick_index < MAX_RIDERS) {
        Rider *r = &g_tour.riders[g_tour.pick_order[g_tour.pick_index]];
        p = text_cat(s, T("WYBIERA: ", "PICKING: "));
        p = name_trunc(p, r->name, NAME_EDIT_MAX);
        p = text_cat(p, " (");
        p = text_uint(p, r->points, 1);
        text_cat(p, r->control == CTRL_AI ? T(" PKT, AI)", " PTS, AI)") : T(" PKT)", " PTS)"));
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
        else text_cat(s, T("WOLNE", "FREE"));
        text_draw(b, (WORD)(x + ((64 - text_len(s) * 8) / 2 & ~7)), 150, s,
                  row[g] != 0xFF ? U_GOLD : U_GREY, U_SHADOW);
    }

    text_draw(b, 16, 190, T("KOLEJNOŚĆ WYBORU:", "PICK ORDER:"), U_GREY, 0);
    for (i = 0; i < MAX_RIDERS; i++) {
        p = text_uint(s, (ULONG)(i + 1), 1);
        p = text_cat(p, ".");
        name_trunc(p, g_tour.riders[g_tour.pick_order[i]].name, 7);
        text_draw((UBYTE *)b, (WORD)(i * 80), 206, s, i == g_tour.pick_index ? U_RAINBOW : U_WHITE, 0);
    }
    text_draw_small_center(b, 236, T("LEWO/PRAWO: POLE   FIRE: WYBIERZ", "LEFT/RIGHT: GATE   FIRE: PICK"), U_GREY);
}

COLD void ui_pick_update(void)
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

static COLD void draw_podium(UBYTE *b)
{
    static const WORD bx[3] = { 128, 48, 208 }, top[3] = { 86, 110, 128 };
    static const UBYTE col[3] = { U_GOLD, U_SILVER, U_BRONZE };
    UBYTE order[MAX_RIDERS], i;
    char s[48], *p;

    tournament_sort(order);
    text_draw_big_center(b, 10, T("KONIEC TURNIEJU", "TOURNAMENT OVER"), U_RAINBOW, U_SHADOW);

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
        p = text_cat(p, T(" PKT  ", " PTS  "));
        p = text_uint(p, r->wins, 1);
        text_cat(p, T(" ZW.", " WINS"));
        text_draw(b, 40, y, s, i == 0 ? U_GOLD : U_WHITE, U_SHADOW);
    }
    if (blink) text_draw_center(b, 238, T("FIRE - NOWY TURNIEJ", "FIRE - NEW TOURNAMENT"), U_RAINBOW, U_SHADOW);
}

/* ========================== NAKLADKI ============================== */

/* ludzie, ktorzy pod tasma nie trzymaja kierunku (bit = pole) - bieg na nich czeka */
static UBYTE not_holding_mask(void)
{
    const Heat *h = &g_tour.heat;
    UBYTE g, m = 0;
    if (g_tour.state != GS_HEAT || h->phase != HEAT_READY || h->phase_timer < 2 * FPS) return 0;
    for (g = 0; g < MAX_RIDERS; g++) {
        const Bike *b = &g_tour.bikes[g];
        if (b->empty || b->state == BIKE_EXCLUDED) continue;
        if (g_tour.riders[b->rider].control == CTRL_HUMAN && !(b->input & BTN_DIRS)) m |= (UBYTE)(1 << g);
    }
    return m;
}

static Overlay current_overlay(void)
{
    if (g_paused) return OV_PAUSE;
    if (not_holding_mask()) return OV_HOLD;
    if (g_tour.state == GS_HEAT && g_tour.heat.phase == HEAT_FALSE_START) return OV_FALSE_START;
    if (g_tour.state == GS_HEAT_RESULT) return OV_RESULT;
    if (g_tour.state == GS_TRACK_GRADING) return OV_GRADING;
    return OV_NONE;
}

/* bieg czeka: ktos z ludzi nie trzyma kierunku pod tasma */
static COLD void draw_hold(UBYTE *b)
{
    UBYTE m = not_holding_mask(), g;
    WORD y = PANEL_Y + 34;
    char s[48];
    panel(b, PANEL_X, PANEL_Y + 10, PANEL_W, 76, U_PANEL, U_GOLD);
    text_draw_center(b, PANEL_Y + 18, T("CZEKAMY NA START", "WAITING FOR START"), U_GOLD, U_SHADOW);
    for (g = 0; g < MAX_RIDERS && y < PANEL_Y + 60; g++)
        if (m & (1 << g)) {
            name_trunc(s, g_tour.riders[g_tour.bikes[g].rider].name, NAME_EDIT_MAX);
            text_draw_center(b, y, s, U_WHITE, U_SHADOW);
            y += 12;
        }
    text_draw_center(b, PANEL_Y + 66, T("TRZYMAJ LEWO LUB PRAWO", "HOLD LEFT OR RIGHT"), U_WHITE, U_SHADOW);
}

/* pauza: panel z pytaniem (na torze w prostokacie nakladek, w menu na srodku ekranu) */
static COLD void draw_pause(UBYTE *b, WORD y)
{
    panel(b, PANEL_X, y, PANEL_W, 92, U_PANEL, U_GOLD);
    text_draw_big_center(b, y + 8, T("PAUZA", "PAUSE"), U_GOLD, U_SHADOW);
    text_draw_center(b, y + 38, g_pause_quit ? T("ZAKOŃCZYĆ GRĘ?", "QUIT THE GAME?") : T("WYJŚĆ DO MENU GŁÓWNEGO?", "BACK TO THE MAIN MENU?"), U_WHITE, U_SHADOW);
    text_draw_center(b, y + 56, T("ESC - TAK", "ESC - YES"), U_GOLD, U_SHADOW);
    text_draw_center(b, y + 72, T("FIRE - GRAJ DALEJ", "FIRE - KEEP PLAYING"), U_WHITE, U_SHADOW);
}

static COLD void draw_false_start(UBYTE *b)
{
    Heat *h = &g_tour.heat;
    Bike *bk = &g_tour.bikes[h->false_starter];
    Rider *r = &g_tour.riders[bk->rider];
    char s[48], *p;

    panel(b, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, U_PANEL, U_RED);
    text_draw_big_center(b, PANEL_Y + 12, T("TAŚMA!", "TAPE!"), U_RED, U_SHADOW);
    helmet_box(b, 80, PANEL_Y + 48, bk->rider);
    name_trunc(s, r->name, NAME_EDIT_MAX);
    text_draw(b, 104, PANEL_Y + 49, s, U_WHITE, U_SHADOW);
    text_draw_center(b, PANEL_Y + 68, bk->state == BIKE_EXCLUDED ? T("WYKLUCZENIE!", "EXCLUDED!") : T("OSTRZEŻENIE", "WARNING"),
                     bk->state == BIKE_EXCLUDED ? U_RED : U_GOLD, U_SHADOW);
    p = text_cat(s, T("POWTÓRKA STARTU (", "RESTART ("));
    p = text_uint(p, h->restarts, 1);
    text_cat(p, ")");
    text_draw_center(b, PANEL_Y + 90, s, U_GREY, 0);
}

static COLD void draw_result(UBYTE *b)
{
    static const char *pts_txt[4] = { T("3 PKT", "3 PTS"), T("2 PKT", "2 PTS"), T("1 PKT", "1 PTS"), T("0 PKT", "0 PTS") };
    Heat *h = &g_tour.heat;
    UBYTE line, i, g;
    char s[48], *p;

    panel(b, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, U_PANEL, U_BORDER);
    if (g_tour.current_heat == RUNOFF_HEAT) text_cat(s, T("BIEG DODATKOWY", "RUN-OFF"));
    else { p = text_cat(s, T("WYNIK BIEGU ", "RESULT - HEAT ")); text_uint(p, h->number, 1); }
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
            helmet_box(b, 80, y, bk->rider);
            name_trunc(s, g_tour.riders[bk->rider].name, 10);
            text_draw(b, 104, y + 1, s, U_WHITE, U_SHADOW);
            if (g_tour.current_heat != RUNOFF_HEAT || !fin)     /* bieg dodatkowy: bez punktow */
                text_draw(b, 200, y + 1, fin ? pts_txt[line] : bk->state == BIKE_EXCLUDED ? T("W", "EXC") :
                                         bk->state == BIKE_DEFECT ? T("D", "DEF") : T("NU", "DNF"),
                          line == 0 && fin ? U_GOLD : U_WHITE, U_SHADOW);
        }
    }
    if (h->finished_count) {
        UWORD t = g_tour.bikes[0].finish_frame;
        for (g = 0; g < 4; g++)
            if (g_tour.bikes[g].rider == h->finish_order[0]) t = g_tour.bikes[g].finish_frame;
        p = text_cat(s, T("CZAS: ", "TIME: "));
        p = fmt_time(p, t, FALSE);
        text_cat(p, " S");
        text_draw_center(b, PANEL_Y + 84, s, g_tour.new_records & REC_HEAT ? U_GOLD : U_GREY, 0);
    }
    /* na stale, bez migania (plansza i tak sama przechodzi dalej po kilku sekundach) */
    if (g_tour.new_records)
        text_draw_center(b, PANEL_Y + 98, g_tour.new_records & REC_HEAT ? T("NOWY REKORD TORU!", "NEW TRACK RECORD!") : T("REKORD OKRĄŻENIA!", "LAP RECORD!"),
                         U_GOLD, U_SHADOW);
    else text_draw_center(b, PANEL_Y + 98, T("FIRE - DALEJ", "FIRE - CONTINUE"), U_GREY, 0);
}

extern UWORD g_grading_progress;   /* 0..255, z main.c */

/* maly panel na murawie (w obrebie PANEL_*), zeby nie zaslaniac traktorow na torze */
#define GRADE_X 96
#define GRADE_Y 66
#define GRADE_W 128
#define GRADE_H 62

static COLD void draw_grading(UBYTE *b)
{
    WORD w = (WORD)((96 * g_grading_progress / 255) & ~15);
    panel(b, GRADE_X, GRADE_Y, GRADE_W, GRADE_H, U_PANEL, U_BORDER);
    text_draw_center(b, GRADE_Y + 6, T("PRZERWA NA TOR", "TRACK BREAK"), U_GOLD, U_SHADOW);
    text_draw_center(b, GRADE_Y + 20, T("POLEWANIE TORU", "WATERING TRACK"), U_WHITE, U_SHADOW);
    blit_fill(b, 112, GRADE_Y + 34, 96, 10, U_BORDER);
    blit_fill(b, 112, GRADE_Y + 36, 96, 6, U_PANEL);
    if (w) blit_fill(b, 112, GRADE_Y + 36, w, 6, U_GREEN);
    text_draw_center(b, GRADE_Y + 48, T("UWAGA: ŚLISKO!", "SLICK SURFACE!"), U_GREY, 0);
}

/* Czy prostokat (x, y, w, h) zachodzi na panel nakladki narysowany w buforze "back" (pauza,
 * czekamy na start, tasma, wynik, przerwa na tor). Drobin szprycy, ktore by na niego zachodzily,
 * roost.c nie rysuje (zmazanie kopia tla wycinaloby dziury w panelu), a motocykle (duszki
 * sprzetowe sa zawsze nad obrazem) chowa main.c. Prostokaty jak w draw_hold, draw_pause itd. */
BOOL ui_panel_covers(WORD x, WORD y, WORD w, WORD h)
{
    WORD px = PANEL_X, py = PANEL_Y, pw = PANEL_W, ph = PANEL_H;
    switch (panel_ov[g_video.back]) {
    case OV_NONE:     return FALSE;
    case OV_HOLD:     py = PANEL_Y + 10; ph = 76; break;
    case OV_PAUSE:    py = PANEL_Y + 10; ph = 92; break;
    case OV_GRADING:  px = GRADE_X; py = GRADE_Y; pw = GRADE_W; ph = GRADE_H; break;
    default:          break;                               /* tasma, wynik: caly panel */
    }
    return (BOOL)(x < px + pw && x + w > px && y < py + ph && y + h > py);
}

/* ========================== RENDER ================================ */

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
        /* tlo pod tasma (x-3..x+4) wraca bajtami: 1-2 bajty na plan zamiast 8 pikseli x 8 planow */
        WORD bx, bx0 = (WORD)((x - 3) >> 3), bx1 = (WORD)((x + 4) >> 3);
        LONG o = (LONG)y * PLAY_STRIDE;
        UBYTE pl;
        for (pl = 0; pl < PLAY_DEPTH; pl++, o += ROW_BYTES)
            for (bx = bx0; bx <= bx1; bx++) buf[o + bx] = g_video.background[o + bx];
        switch (style) {
        case 0:                                         /* napieta: 3 px bieli + cien */
            blit_plot(buf, x - 1, y, 1); blit_plot(buf, x, y, 1); blit_plot(buf, x + 1, y, 1);
            blit_plot(buf, x + 2, y, U_SHADOW);
            break;
        case 1: case 2: {                               /* zrywa sie: rozmyta, drgajaca */
            WORD j = (WORD)(((y >> 1) + style) & 1 ? 1 : -1);
            blit_plot(buf, x - 1 + j, y, 1); blit_plot(buf, x + j, y, 1); blit_plot(buf, x + 1 + j, y, 1);
            blit_plot(buf, x + 2 + j, y, U_SHADOW);
            break;
        }
        case 3: case 4:                                 /* gruba przerywana, odlatuje */
            if (((y + style) & 3) < 2) { blit_plot(buf, x - 2, y, 1); blit_plot(buf, x - 1, y, 1); blit_plot(buf, x, y, 1); }
            break;
        case 5: case 6:                                 /* strzepy */
            if (((y + style) & 7) < 2) { blit_plot(buf, x - 3, y, 1); blit_plot(buf, x - 2, y, 1); }
            break;
        case 7:                                         /* ostatnie punkty */
            if ((y & 7) == 0) blit_plot(buf, x - 3, y, 1);
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
#define SW_NONE 0xFF
static UBYTE startwin_drawn[2] = { SW_NONE, SW_NONE };

/* stan okienka: SW_NONE albo (klatka odlotu << 2) | lampka (0 zgaszona, 1 czerwona, 2 zielona); 3 = awaria */
#define SW_FLY   10                /* klatki odlotu tasmy w okienku (0.2 s) - okienko znika po nich */
#define SW_LAMP_X (SW_X + SW_W / 2 - 4)   /* lampka na srodku poprzeczki */
#define SW_RISE  40                /* o ile tasma idzie w gore (az za gorna krawedz okienka) */
#define SW_BOW   2                 /* delikatne ugiecie srodka w polowie odlotu (px) */
#define SW_TAPE_REST (SW_Y + 38)   /* tasma w spoczynku */
#define SW_TAPE_H 7                /* dwie wstegi po 2 px + 3 px przerwy z tasiemkami */
#define SW_TAPE_X0 (SW_X + 11)     /* miedzy slupkami maszyny */
#define SW_TAPE_X1 (SW_X + SW_W - 11)

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
    case HEAT_TAPE_UP:
    case HEAT_RACING:    /* odlot tasmy trwa jeszcze chwile po ruszeniu, potem okienko znika */
        return h->phase_timer < SW_FLY ? (UBYTE)((h->phase_timer << 2) | 2) : SW_NONE;
    default:             return SW_NONE;
    }
}

/* kolor statycznego tla okienka w punkcie (x, y): trybuna, kibice, poprzeczka, lampka, nawierzchnia */
static UBYTE startwin_px(WORD x, WORD y, UBYTE lamp)
{
    static const UBYTE lamp_col[3] = { U_PANEL, U_RED, U_GREEN };
    if (y == SW_Y + 6 || y == SW_Y + 7) return U_GREY;                       /* poprzeczka */
    if (y >= SW_Y + 8 && y < SW_Y + 13 && x >= SW_LAMP_X && x < SW_LAMP_X + 8) return lamp_col[lamp];
    if (y < SW_Y + 24) {                                                     /* trybuna z kibicami */
        if (y >= SW_Y + 4 && (y - (SW_Y + 4)) % 3 == 0 && ((x - (SW_X + 3)) & 1) == 0)
            return (UBYTE)(140 + ((x * 7 + y * 13) % 32));
        return 129;
    }
    if (((x * 37 + y * 91) & 7) == 0) return (UBYTE)(19 + ((x + y) & 3));   /* faktura nawierzchni */
    return 24;
}

static void draw_lamp(UBYTE *b, UBYTE lamp);

static COLD void draw_startwin_static(UBYTE *b, UBYTE lamp)
{
    WORD x, y;
    panel(b, SW_X, SW_Y, SW_W, SW_H, U_PANEL, U_BORDER);
    if (lamp == 3) {                                         /* awaria maszyny startowej */
        text_draw_center(b, SW_Y + 10, T("AWARIA", "GATE"), U_RED, U_SHADOW);
        text_draw_center(b, SW_Y + 24, T("MASZYNY", "FAILURE"), U_RED, U_SHADOW);
        text_draw_center(b, SW_Y + 44, T("START Z FLAGI", "FLAG START"), U_WHITE, U_SHADOW);
        return;
    }
    blit_fill(b, SW_X, SW_Y + 2, SW_W, 22, 129);             /* trybuna */
    blit_fill(b, SW_X, SW_Y + 24, SW_W, SW_H - 26, 24);      /* nawierzchnia */
    hw_wait_blit();
    for (y = SW_Y + 4; y < SW_Y + 24; y += 3)                /* kibice */
        for (x = SW_X + 3; x < SW_X + SW_W - 3; x += 2)
            blit_plot(b, x, y, startwin_px(x, y, lamp));
    for (y = SW_Y + 24; y < SW_Y + SW_H - 2; y++)            /* faktura nawierzchni */
        for (x = SW_X + 2; x < SW_X + SW_W - 2; x++)
            if (((x * 37 + y * 91) & 7) == 0) blit_plot(b, x, y, startwin_px(x, y, lamp));
    for (y = SW_Y + 6; y < SW_Y + SW_H - 4; y++)             /* slupki maszyny */
        for (x = 0; x < 3; x++) {
            UBYTE c = ((y >> 2) & 1) ? U_RED : U_WHITE;
            blit_plot(b, SW_X + 8 + x, y, c);
            blit_plot(b, SW_X + SW_W - 11 + x, y, c);
        }
    for (x = SW_X + 8; x < SW_X + SW_W - 8; x++) {           /* poprzeczka */
        blit_plot(b, x, SW_Y + 6, U_GREY);
        blit_plot(b, x, SW_Y + 7, U_GREY);
    }
    draw_lamp(b, lamp);
}

static void draw_lamp(UBYTE *b, UBYTE lamp)
{
    WORD x, y;
    for (y = SW_Y + 8; y < SW_Y + 13; y++)
        for (x = SW_LAMP_X; x < SW_LAMP_X + 8; x++) blit_plot(b, x, y, startwin_px(x, y, lamp));
}

/* wiersze y0..y1 obszaru okienka z zapamietanego tla do bufora - procesorem (bez Blittera) */
static void startwin_from_cache(UBYTE *b, WORD y0, WORD y1)
{
    WORD y;
    UBYTE pl, k;
    hw_wait_blit();       /* np. kopia calego tla toru do bufora - inaczej Blitter nadpisalby okienko */
    for (y = y0; y <= y1; y++) {
        LONG o = (LONG)y * PLAY_STRIDE + SW_X / 8;
        const UBYTE *src = sw_cache + (LONG)(y - SW_Y) * PLAY_STRIDE + SW_X / 8;
        for (pl = 0; pl < PLAY_DEPTH; pl++, o += ROW_BYTES, src += ROW_BYTES)
            for (k = 0; k < SW_W / 8; k += 2)
                *(UWORD *)(b + o + k) = *(const UWORD *)(src + k);
    }
}

static void startwin_to_cache(const UBYTE *b)
{
    ULONG n;
    const ULONG *src = (const ULONG *)(b + (LONG)SW_Y * PLAY_STRIDE);
    ULONG *d = (ULONG *)sw_cache;
    hw_wait_blit();
    for (n = 0; n < (ULONG)SW_H * PLAY_STRIDE / 4; n++) *d++ = *src++;
}

static WORD tape_rows[2][2];        /* wiersze tasmy narysowanej w buforze (do zmazania) */

/* Tasma (dwie wstegi z tasiemkami) w klatce odlotu t (0 = spoczynek).
 * Odlot "ease-out": od razu wyskakuje w gore i wyhamowuje za gorna krawedzia okienka,
 * srodek zostaje troche w tyle (luk w dol najwiekszy w polowie odlotu). */
static void draw_tape_ribbon(UBYTE *b, UBYTE bi, WORD t)
{
    /* Najpierw maski wierszy (biale wstegi i srebrne tasiemki) w pamieci, potem zapis do ekranu
     * bajtami: dawniej kazdy piksel tasmy osobno w 8 planach - w klatkach odlotu, czyli dokladnie
     * w chwili startu, to gubilo klatki. */
    static UBYTE mw[SW_H][SW_W / 8], ms[SW_H][SW_W / 8];      /* wiersze okienka, bajty od SW_X */
    WORD x, r, half = (SW_TAPE_X1 - SW_TAPE_X0) / 2, mid = SW_TAPE_X0 + half, lo = 32767, hi = -1;
    WORD q = (WORD)(SW_FLY - t);
    /* rownomiernie z lekkim hamowaniem (predkosc 1.5 -> 0.5): widac caly lot, za krawedzia pod koniec */
    LONG rise = (LONG)SW_RISE * t * (3 * SW_FLY - t) / (2 * SW_FLY * SW_FLY);
    LONG bow_max = (LONG)4 * SW_BOW * t * q / (SW_FLY * SW_FLY);
    const WORD top = SW_Y + 3, bottom = SW_Y + SW_H - 6;       /* obszar, w ktorym widac tasme */
    for (x = SW_TAPE_X0; x < SW_TAPE_X1; x++) {
        WORD d = (WORD)(x - mid);
        WORD yy = (WORD)(SW_TAPE_REST - rise + bow_max * (half * half - d * d) / (half * half));
        WORD bx = (WORD)((x - SW_X) >> 3);
        UBYTE bit = (UBYTE)(0x80 >> ((x - SW_X) & 7));
        BOOL tie = (BOOL)(((x - SW_TAPE_X0) & 7) == 3);
        for (r = 0; r < SW_TAPE_H; r++) {
            WORD py = (WORD)(yy + r), row;
            /* dwie biale wstegi (wiersze 0-1 i 5-6), miedzy nimi co 8 px cienka tasiemka */
            BOOL white = (BOOL)(r < 2 || r >= SW_TAPE_H - 2);
            if (py < top || py >= bottom || (!white && !tie)) continue;
            row = (WORD)(py - SW_Y);
            if (py < lo) {                                 /* nowy wiersz: maski od zera */
                WORD k, z;
                for (z = py; z < (lo == 32767 ? py + 1 : lo); z++)
                    for (k = 0; k < SW_W / 8; k++) mw[z - SW_Y][k] = ms[z - SW_Y][k] = 0;
                lo = py;
            }
            if (py > hi) {
                WORD k, z;
                for (z = (WORD)(hi < 0 ? py : hi + 1); z <= py; z++)
                    for (k = 0; k < SW_W / 8; k++) mw[z - SW_Y][k] = ms[z - SW_Y][k] = 0;
                hi = py;
            }
            if (white) mw[row][bx] |= bit; else ms[row][bx] |= bit;
        }
    }
    if (hi >= 0) {
        WORD y;
        hw_wait_blit();
        for (y = lo; y <= hi; y++) {
            const UBYTE *w = mw[y - SW_Y], *s = ms[y - SW_Y];
            UBYTE *row = b + (LONG)y * PLAY_STRIDE + SW_X / 8;
            WORD k;
            for (k = 0; k < SW_W / 8; k++) {
                UBYTE m = (UBYTE)(w[k] | s[k]), pl;
                UBYTE *d = row + k;
                if (!m) continue;
                for (pl = 0; pl < PLAY_DEPTH; pl++, d += ROW_BYTES) {
                    UBYTE v = (UBYTE)((((U_WHITE >> pl) & 1) ? w[k] : 0) | (((U_SILVER >> pl) & 1) ? s[k] : 0));
                    *d = (UBYTE)((*d & ~m) | v);
                }
            }
        }
    }
    tape_rows[bi][0] = hi >= 0 ? lo : -1;
    tape_rows[bi][1] = hi;
}

static UBYTE sw_clear;              /* klatki, przez ktore jeszcze czyscimy obszar okienka */
static BYTE  cache_lamp = -1;       /* lampka w zapamietanym tle okienka (-1 = tla brak, 3 = awaria) */

/* Tlo okienka rysowane w calosci przy pojawieniu sie i zmianie lampki (i zapamietane);
 * w klatkach odlotu tylko pas pod stara tasma wraca z pamieci i rysuje sie nowa tasma.
 * Po zniknieciu obszar wraca z tla toru przez kilka klatek w obu buforach. */
static void update_startwin(UBYTE *b, UBYTE bi)
{
    UBYTE st = startwin_state(), old = startwin_drawn[bi];
    if (st == SW_NONE) {
        if (old != SW_NONE) sw_clear = 6;
        if (sw_clear) {
            sw_clear--;
            blit_copy(b, g_video.background, SW_X, SW_Y, SW_W, SW_H);
        }
        startwin_drawn[bi] = SW_NONE;
        return;
    }
    if (st == old) return;
    if (!sw_cache) {
        draw_startwin_static(b, (UBYTE)(st & 3));
        tape_rows[bi][0] = -1;
    } else {
        /* Tlo okienka (piksel po pikselu - kilka klatek pracy) rysowane raz i trzymane w pamieci;
         * zmiana lampki przemalowuje w pamieci tylko lampke, a bufory kopiuja okienko z pamieci.
         * Dawniej pelne rysowanie w kazdym buforze przy czerwonym i przy zielonym gubilo klatki
         * w chwili startu. */
        UBYTE lamp = (UBYTE)(st & 3);
        BOOL full = (BOOL)(old == SW_NONE || (old & 3) == 3 || lamp == 3);   /* calosc: pojawienie sie, awaria */
        BOOL lamp_only = (BOOL)(!full && (old & 3) != lamp);                /* zmiana swiatla: same wiersze lampki */
        if (cache_lamp < 0 || (cache_lamp == 3) != (lamp == 3)) {
            draw_startwin_static(b, lamp);
            startwin_to_cache(b);
            cache_lamp = (BYTE)lamp;
            full = FALSE;
            tape_rows[bi][0] = -1;
        } else if (cache_lamp != (BYTE)lamp) {
            draw_lamp(sw_cache - (LONG)SW_Y * PLAY_STRIDE, lamp);   /* pamiec w ukladzie ekranu od SW_Y */
            cache_lamp = (BYTE)lamp;
        }
        if (full) {
            startwin_from_cache(b, SW_Y, SW_Y + SW_H - 1);
            tape_rows[bi][0] = -1;
        } else {
            if (lamp_only) startwin_from_cache(b, SW_Y + 8, SW_Y + 12);     /* wiersze lampki (draw_lamp) */
            if (tape_rows[bi][0] >= 0) startwin_from_cache(b, tape_rows[bi][0], tape_rows[bi][1]);
        }
    }
    if ((st & 3) != 3) draw_tape_ribbon(b, bi, (WORD)(st >> 2));
    startwin_drawn[bi] = st;
}

static BOOL state_uses_track(GameState s)
{
    return s == GS_HEAT || s == GS_HEAT_RESULT || s == GS_TRACK_GRADING;
}

static UBYTE race_lines_ok[2];          /* lista biegu ma juz stale kolory linii */
static BOOL  news_lines;                /* linie maja kolory gazety (papier) */
static BOOL  bg_ready;                  /* line_bg ma gradient menu (tor i gazeta go nadpisuja) */

static void update_line_colors(void)
{
    UWORD i;
    if (state_uses_track(g_tour.state)) {
        if (race_lines_ok[g_video.back]) return;    /* stale - nie zapisuj co klatke */
        race_lines_ok[g_video.back] = 1;
        bg_ready = FALSE;                           /* line_bg wspolne: po biegu gradient od nowa */
        /* ramka obrazu (overscan) w kolorze betonu trybun - bez czarnych pasow po bokach toru */
        for (i = 0; i < PLAY_H; i++) { line_bg[i] = BORDER_RACE; line_fg[i] = 0xFFF; }
    } else if (g_tour.state == GS_NEWS) {
        /* gazeta: kremowy papier i grafitowy tekst; gradient menu policzy sie od nowa */
        race_lines_ok[0] = race_lines_ok[1] = 0;
        news_lines = TRUE;
        for (i = 0; i < FULL_H; i++) { line_bg[i] = NEWS_PAPER; line_fg[i] = NEWS_INK; }
    } else {
        UWORD k = (UWORD)((g_frame >> 1) % 48), r = 0;
        race_lines_ok[0] = race_lines_ok[1] = 0;
        if (news_lines) { news_lines = FALSE; bg_ready = FALSE; }
        if (!bg_ready) {
            /* tlo: granat -> fiolet -> granat (stale, liczone raz) */
            bg_ready = TRUE;
            for (i = 0; i < FULL_H; i++) {
                UWORD t = (UWORD)((i < FULL_H / 2 ? i : FULL_H - 1 - i) * 13 / 16);
                line_bg[i] = (UWORD)(((t / 26) << 8) | ((t / 52) << 4) | (2 + t / 18));
            }
        }
        for (i = 0; i < FULL_H; i++) {
            /* tekst: przesuwajaca sie tecza (kolor co 3 linie) - bez dzielenia, co klatke */
            line_fg[i] = rainbow[k];
            if (++r == 3) { r = 0; if (++k == 48) k = 0; }
        }
    }
    video_set_lines(line_bg, line_fg);
}

/* Migajacy napis menu: pasek odtwarzany z tla (ilustracja na planszy tytulowej, inaczej
 * kolor 0) i napis rysowany co drugie mrugniecie. Pelne przerysowanie ekranu menu trwa
 * na A1200 bez Fast RAM kilka klatek - przy mruganiu co 16 klatek menu zwalnialo do ~5 kl./s. */
static void draw_blink_line(UBYTE *b)
{
    WORD y = 238;
    const char *s;
    switch (g_tour.state) {
    case GS_TITLE:   y = 206; s = T("NACIŚNIJ FIRE", "PRESS FIRE"); break;
    case GS_TABLE:   s = T("FIRE - DALEJ", "FIRE - CONTINUE"); break;
    case GS_PODIUM:  s = T("FIRE - NOWY TURNIEJ", "FIRE - NEW TOURNAMENT"); break;
    case GS_CREDITS: s = T("FIRE - POWRÓT", "FIRE - BACK"); break;
    case GS_RECORDS: s = T("FIRE - POWRÓT", "FIRE - BACK"); break;
    case GS_NEWS:    y = 240; s = T("FIRE - NASTĘPNY TURNIEJ", "FIRE - NEXT TOURNAMENT"); break;
    default: return;
    }
    if (g_paused) return;                                /* okno pauzy - pasek moglby je przeciac */
    if (g_tour.state == GS_TITLE && title_img) blit_copy(b, title_img, 0, y - 1, SCREEN_W, 11);
    else blit_fill(b, 0, y - 1, SCREEN_W, 11, 0);
    if (blink) {
        if (g_tour.state == GS_NEWS) text_draw_center(b, y, s, U_RED, 0);   /* papier: bez cienia */
        else text_draw_center(b, y, s, U_RAINBOW, U_SHADOW);
    }
#ifdef SHOW_LAG
    {
        char t[12];
        text_uint(t, g_lag % 100000, 1);
        blit_fill(b, 0, 0, 64, 12, 0);
        text_draw(b, 4, 2, t, U_RED, U_SHADOW);
    }
#endif
}

void ui_render(void)
{
    UBYTE *b = video_backbuffer();
    UBYTE bi = g_video.back;
    UBYTE new_blink = (UBYTE)((g_frame >> 4) & 1);

    if (new_blink != blink) {
        blink = new_blink;
        /* w menu miga jeden napis - odswiezany sam pasek; kursor edycji nazwiska - caly ekran */
        if (g_tour.state == GS_SETUP) { if (editing) ui_invalidate(); }
        else if (g_tour.state == GS_NAME_ENTRY) ui_invalidate();             /* migajacy kursor */
        else if (!state_uses_track(g_tour.state)) blink_dirty[0] = blink_dirty[1] = 1;
    }
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
            panel_ov[bi] = OV_NONE;                       /* swiezy tor - panel pusty */
        }
        if (panel_ov[bi] != OV_NONE)                      /* wyjezdzone sciezki - nie pod nakladka */
            track_flush_wear(bi, b, PANEL_X, PANEL_Y, PANEL_W, PANEL_H);
        else
            track_flush_wear(bi, b, 0, 0, 0, 0);
        /* okienko lezy w obszarze nakladek: przy nakladce schowane (panel je przykrywa),
         * po jej zdjeciu rysowane od nowa w nastepnej klatce */
        if (panel_ov[bi] != OV_NONE || current_overlay() != OV_NONE) startwin_drawn[bi] = SW_NONE;
        else update_startwin(b, bi);
        {
            /* nakladka zalezna od stanu gry (np. podpowiedz pod tasma) - zmiana = przerysowanie */
            static UBYTE last_ov = OV_NONE, last_mask;
            UBYTE ov = (UBYTE)current_overlay(), mask = not_holding_mask();
            if (ov != last_ov || mask != last_mask) { last_ov = ov; last_mask = mask; ui_invalidate(); }
        }
        if (drawn_serial[bi] != serial) {
            /* Panel nakladek (224x112, 8 planow) kopiowany z tla tylko, gdy jest co pokazac albo
             * zdjac - zmiany fazy startu (czerwone, zielone, ruszenie) bez nakladki go nie ruszaja.
             * Dawniej 4 takie kopie w pierwszych klatkach po zielonym gubily klatki na starcie. */
            UBYTE ov = (UBYTE)current_overlay();
            drawn_serial[bi] = serial;
            if (ov == OV_NONE && panel_ov[bi] == OV_NONE) goto panel_done;
            panel_ov[bi] = ov;
            blit_copy(b, g_video.background, PANEL_X, PANEL_Y, PANEL_W, PANEL_H);
            switch (ov) {
            case OV_FALSE_START: draw_false_start(b); break;
            case OV_RESULT:      draw_result(b); break;
            case OV_GRADING:     draw_grading(b); break;
            case OV_PAUSE:       draw_pause(b, PANEL_Y + 10); break;
            case OV_HOLD:        draw_hold(b); break;
            default: break;
            }
        }
panel_done:
        render_tape(b);
        return;
    }

    if (drawn_serial[bi] == serial && buf_is_track[bi] == 0) {
        if (blink_dirty[bi]) { blink_dirty[bi] = 0; draw_blink_line(b); }
        if (g_tour.state == GS_PRESENT) present_girls(b, bi);    /* wychodzace podprowadzajace */
        return;
    }
    blink_dirty[bi] = 0;
    blit_clear(b);
    buf_is_track[bi] = 0;
    roost_forget_buffer(bi);
    drawn_serial[bi] = serial;
    switch (g_tour.state) {
    case GS_TITLE:     draw_title(b); break;
    case GS_MENU:      draw_menu(b); break;
    case GS_CREDITS:   draw_credits(b); break;
    case GS_HELP:      draw_help(b); break;
    case GS_RECORDS:   draw_records(b); break;
    case GS_NAME_ENTRY: draw_name_entry(b); break;
    case GS_NEWS:      draw_news(b); break;
    case GS_SETUP:     draw_setup(b); break;
    case GS_OPTIONS:   draw_options(b); break;
    case GS_TABLE:     draw_table(b); break;
    case GS_PRESENT:   draw_present(b, bi); break;
    case GS_GATE_PICK: draw_pick(b); break;
    case GS_PODIUM:    draw_podium(b); break;
    default: break;
    }
    if (g_paused) draw_pause(b, 82);
}
