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
/* okienko maszyny startowej (prawy dolny rog toru) */
#define SW_X 208
#define SW_Y 138                   /* ponizej obszaru nakladek (PANEL_Y + PANEL_H = 136) */
#define SW_W 112
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

void ui_init(void)
{
    rgb12_lerp_table();
    /* ilustracja z tools/genart.py; bez pliku plansza ma sam gradient i napisy */
    title_img = AllocMem(FULL_SIZE, MEMF_CHIP);
    if (title_img && !asset_load("PROGDIR:data/title.bpl", title_img, FULL_SIZE)) {
        FreeMem(title_img, FULL_SIZE);
        title_img = 0;
    }
    sw_cache = AllocMem((ULONG)SW_H * PLAY_STRIDE, MEMF_ANY);   /* bez pamieci: pelne przerysowanie */
    girls_img = AllocMem(4 * GIRL_SIZE, MEMF_ANY);
    if (girls_img && !asset_load("PROGDIR:data/girls.bin", girls_img, 4 * GIRL_SIZE)) {
        FreeMem(girls_img, 4 * GIRL_SIZE);
        girls_img = 0;                  /* bez pliku: same tablice */
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

void ui_free(void)
{
    if (title_img) FreeMem(title_img, FULL_SIZE);
    if (sw_cache) FreeMem(sw_cache, (ULONG)SW_H * PLAY_STRIDE);
    if (girls_img) FreeMem(girls_img, 4 * GIRL_SIZE);
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
static void draw_title(UBYTE *b)
{
    if (title_img) blit_copy(b, title_img, 0, 0, SCREEN_W, FULL_H);
    text_draw_big_center(b, 12, "SPEEDWAY", U_RAINBOW, U_SHADOW);
    text_draw_big_center(b, 38, "PARTY", U_GOLD, U_SHADOW);
    text_draw_center(b, 64, "AMIGA 1200", U_WHITE, U_SHADOW);
    if (blink) text_draw_center(b, 206, "NACIŚNIJ FIRE", U_RAINBOW, U_SHADOW);
    text_draw_center(b, 224, "TURNIEJ 15 BIEGÓW DLA 1-4 GRACZY", U_WHITE, U_SHADOW);
    text_draw_center(b, 240, "MIT © 2026 b4rtiga", U_GREY, 0);
}

/* ==================== WYBOR PO PLANSZY TYTULOWEJ ==================== */

/* Na tle ilustracji tytulowej: JEDZIEMY! (dalej do ustawien), INSTRUKCJA albo CREDITS. */
#define MENU_ITEMS 3
#define MENU_Y0    156                      /* pierwsza pozycja; kolejne co MENU_DY */
#define MENU_DY    22
static UBYTE menu_row;

void ui_menu_enter(UBYTE row) { menu_row = (UBYTE)(row < MENU_ITEMS ? row : 0); }

UBYTE ui_menu_update(void)
{
    UBYTE m = input_menu();
    if (m & (BTN_UP | BTN_LEFT))    { menu_row = (UBYTE)((menu_row + MENU_ITEMS - 1) % MENU_ITEMS); ui_invalidate(); }
    if (m & (BTN_DOWN | BTN_RIGHT)) { menu_row = (UBYTE)((menu_row + 1) % MENU_ITEMS); ui_invalidate(); }
    if (ui_confirm()) return (UBYTE)(menu_row + 1);
    return 0;
}

static void draw_menu(UBYTE *b)
{
    static const char *items[MENU_ITEMS] = { "JEDZIEMY!", "INSTRUKCJA", "CREDITS" };
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

static void draw_credits(UBYTE *b)
{
    text_draw_big_center(b, 10, "SPEEDWAY PARTY", U_RAINBOW, U_SHADOW);
    text_draw_center(b, 36, "TWÓRCY", U_GOLD, U_SHADOW);
    text_draw_center(b, 62, "GRĘ STWORZYŁ b4rtiga", U_WHITE, U_SHADOW);
    text_draw_center(b, 86, "PRZY POMOCY SZTUCZNEJ INTELIGENCJI:", U_GREY, 0);
    text_draw_center(b, 98, "CLAUDE (ANTHROPIC) ORAZ GPT-6 ASTRA", U_WHITE, U_SHADOW);
    text_draw_center(b, 122, "KOD ŹRÓDŁOWY (LICENCJA MIT):", U_GREY, 0);
    text_draw_center(b, 134, "github.com/b4rtiga/Speedway-Party", U_GOLD, U_SHADOW);
    text_draw_center(b, 160, "DZIĘKUJĘ ZA ZAINTERESOWANIE GRĄ!", U_WHITE, U_SHADOW);
    text_draw_center(b, 176, "ŻYCZĘ UDANEJ ZABAWY", U_WHITE, U_SHADOW);
    text_draw_center(b, 188, "I SZEROKIEJ NA OSTATNIM ŁUKU!", U_GOLD, U_SHADOW);
    if (blink) text_draw_center(b, 238, "FIRE - POWRÓT", U_RAINBOW, U_SHADOW);
}

/* ========================== INSTRUKCJA =========================== */

/* Trzy strony tekstu fontem gry (najwyzej 40 znakow w wierszu); na trzeciej schemat owalu
 * z idealna linia. Tresc zgodna z mechanika (race.c, start.c, track.c, tournament.c). */
#define HELP_PAGES 3
static UBYTE help_page;
static void plot(UBYTE *buf, WORD x, WORD y, UBYTE c);
static void rect_px(UBYTE *b, WORD x, WORD y, WORD w, WORD h, UBYTE c);

static const char *const help_title[HELP_PAGES] = { "ZASADY TURNIEJU", "START I STEROWANIE", "LINIA JAZDY" };
static const char *const help_text0[] = {
    "TURNIEJ TO 15 BIEGÓW PO 4 OKRĄŻENIA.",
    "W KAŻDYM BIEGU JADĄ CZTEREJ ZAWODNICY,",
    "PUNKTY ZA MIEJSCA: 3 - 2 - 1 - 0.",
    "KAŻDY STARTUJE Z KAŻDEGO POLA A-D.",
    "W BIEGACH NOMINOWANYCH LIDER TABELI",
    "WYBIERA POLE PIERWSZY. REMIS O PODIUM",
    "ROZSTRZYGA BIEG DODATKOWY.",
    "PO BIEGACH 4, 7, 11 I 13 TOR JEST",
    "RÓWNANY I POLEWANY, POTEM PRZESYCHA.",
    "",
    "FLAGI SĘDZIEGO:",
    "CZERWONA - PRZERWANIE BIEGU",
    "CZARNA - WYKLUCZENIE ZAWODNIKA",
    "ŻÓŁTA Z KRZYŻEM - OSTATNIE OKRĄŻENIE",
    "SZACHOWNICA - KONIEC BIEGU",
    0 };
static const char *const help_text1[] = {
    "GAZ JEST ZAWSZE ODKRĘCONY DO KOŃCA.",
    "LEWO - ŁAMIESZ MOTOCYKL W UŚLIZG,",
    "       SKRĘCASZ W ŁUK (DO 40 STOPNI).",
    "PRAWO - KONTRA, PROSTUJE MOTOCYKL.",
    "",
    "START SPOD TAŚMY:",
    "1. TRZYMAJ LEWO LUB PRAWO.",
    "2. PATRZ NA ŚWIATŁA I OKIENKO TAŚMY.",
    "3. PUŚĆ OD RAZU NA ZIELONYM!",
    "IM SZYBCIEJ PUŚCISZ, TYM LEPSZY START.",
    "ZA WCZEŚNIE - TAŚMA I OSTRZEŻENIE,",
    "DRUGA TAŚMA - WYKLUCZENIE Z BIEGU.",
    "ZA PÓŹNO - KOŁO MIELI W MIEJSCU.",
    "",
    "ESC - PAUZA.",
    0 };
static const char *const help_text2[] = {
    "NAJLEPSZA LINIA JAZDY JEST DYNAMICZNA:",
    "NA KAŻDYCH ZAWODACH TOR UKŁADA SIĘ",
    "INACZEJ. SZUKAJ JEJ OD PIERWSZEGO BIEGU.",
    "",
    "NA PROSTEJ JEDŹ PROSTO: UŚLIZG HAMUJE.",
    "W ŁUKU PULSUJ PRZYCISKIEM, NIE TRZYMAJ.",
    "LUŹNE ODSYPISKO POD BANDĄ ZAKOPUJE",
    "WOLNYCH. SZYBKO I W LEKKIM UŚLIZGU -",
    "SZEROKA NIESIE. OMIJAJ SZPRYCĘ I BANDĘ.",
    0 };
static const char *const *const help_text[HELP_PAGES] = { help_text0, help_text1, help_text2 };

void ui_help_enter(UBYTE page) { help_page = (UBYTE)(page < HELP_PAGES ? page : 0); }

BOOL ui_help_update(void)
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
static void help_oval_point(WORD s, WORD r, WORD *x, WORD *y)
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

static void draw_help_oval(UBYTE *b)
{
    WORD s, x, y;
    for (s = 0; s < 256; s++) {
        help_oval_point(s, 36, &x, &y); plot(b, x, y, U_WHITE);        /* banda */
        help_oval_point(s, 13, &x, &y); plot(b, x, y, U_RED);          /* kraweznik */
    }
    for (s = 0; s < 4; s++)                                            /* strzalka: jazda w prawo */
        rect_px(b, HD_CX - 4 + s * 2, HD_CY + 20 + s, 2, (WORD)(7 - 2 * s), U_GREEN);
}

static void draw_help(UBYTE *b)
{
    const char *const *t = help_text[help_page];
    char s[32], *p;
    WORD y = (WORD)(help_page == 2 ? 124 : 52);
    text_draw_big_center(b, 6, "INSTRUKCJA", U_RAINBOW, U_SHADOW);
    p = text_cat(s, help_title[help_page]);
    p = text_cat(p, "  ");
    p = text_uint(p, (ULONG)(help_page + 1), 1);
    p = text_cat(p, "/");
    text_uint(p, HELP_PAGES, 1);
    text_draw_center(b, 32, s, U_GOLD, 0);
    if (help_page == 2) draw_help_oval(b);
    for (; *t; t++, y += 11) text_draw_center(b, y, *t, U_WHITE, U_SHADOW);
    text_draw_center(b, 240, "LEWO/PRAWO - STRONA   FIRE - POWRÓT", U_GREY, 0);
}

/* ======================== GAZETA PO ZAWODACH ======================= */

/* "GLOS OWALU": kremowy papier i grafitowy tekst (kolor 0 i 100 ustawia Copper - ta sama
 * paleta co reszta menu), czerwony akcent, linie i raster z pikseli. Dane z news_summary(). */
#define NEWS_PAPER 0xEDB                     /* 12-bit: kremowy papier (kolor 0) */
#define NEWS_INK   0x222                     /* grafit (kolor 100 = U_RAINBOW na tym ekranie) */
static const char *const news_head[5][2] = {
    { "TYTUŁ PO",    "DOGRYWCE!" },
    { "ZWYCIĘSTWO",  "Z REKORDEM!" },
    { "BEZ STRATY",  "PUNKTU!" },
    { "JEDEN PUNKT", "RÓŻNICY!" },
    { "OWAL MA",     "ZWYCIĘZCĘ!" } };
static const char *const news_body[5][2] = {
    { "O ZWYCIĘSTWIE ZDECYDOWAŁ",       "BIEG DODATKOWY." },
    { "TRIUMFATOR ZAPISAŁ SIĘ TAKŻE",   "W TABELI REKORDÓW TORU." },
    { "W KAŻDYM SWOIM STARCIE",         "ZWYCIĘZCA BYŁ PIERWSZY." },
    { "TAK NIEWIELE DZIELIŁO PIERWSZE", "I DRUGIE MIEJSCE." },
    { "TURNIEJ ROZSTRZYGNIĘTY.",        "OTO KOŃCOWA KLASYFIKACJA." } };
static const char *const news_fact[4] = { "FALSTARTY: ", "WYKLUCZENIA: ", "ZWYCIĘSTWA LIDERA: ", "GRACZE PRZY STOLE: " };

/* liczba wyrownana do prawej krawedzi x1 */
static void text_uint_right(UBYTE *b, WORD x1, WORD y, ULONG v, UBYTE c)
{
    char s[8];
    text_uint(s, v, 1);
    text_draw(b, (WORD)(x1 - text_len(s) * FONT_W), y, s, c, 0);
}

static void draw_news(UBYTE *b)
{
    const NewsSummary *n = news_summary();
    char s[48], *p;
    UBYTE i;
    WORD x;
    text_draw_big_center(b, 4, "GŁOS OWALU", U_SHADOW, 0);
    p = text_cat(s, "WYDANIE SPECJALNE / TURNIEJ NR ");
    text_uint(p, n->number, 2);
    text_draw_center(b, 28, s, U_RAINBOW, 0);
    rect_px(b, 8, 40, SCREEN_W - 16, 2, U_SHADOW);
    rect_px(b, 8, 44, SCREEN_W - 16, 1, U_SHADOW);
    for (x = 8; x < SCREEN_W - 8; x += 4) rect_px(b, x, 46, 2, 1, U_RAINBOW);   /* raster */

    text_draw_big(b, 16, 52, news_head[n->rule][0], U_SHADOW, 0);
    text_draw_big(b, 16, 74, news_head[n->rule][1], U_RED, 0);
    p = name_trunc(s, n->name[0], NEWS_NAME);
    text_cat(p, " WYGRYWA TURNIEJ");
    text_draw(b, 16, 100, s, U_SHADOW, 0);
    text_draw(b, 16, 114, news_body[n->rule][0], U_RAINBOW, 0);
    text_draw(b, 16, 125, news_body[n->rule][1], U_RAINBOW, 0);

    rect_px(b, 16, 139, SCREEN_W - 32, 1, U_SHADOW);
    text_draw(b, 16, 143, "MIEJSCE / ZAWODNIK", U_RAINBOW, 0);
    text_draw(b, 216, 143, "PKT", U_RAINBOW, 0);
    text_draw(b, 256, 143, "WYGR.", U_RAINBOW, 0);
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
    if (blink) text_draw_center(b, 240, "FIRE - NASTĘPNY TURNIEJ", U_RED, 0);
}

BOOL ui_title_update(void)
{
    return (input_menu() & BTN_FIRE) != 0;
}

/* ========================= USTAWIENIA ============================= */

#define SETUP_ROWS   8
#define ROW_COND     5
#define ROW_MUSIC    6
#define ROW_START    7
static const char *cond_names[3] = { "SUCHY", "WILGOTNY", "MOKRY" };
static const char *port_names[INPUT_NONE] = {
    "JOY 1", "JOY 0", "JOY 3 LPT", "JOY 4 LPT", "KLAW Z/X", "KLAW N/M", "STRZAŁKI"
};
static const char *level_names[3] = { "NOWICJUSZ", "PROFESJONALISTA", "MISTRZ" };

/* znaki do wyboru joystickiem */
static const UBYTE charset[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ\x80\x81\x82\x83\x84\x85\x86\x87\x88" "0123456789-.";

void ui_setup_enter(void)
{
    setup_row = ROW_START;
    editing = FALSE;
}

static WORD setup_row_y(UBYTE row) { return (WORD)(50 + row * 18); }

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
        text_draw(b, human ? 216 : 192, y, human ? port_names[r->input] : level_names[r->ai_level], U_GOLD, U_SHADOW);   /* PROFESJONALISTA = 15 znakow */
    }
    text_draw(b, 40, setup_row_y(ROW_COND), "STAN TORU:", U_WHITE, U_SHADOW);
    p = text_cat(s, "< ");
    p = text_cat(p, cond_names[g_tour.start_condition]);
    text_cat(p, " >");
    text_draw(b, 168, setup_row_y(ROW_COND), s, U_GOLD, U_SHADOW);
    text_draw(b, 40, setup_row_y(ROW_MUSIC), "MUZYKA:", U_WHITE, U_SHADOW);
    text_draw(b, 168, setup_row_y(ROW_MUSIC), g_music_enabled ? "< TAK >" : "< NIE >", U_GOLD, U_SHADOW);
    text_draw_center(b, setup_row_y(ROW_START), "START TURNIEJU", setup_row == ROW_START ? U_RAINBOW : U_WHITE, U_SHADOW);

    text_draw_center(b, 206, editing ? "PISZ Z KLAWIATURY / GÓRA-DÓŁ LITERA"
                                     : "GÓRA/DÓŁ: WYBÓR  LEWO/PRAWO: ZMIANA", U_GREY, 0);
    text_draw_center(b, 222, editing ? "PRAWO: DODAJ  LEWO: USUŃ  FIRE: OK"
                                     : "FIRE: EDYCJA NAZWISKA / START", U_GREY, 0);
    text_draw_center(b, 238, "ESC - WYJŚCIE Z GRY", U_GREY, 0);
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
            g_tour.num_humans = (UBYTE)(n < 0 ? 0 : n > MAX_RIDERS ? MAX_RIDERS : n);   /* 0 = pokaz: same boty */
            tournament_default_names();
        } else if (setup_row <= MAX_RIDERS) {
            Rider *r = &g_tour.riders[setup_row - 1];
            if (setup_row - 1 < g_tour.num_humans)
                r->input = (InputPort)((r->input + INPUT_NONE + d) % INPUT_NONE);
            else
                r->ai_level = (AiLevel)((r->ai_level + 3 + d) % 3);
        } else if (setup_row == ROW_COND) {
            g_tour.start_condition = (UBYTE)((g_tour.start_condition + 3 + d) % 3);
        } else if (setup_row == ROW_MUSIC) {
            g_music_enabled = (BOOL)!g_music_enabled;
        }
    }
    if (m & BTN_FIRE) {
        if (setup_row == ROW_START) return TRUE;
        if (setup_row == ROW_MUSIC) { g_music_enabled = (BOOL)!g_music_enabled; changed = TRUE; }
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
    text_cat(text_cat(s, "TOR: "), w > 170 ? "MOKRY" : w > 60 ? "WILGOTNY" : "SUCHY");
}

/* cyfry 3x5 do naglowka tabeli (numery biegow) - bity: 5 wierszy po 3 */
static const UWORD tiny_digit[10] = {
    0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF
};

static void plot(UBYTE *buf, WORD x, WORD y, UBYTE c);

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

static void tiny_number(UBYTE *b, WORD x, WORD y, UBYTE v, UBYTE c)
{
    UBYTE d[2], n = 0, i, k;
    if (v >= 10) d[n++] = (UBYTE)(v / 10);
    d[n++] = (UBYTE)(v % 10);
    x -= (WORD)(n * 4 - 1) / 2;                          /* x = srodek liczby */
    for (i = 0; i < n; i++, x += 4)
        for (k = 0; k < 15; k++)
            if (tiny_digit[d[i]] & (0x4000 >> k)) plot(b, (WORD)(x + k % 3), (WORD)(y + k / 3), c);
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
static void draw_standings(UBYTE *b, WORD y)
{
    UBYTE order[MAX_RIDERS], i, h, next = g_tour.current_heat;
    WORD bottom = (WORD)(y + TB_HEAD + MAX_RIDERS * TB_ROW);
    char s[48], *p;

    tournament_sort(order);
    rect_px(b, TB_X, y - 2, SCREEN_W - 2 * TB_X, bottom - y + 3, U_BORDER);
    rect_px(b, TB_X + 1, y - 1, SCREEN_W - 2 * TB_X - 2, bottom - y + 1, U_PANEL);
    if (next < NUM_HEATS)                                 /* najblizszy bieg podswietlony */
        rect_px(b, (WORD)(TB_GRID + next * TB_CELL + 1), y - 1, TB_CELL - 1, bottom - y + 1, U_CURSOR);
    text_draw(b, TB_NAME, y, "ZAWODNIK", U_GREY, 0);
    text_draw(b, TB_PTS + 4, y, "PKT", U_GREY, 0);
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
            s[0] = v == HP_EXCLUDED ? 'W' : (char)('0' + v); s[1] = 0;
            text_draw(b, (WORD)(TB_GRID + h * TB_CELL + 3), yy, s,
                      v == HP_EXCLUDED ? U_BORDER : v == 3 ? U_GOLD : U_WHITE, 0);
        }
        p = text_uint(s, r->points, 1);
        *p = 0;
        text_draw(b, (WORD)(SCREEN_W - TB_X - 6 - text_len(s) * 8), yy, s, U_GOLD, U_SHADOW);
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
        helmet_box(b, 64, y, g_tour.heat_table[hn][g]);   /* kolor kasku zawodnika */
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
    if (blink) text_draw_center(b, 238, "FIRE - DALEJ", U_RAINBOW, U_SHADOW);
}

/* ===================== PREZENTACJA PRZED BIEGIEM ====================== */

/* Podprowadzajace: grafika 48x96 z tools/gengirls.py (data/girls.bin, 4 postacie planarnie,
 * 6 bajtow szerokosci). Kopia z maska: przezroczyste piksele (kolor 0) zostawiaja tlo,
 * wiec dlonie nachodza na tablice. Kolory w palecie toru (180..191, 221..239).
 *
 * Postac i tablica maja kolor kasku zawodnika; kazda stoi na polu, z ktorego startuje jej
 * zawodnik, i kolysze biodrami: wiersze w pasie bioder przesuniete w bok o -2..2 px
 * (glowa, rece z tablica i stopy w miejscu), kazda w innej fazie. Przerysowywany tylko
 * ten pas i tylko przy zmianie fazy. */
#define PRES_Y      72                      /* gora postaci */
#define PRES_TIME   (3 * FPS)               /* prezentacja: 3 s albo FIRE */
#define SWAY_Y0     30                      /* pas bioder (wiersze postaci) */
#define SWAY_Y1     88
#define SWAY_MID    58
#define SWAY_STEP   2                       /* klatek na faze (32 fazy = ok. 1.3 s) */
#define SWAY_AMP    5                       /* wychylenie bioder w polowkach piksela (2.5 px) */
static UWORD present_t;                     /* klatki od wejscia na ekran prezentacji */
static UBYTE present_drawn[2][4];           /* faza narysowana w buforze (0xFF = brak) */
static const BYTE sway_tab[32] = { 0, 12, 24, 36, 45, 53, 59, 63, 64, 63, 59, 53, 45, 36, 24, 12,
                                   0, -12, -24, -36, -45, -53, -59, -63, -64, -63, -59, -53, -45, -36, -24, -12 };   /* 64 sin */

static WORD gate_x(UBYTE g) { return (WORD)(16 + g * 80); }
/* faza podprowadzajacej g: kazda w innym rytmie i zmienia faze w innej klatce (praca rozlozona) */
static UBYTE sway_phase(UBYTE g) { return (UBYTE)(((present_t + g) / SWAY_STEP + g * 10) & 31); }

void ui_present_start(void)
{
    UBYTE i, g;
    present_t = 0;
    for (i = 0; i < 2; i++) for (g = 0; g < 4; g++) present_drawn[i][g] = 0xFF;
}

/* raz na klatke w GS_PRESENT; zwraca TRUE po czasie prezentacji */
BOOL ui_present_update(void)
{
    if (present_t < 0xFFF0) present_t++;
    return (BOOL)(present_t >= PRES_TIME);
}

/* przesuniecie wiersza y przy fazie kolysania amp (64 * sin): najwiecej w biodrach, ku glowie
 * i stopom zanika - kazdy wiersz przeskakuje o piksel w innej chwili, ruch wyglada plynnie */
static WORD sway_dx(WORD y, BYTE amp)
{
    WORD d = (WORD)(y < SWAY_MID ? SWAY_MID - y : y - SWAY_MID), span = y < SWAY_MID ? SWAY_MID - SWAY_Y0 : SWAY_Y1 - SWAY_MID;
    LONG v;
    if (y <= SWAY_Y0 || y >= SWAY_Y1) return 0;
    v = (LONG)amp * (span - d) * SWAY_AMP;                 /* w 1/(64*span*2) piksela */
    return (WORD)((v + (v >= 0 ? span * 64 : -span * 64)) / (span * 128));
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

static void draw_girl(UBYTE *b, WORD x0, WORD y0, UBYTE color, BYTE amp)
{
    /* tablica nad glowa: kolor kasku zawodnika, ciemny kontur */
    rect_px(b, x0 - 6, y0 - 22, GIRL_W + 12, 24, U_SHADOW);
    rect_px(b, x0 - 4, y0 - 20, GIRL_W + 8, 20, (UBYTE)(U_HELMET + color));
    if (girls_img) draw_girl_rows(b, x0, y0, color, 0, GIRL_H, amp);
}

/* kolysanie: pas bioder przerysowany przy zmianie fazy (w kazdym buforze osobno) */
static void present_girls(UBYTE *b, UBYTE bi)
{
    UBYTE hn = g_tour.current_heat, g;
    if (!girls_img) return;
    for (g = 0; g < 4; g++) {
        UBYTE ph = sway_phase(g);
        if (g_tour.heat_table[hn][g] == 0xFF || present_drawn[bi][g] == ph) continue;
        rect_px(b, gate_x(g) - 8, PRES_Y + SWAY_Y0 + 1, GIRL_W + 16, SWAY_Y1 - SWAY_Y0 - 1, 0);
        draw_girl_rows(b, gate_x(g), PRES_Y, g_tour.heat_table[hn][g], SWAY_Y0 + 1, SWAY_Y1, sway_tab[ph]);
        present_drawn[bi][g] = ph;
    }
}

static void draw_present(UBYTE *b, UBYTE bi)
{
    UBYTE hn = g_tour.current_heat, g;
    char s[48], *p;
    if (hn == RUNOFF_HEAT) text_cat(s, "BIEG DODATKOWY");
    else { p = text_cat(s, "BIEG "); text_uint(p, (ULONG)(hn + 1), 1); }
    text_draw_big_center(b, 8, s, U_RAINBOW, U_SHADOW);
    text_draw_center(b, 34, "PREZENTACJA ZAWODNIKÓW", U_GOLD, 0);
    for (g = 0; g < 4; g++) {
        WORD cx = (WORD)(gate_x(g) + GIRL_W / 2);
        UBYTE r = g_tour.heat_table[hn][g];
        if (r == 0xFF) continue;                         /* puste pole (bieg dodatkowy) */
        draw_girl(b, gate_x(g), PRES_Y, r, sway_tab[sway_phase(g)]);
        present_drawn[bi][g] = sway_phase(g);
        name_trunc(s, g_tour.riders[r].name, 9);
        text_draw(b, (WORD)(cx - text_len(s) * 4), PRES_Y + GIRL_H + 8, s, U_WHITE, U_SHADOW);
        s[0] = gate_letter[g]; s[1] = 0;
        text_draw(b, cx - 4, PRES_Y + GIRL_H + 22, s, U_GREY, 0);
    }
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
static void draw_hold(UBYTE *b)
{
    UBYTE m = not_holding_mask(), g;
    WORD y = PANEL_Y + 34;
    char s[48];
    panel(b, PANEL_X, PANEL_Y + 10, PANEL_W, 76, U_PANEL, U_GOLD);
    text_draw_center(b, PANEL_Y + 18, "CZEKAMY NA START", U_GOLD, U_SHADOW);
    for (g = 0; g < MAX_RIDERS && y < PANEL_Y + 60; g++)
        if (m & (1 << g)) {
            name_trunc(s, g_tour.riders[g_tour.bikes[g].rider].name, NAME_EDIT_MAX);
            text_draw_center(b, y, s, U_WHITE, U_SHADOW);
            y += 12;
        }
    text_draw_center(b, PANEL_Y + 66, "TRZYMAJ LEWO LUB PRAWO", U_WHITE, U_SHADOW);
}

/* pauza: panel z pytaniem (na torze w prostokacie nakladek, w menu na srodku ekranu) */
static void draw_pause(UBYTE *b, WORD y)
{
    panel(b, PANEL_X, y, PANEL_W, 92, U_PANEL, U_GOLD);
    text_draw_big_center(b, y + 8, "PAUZA", U_GOLD, U_SHADOW);
    text_draw_center(b, y + 38, g_pause_quit ? "ZAKOŃCZYĆ GRĘ?" : "WYJŚĆ DO MENU GŁÓWNEGO?", U_WHITE, U_SHADOW);
    text_draw_center(b, y + 56, "ESC - TAK", U_GOLD, U_SHADOW);
    text_draw_center(b, y + 72, "FIRE - GRAJ DALEJ", U_WHITE, U_SHADOW);
}

static void draw_false_start(UBYTE *b)
{
    Heat *h = &g_tour.heat;
    Bike *bk = &g_tour.bikes[h->false_starter];
    Rider *r = &g_tour.riders[bk->rider];
    char s[48], *p;

    panel(b, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, U_PANEL, U_RED);
    text_draw_big_center(b, PANEL_Y + 12, "TAŚMA!", U_RED, U_SHADOW);
    helmet_box(b, 80, PANEL_Y + 48, bk->rider);
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
            helmet_box(b, 80, y, bk->rider);
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
    /* na stale, bez migania (plansza i tak sama przechodzi dalej po kilku sekundach) */
    if (g_tour.new_records)
        text_draw_center(b, PANEL_Y + 98, g_tour.new_records & REC_HEAT ? "NOWY REKORD TORU!" : "REKORD OKRĄŻENIA!",
                         U_GOLD, U_SHADOW);
    else text_draw_center(b, PANEL_Y + 98, "FIRE - DALEJ", U_GREY, 0);
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
#define SW_NONE 0xFF
static UBYTE startwin_drawn[2] = { SW_NONE, SW_NONE };

/* stan okienka: SW_NONE albo (klatka odlotu << 2) | lampka (0 zgaszona, 1 czerwona, 2 zielona); 3 = awaria */
#define SW_FLY   8                 /* klatki odlotu tasmy w okienku (0.16 s) - okienko znika po nich */
#define SW_RISE  54                /* o ile tasma idzie w gore (az za gorna krawedz okienka) */
#define SW_BOW   8                 /* ugiecie srodka w polowie odlotu */
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
    if (y >= SW_Y + 8 && y < SW_Y + 13 && x >= SW_X + 52 && x < SW_X + 60) return lamp_col[lamp];
    if (y < SW_Y + 24) {                                                     /* trybuna z kibicami */
        if (y >= SW_Y + 4 && (y - (SW_Y + 4)) % 3 == 0 && ((x - (SW_X + 3)) & 1) == 0)
            return (UBYTE)(140 + ((x * 7 + y * 13) % 32));
        return 129;
    }
    if (((x * 37 + y * 91) & 7) == 0) return (UBYTE)(19 + ((x + y) & 3));   /* faktura nawierzchni */
    return 24;
}

static void draw_lamp(UBYTE *b, UBYTE lamp);

static void draw_startwin_static(UBYTE *b, UBYTE lamp)
{
    WORD x, y;
    panel(b, SW_X, SW_Y, SW_W, SW_H, U_PANEL, U_BORDER);
    if (lamp == 3) {                                         /* awaria maszyny startowej */
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
            plot(b, x, y, startwin_px(x, y, lamp));
    for (y = SW_Y + 24; y < SW_Y + SW_H - 2; y++)            /* faktura nawierzchni */
        for (x = SW_X + 2; x < SW_X + SW_W - 2; x++)
            if (((x * 37 + y * 91) & 7) == 0) plot(b, x, y, startwin_px(x, y, lamp));
    for (y = SW_Y + 6; y < SW_Y + SW_H - 4; y++)             /* slupki maszyny */
        for (x = 0; x < 3; x++) {
            UBYTE c = ((y >> 2) & 1) ? U_RED : U_WHITE;
            plot(b, SW_X + 8 + x, y, c);
            plot(b, SW_X + SW_W - 11 + x, y, c);
        }
    for (x = SW_X + 8; x < SW_X + SW_W - 8; x++) {           /* poprzeczka */
        plot(b, x, SW_Y + 6, U_GREY);
        plot(b, x, SW_Y + 7, U_GREY);
    }
    draw_lamp(b, lamp);
}

static void draw_lamp(UBYTE *b, UBYTE lamp)
{
    WORD x, y;
    for (y = SW_Y + 8; y < SW_Y + 13; y++)
        for (x = SW_X + 52; x < SW_X + 60; x++) plot(b, x, y, startwin_px(x, y, lamp));
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
    LONG rise = (LONG)SW_RISE * (SW_FLY * SW_FLY - q * q) / (SW_FLY * SW_FLY);
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
        BOOL full = (BOOL)(old == SW_NONE || (old & 3) != lamp);
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
        } else if (tape_rows[bi][0] >= 0) {
            startwin_from_cache(b, tape_rows[bi][0], tape_rows[bi][1]);
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

static void update_line_colors(void)
{
    UWORD i;
    if (state_uses_track(g_tour.state)) {
        if (race_lines_ok[g_video.back]) return;    /* stale - nie zapisuj co klatke */
        race_lines_ok[g_video.back] = 1;
        /* ramka obrazu (overscan) w kolorze betonu trybun - bez czarnych pasow po bokach toru */
        for (i = 0; i < PLAY_H; i++) { line_bg[i] = BORDER_RACE; line_fg[i] = 0xFFF; }
    } else if (g_tour.state == GS_NEWS) {
        /* gazeta: kremowy papier i grafitowy tekst; gradient menu policzy sie od nowa */
        race_lines_ok[0] = race_lines_ok[1] = 0;
        news_lines = TRUE;
        for (i = 0; i < FULL_H; i++) { line_bg[i] = NEWS_PAPER; line_fg[i] = NEWS_INK; }
    } else {
        static BOOL bg_ready;
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
    case GS_TITLE:   y = 206; s = "NACIŚNIJ FIRE"; break;
    case GS_TABLE:   s = "FIRE - DALEJ"; break;
    case GS_PRESENT: s = "FIRE - POD TAŚMĘ!"; break;
    case GS_PODIUM:  s = "FIRE - NOWY TURNIEJ"; break;
    case GS_CREDITS: s = "FIRE - POWRÓT"; break;
    case GS_NEWS:    y = 240; s = "FIRE - NASTĘPNY TURNIEJ"; break;
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
    UBYTE *b = back();
    UBYTE bi = g_video.back;
    UBYTE new_blink = (UBYTE)((g_frame >> 4) & 1);

    if (new_blink != blink) {
        blink = new_blink;
        /* w menu miga jeden napis - odswiezany sam pasek; kursor edycji nazwiska - caly ekran */
        if (g_tour.state == GS_SETUP) { if (editing) ui_invalidate(); }
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
        track_flush_wear(bi, b);                          /* wyjezdzone sciezki */
        update_startwin(b, bi);                           /* przed nakladkami - nie tnie panelu */
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
    case GS_NEWS:      draw_news(b); break;
    case GS_SETUP:     draw_setup(b); break;
    case GS_TABLE:     draw_table(b); break;
    case GS_PRESENT:   draw_present(b, bi); break;
    case GS_GATE_PICK: draw_pick(b); break;
    case GS_PODIUM:    draw_podium(b); break;
    default: break;
    }
    if (g_paused) draw_pause(b, 82);
}
