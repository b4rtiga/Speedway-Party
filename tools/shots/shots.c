/* shots.c - zrzuty ekranu z PRAWDZIWEGO kodu gry (ui.c, hud.c, text.c, sprites.c,
 * tractors.c, roost.c + logika biegu) skompilowanego na PC. Obraz skladany jak w AGA:
 * bitplany 8 + paleta, kolory per linia z Coppera (kolor 0 i 100), duszki
 * zdekodowane z bajtow sprzetowych (POS/CTL + dane), HUD 5 bitplanow + gradient.
 * Wynik: tools/shots/out/ (PPM) -> python tools/shots/topng.py -> art/screen_NN.png */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
#include "game.h"
#include "video.h"
#include "ui.h"
#include "hw.h"
#include "hof.h"

extern RGB24 cur_play[256], cur_hud[32], cur_menu[256];
extern UWORD cap_bg[FULL_H], cap_fg[FULL_H];
extern const UWORD *cap_spr[8];
extern volatile ULONG g_frame;
void host_init(void);
UWORD g_grading_progress;

static UBYTE rgb[256][320][3];

static void set12(UBYTE *o, UWORD c) { o[0] = (UBYTE)(((c >> 8) & 15) * 17); o[1] = (UBYTE)(((c >> 4) & 15) * 17); o[2] = (UBYTE)((c & 15) * 17); }
static void set24(UBYTE *o, const RGB24 *c) { o[0] = c->r; o[1] = c->g; o[2] = c->b; }

static UBYTE planar_px(const UBYTE *buf, int stride, int depth, int x, int y)
{
    UBYTE c = 0;
    int p;
    for (p = 0; p < depth; p++)
        if (buf[y * stride + p * ROW_BYTES + (x >> 3)] & (0x80 >> (x & 7))) c |= (UBYTE)(1 << p);
    return c;
}

/* dane duszkow z pliku sa big-endian (jak na Amidze); naglowek POS/CTL pisze C natywnie */
static UWORD be(UWORD w) { return (UWORD)((w >> 8) | (w << 8)); }

static void draw_sprites(void)
{
    int n, W = SPRITE_W / 16;
    for (n = 0; n < 8; n += 2) {
        const UWORD *ev = cap_spr[n], *od = cap_spr[n + 1];
        int hs, vs, ve, x, y, r;
        if (!ev || ev == g_blank_sprite) continue;
        hs = ((ev[0] & 0xFF) << 1) | (ev[W] & 1);
        vs = (ev[0] >> 8) | (((ev[W] >> 2) & 1) << 8);
        ve = (ev[W] >> 8) | (((ev[W] >> 1) & 1) << 8);
        if (!(od[W] & 0x80)) fprintf(stderr, "UWAGA: duszek %d bez bitu ATT\n", n + 1);
        x = hs - 0x80;
        y = vs - DISPLAY_VSTART;
        for (r = 0; r < ve - vs; r++) {
            const UWORD *le = ev + 2 * W + r * 2 * W, *lo = od + 2 * W + r * 2 * W;
            int px;
            for (px = 0; px < SPRITE_W; px++) {
                int wi = px >> 4, bit = 15 - (px & 15), c;
                c = ((be(le[wi]) >> bit) & 1) | (((be(le[W + wi]) >> bit) & 1) << 1)
                  | (((be(lo[wi]) >> bit) & 1) << 2) | (((be(lo[W + wi]) >> bit) & 1) << 3);
                if (c && x + px >= 0 && x + px < 320 && y + r >= 0 && y + r < 256)
                    set24(rgb[y + r][x + px], &cur_play[240 + c]);
            }
        }
    }
}

/* sklada klatke 320x256 RGB w rgb[][] tak jak AGA (bitplany + Copper + HUD + duszki) */
static void compose(BOOL sprites)
{
    int x, y;
    const UBYTE *pb = g_video.play[g_video.back];

    /* menu: cale 256 linii z bitplanow (Copper-lista bez HUD) */
    for (y = 0; y < (g_video.menu ? FULL_H : PLAY_H); y++)
        for (x = 0; x < 320; x++) {
            UBYTE c = planar_px(pb, PLAY_STRIDE, PLAY_DEPTH, x, y);
            if (c == 0) set12(rgb[y][x], cap_bg[y]);
            else if (c == 100) set12(rgb[y][x], cap_fg[y]);
            else set24(rgb[y][x], g_video.menu ? &cur_menu[c] : &cur_play[c]);
        }
    for (y = 0; y < HUD_H && !g_video.menu; y++)
        for (x = 0; x < 320; x++) {
            UBYTE *o = rgb[PLAY_H + y][x];
            UBYTE c;
            int t;
            if (y < 4) { set24(o, &cur_hud[0]); continue; }      /* przerwa: BPLCON0 bez planow */
            c = planar_px(g_video.hud, HUD_STRIDE, HUD_DEPTH, x, y - 4);
            t = (y - 4) * 15 / (HUD_H - 4);
            if (c == 0) set12(o, (UWORD)((1 << 8) | ((1 + t / 3) << 4) | (4 + t / 2)));
            else set24(o, &cur_hud[c]);
        }
    if (sprites) draw_sprites();
}

static void shot(const char *name, BOOL sprites)
{
    char path[128];
    FILE *f;
    compose(sprites);
#ifdef LANG_EN
    snprintf(path, sizeof(path), "tools/shots/out-en/%s.ppm", name);   /* wersja angielska */
#else
    snprintf(path, sizeof(path), "tools/shots/out/%s.ppm", name);
#endif
    f = fopen(path, "wb");
    fprintf(f, "P6\n320 256\n255\n");
    fwrite(rgb, 1, sizeof(rgb), f);
    fclose(f);
}

static void render(void) { track_tick_flush(); ui_invalidate(); roost_erase(); ui_render(); roost_draw(); hud_update(); }

static void bike_sprites(void)
{
    UBYTE i;
    for (i = 0; i < 8; i++) video_set_sprite(i, g_blank_sprite);
    for (i = 0; i < MAX_RIDERS; i++) {
        Bike *b = &g_tour.bikes[i];
        UWORD *ev, *od;
        if (b->state == BIKE_EXCLUDED || ui_panel_covers((WORD)(FIX2INT(b->x) - 10), (WORD)(FIX2INT(b->y) - 10), 20, 20)) continue;   /* jak main.c */
        ev = sprite_bike_frame(i, g_video.back, b->rider, b->sprite_frame, b->skid_frame);   /* jak main.c */
        od = ev + SPRITE_HALF_WORDS;
        sprite_set_pos(ev, FIX2INT(b->x), FIX2INT(b->y), SPRITE_H, FALSE);
        sprite_set_pos(od, FIX2INT(b->x), FIX2INT(b->y), SPRITE_H, TRUE);
        video_set_sprite(b->hw_sprite, ev);
        video_set_sprite((UBYTE)(b->hw_sprite + 1), od);
    }
}

static void begin(UBYTE hn)
{
    UBYTE g;
    g_tour.current_heat = hn;
    g_tour.heat.number = (UBYTE)(hn + 1);
    g_tour.heat.restarts = 0;
    for (g = 0; g < 4; g++) g_tour.heat.gate_rider[g] = g_tour.heat_table[hn][g];
    track_tick_flush();
    start_begin();
}

static void step(void)
{
    start_update();
    if (g_tour.heat.phase == HEAT_TAPE_UP || g_tour.heat.phase == HEAT_RACING) race_update();
}

static void run_heat(UBYTE hn)
{
    begin(hn);
    while (g_tour.heat.phase != HEAT_FINISHED) step();
    g_tour.new_records = records_check_heat();
    tournament_award_points();
    if (tournament_watering_after((UBYTE)(hn + 1))) track_apply_watering(); else track_dry_step();
}

static void nominate_all(void)
{
    UBYTE k;
    tournament_prepare_nominated();
    for (k = 0; k < 4; k++)
        tournament_assign_gate(g_tour.pick_order[k], tournament_ai_choose_gate(g_tour.pick_order[k]));
}

/* ------------------------------------------------------------------------------
 * Tryb "trailer": kolejne klatki 50 fps (surowe RGB 320x256) na stdout, dla ffmpeg.
 * Przebieg jak w grze: plansza tytulowa, ustawienia, program zawodow, bieg 1 od
 * procedury startowej do mety, wyniki biegu i tabela z punktacja. Same boty.
 * ------------------------------------------------------------------------------ */
static long trailer_frames;
static FILE *trailer_audio;              /* TRAILER_AUDIO: okres i glosnosc kanalow silnikow co klatke */
static void close_trailer_audio(void) { fclose(trailer_audio); }

static void tframe(BOOL sprites)
{
    UBYTE i;
    g_frame++;
    roost_erase();
    ui_render();
    roost_draw();
    if (sprites) bike_sprites();
    else for (i = 0; i < 8; i++) video_set_sprite(i, g_blank_sprite);
    hud_update();
    compose(sprites);
    fwrite(rgb, 1, sizeof(rgb), stdout);
    audio_update();                               /* prawdziwa logika dzwieku gry (audio.c) */
    if (trailer_audio) {
        /* rejestry Pauli AUD0/AUD1: PER (+6), VOL (+8) - mikser: tools/trailer_audio.py */
        UWORD r[4];
        r[0] = REG16(0x0A6); r[1] = REG16(0x0A8); r[2] = REG16(0x0B6); r[3] = REG16(0x0B8);
        fwrite(r, sizeof(r), 1, trailer_audio);
    }
    trailer_frames++;
    g_video.front = g_video.back;                 /* podwojny bufor jak na Amidze */
    g_video.back ^= 1;
}

static void thold(int n, BOOL sprites) { while (n-- > 0) tframe(sprites); }
static void tenter(GameState s) { g_tour.state = s; ui_invalidate(); }

static int trailer(void)
{
    static const UBYTE start_wet[3] = { 0, 130, 255 };
    HeatPhase last;
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    host_init();
    fixmath_init();
    sprites_init();
    roost_init();
    tractors_init();
    tournament_defaults();
    tournament_reset();
    track_init();
    ui_init();
    hud_init();
    g_frame = 0;
    {
        const char *ap = getenv("TRAILER_AUDIO");
        if (ap) trailer_audio = fopen(ap, "wb");
        if (trailer_audio) atexit(close_trailer_audio);
    }
    audio_init();
    audio_start();

    tenter(GS_TITLE);  thold(5 * FPS, FALSE);                 /* plansza tytulowa */
    ui_menu_enter(0);
    tenter(GS_MENU);   thold(FPS * 3 / 2, FALSE);             /* TURNIEJ / CREDITS */
    g_tour.start_condition = 0;                               /* suchy tor */
    g_defects_enabled = TRUE;                                 /* w ustawieniach domyslnie TAK */
    tenter(GS_SETUP);  thold(FPS * 3 / 2, FALSE);             /* ustawienia: 1 gracz... */
    g_tour.num_humans = 0;                                    /* ...zmiana na pokaz: same boty */
    tournament_default_names();
    ui_invalidate();   thold(2 * FPS, FALSE);

    g_defects_enabled = FALSE;                                /* bieg w trailerze bez losowego defektu */
    tournament_reset();                                       /* START TURNIEJU */
    g_tour.current_heat = 0;
    track_reset_wear();
    track_random_surface();
    track_set_wetness(start_wet[g_tour.start_condition]);
    tenter(GS_TABLE);  thold(FPS * 7 / 2, FALSE);             /* program zawodow */
    ui_present_start(); tenter(GS_PRESENT);                   /* podprowadzajace na polach */
    while (!ui_present_update()) tframe(FALSE);

    begin(0);                                                 /* bieg 1: start i jazda do mety */
    tenter(GS_HEAT);
    last = g_tour.heat.phase;
    {
        /* TRAILER_LAPS=x: nagranie konczy sie, gdy prowadzacy przejedzie x okrazen (np. 1.5 = polowa 2.) */
        const char *env = getenv("TRAILER_LAPS");
        double laps = env ? atof(env) : 0.0;
        while (g_tour.heat.phase != HEAT_FINISHED && trailer_frames < 20000) {
            UBYTE i;
            LONG lead = 0;
            step();
            if (g_tour.heat.phase != last) { last = g_tour.heat.phase; ui_invalidate(); }
            tframe(TRUE);
            for (i = 0; i < MAX_RIDERS; i++)
                if (g_tour.bikes[i].race_distance > lead) lead = g_tour.bikes[i].race_distance;
            /* dystans liczony od linii: 1. okrazenie zaczyna sie przy 1 * dlugosc */
            if (laps > 0 && lead >= (LONG)((1.0 + laps) * g_tour.track.lap_length)) {
                fprintf(stderr, "trailer: %ld klatek (%.1f s), koniec po %.1f okr.\n", trailer_frames, trailer_frames / 50.0, laps);
                return 0;
            }
        }
    }
    g_tour.new_records = records_check_heat();
    tournament_award_points();
    tenter(GS_HEAT_RESULT); thold(6 * FPS, FALSE);           /* wyniki biegu (jak w grze: bez duszkow) */

    g_tour.current_heat = 1;                                  /* tabela z punktami po biegu 1 */
    tenter(GS_TABLE);  thold(5 * FPS, FALSE);
    fprintf(stderr, "trailer: %ld klatek (%.1f s)\n", trailer_frames, trailer_frames / 50.0);
    return 0;
}

/* Tryb "present": sama prezentacja przed biegiem 1 (podprowadzajace z tablicami), PRESENT_S sekund
 * klatek RGB 320x256 na stdout - podglad kolysania:  shots.exe present | ffmpeg ... */
static int present(void)
{
    const char *env = getenv("PRESENT_S");
    long n = (env ? atol(env) : 4) * FPS;
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    host_init();
    fixmath_init();
    sprites_init();
    tournament_defaults();
    tournament_reset();
    track_init();
    ui_init();
    hud_init();
    g_tour.current_heat = 0;
    ui_present_start(); tenter(GS_PRESENT);
    while (n-- > 0) { ui_present_update(); tframe(FALSE); }
    return 0;
}

/* Tryb "endcard": ostatnia plansza trailera - ilustracja tytulowa z napisem fontem gry
 * (jedna klatka RGB 320x256 na stdout). Napis w 2 liniach: duzy font ma 16 px na znak. */
static int endcard(void)
{
    extern const char *g_endcard[2];
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    host_init();
    fixmath_init();
    sprites_init();
    tournament_defaults();
    track_init();
    ui_init();
    hud_init();
#ifdef LANG_EN
    g_endcard[0] = "SEE YOU";                    /* wersja angielska trailera */
    g_endcard[1] = "AT THE TAPE!";
#else
    g_endcard[0] = "Z ŻUŻLOWYM";                 /* napisy w kodzie gry sa w UTF-8 (text.c) */
    g_endcard[1] = "POZDROWIENIEM!";
#endif
    g_frame = 16;
    tenter(GS_TITLE);
    tframe(FALSE);
    return 0;
}

static int screenshots(void)
{
    static const char test_name[] = "MAREK \x83" "OPATA";   /* test polskiej litery Ł */
    int i;

    host_init();
    fixmath_init();
    sprites_init();
    roost_init();
    tractors_init();
    tournament_defaults();
    g_tour.num_humans = 0;            /* boty - zeby biegi same sie rozgrywaly */
    strcpy(g_tour.riders[0].name, test_name);
    tournament_reset();
    track_init();
    ui_init();
    hud_init();
    g_frame = 16;                     /* migajace napisy widoczne */
    {
        /* przykladowe rekordy graczy (boty do tabel nie wchodza) */
        HofTrack *h = &g_hof[0];
        hof_clear();
        hof_insert(h->heat, 1160, "ADA    ", 0); hof_insert(h->heat, 1175, "KUBA   ", 0); hof_insert(h->heat, 1190, "OLA    ", 0);
        hof_insert(h->lap, 268, "KUBA   ", 0);   hof_insert(h->lap, 271, "ADA    ", 0);   hof_insert(h->lap, 275, "OLA    ", 0);
    }

    g_tour.state = GS_TITLE;   render(); shot("01_title", FALSE);
    ui_menu_enter(0);
    g_tour.state = GS_MENU;    render(); shot("18_menu", FALSE);
    g_tour.state = GS_CREDITS; render(); shot("19_credits", FALSE);
    g_tour.state = GS_HELP;
    ui_help_enter(0);          render(); shot("20_instrukcja_1", FALSE);
    ui_help_enter(1);          render(); shot("21_instrukcja_2", FALSE);
    ui_help_enter(2);          render(); shot("22_instrukcja_3", FALSE);
    g_tour.num_humans = 2;
    tournament_default_names();       /* GRACZ 2 zamiast CPU 2 */
    g_defects_enabled = TRUE;            /* w ustawieniach domyslnie TAK */
    g_tour.state = GS_SETUP;   render(); shot("02_setup", FALSE);
    ui_options_enter();
    g_tour.state = GS_OPTIONS; render(); shot("29_opcje", FALSE);
    g_defects_enabled = FALSE;
    g_tour.num_humans = 0;

    for (i = 0; i < 3; i++) run_heat((UBYTE)i);
    /* suchy tor po 3 biegach: widac wyjezdzone sciezki */
    begin(3);
    g_tour.state = GS_HEAT;
    while (g_tour.heat.phase != HEAT_RACING) step();
    for (i = 0; i < 330; i++) step();
    bike_sprites(); render(); shot("13_sciezki", TRUE);
    {
        /* pauza w biegu (ESC): kilka klatek jak w petli gry - zmazanie szprycy, nakladka,
         * rysowanie szprycy, zamiana buforow; panel pauzy nie moze miec dziur ani drobin */
        UBYTE k, b = 0;
        WORD n;
        /* motocykl w lewym luku przy krawedzi panelu nakladek (x 48), ze szpryca */
        for (n = 0; n < 3000 && g_tour.heat.phase == HEAT_RACING; n++) {
            for (b = 0; b < MAX_RIDERS; b++) {
                const Bike *bk = &g_tour.bikes[b];
                WORD x = FIX2INT(bk->x), y = FIX2INT(bk->y);
                if (x > 44 && x < 62 && y > 50 && y < 120 && bk->drift > 12) break;
            }
            if (b < MAX_RIDERS) break;
            step();
            roost_erase(); ui_render(); roost_draw();
            g_video.front = g_video.back; g_video.back ^= 1;
        }
        for (k = 0; k < 6; k++) {
            if (k == 2) { g_paused = TRUE; ui_invalidate(); }
            roost_erase(); ui_render(); roost_draw();
            if (k < 5) { g_video.front = g_video.back; g_video.back ^= 1; }
        }
        bike_sprites(); shot("test_pauza", TRUE);          /* kontrola: tylko tools/shots/out */
        g_paused = FALSE;
        ui_invalidate();
    }
    while (g_tour.heat.phase != HEAT_FINISHED) step();
    tournament_award_points();
    if (tournament_watering_after(4)) track_apply_watering();
    g_tour.current_heat = 4;
    g_tour.state = GS_TABLE;   render(); shot("03_table", FALSE);
    ui_present_start(); g_tour.state = GS_PRESENT;
    while (!ui_present_update()) ;
    render(); shot("17_prezentacja", FALSE);

    begin(4);                         /* bieg 5: czerwone swiatlo pod tasma */
    g_tour.state = GS_HEAT;
    while (g_tour.heat.phase != HEAT_RED_LIGHT) step();
    bike_sprites(); render(); hud_update(); shot("04_gate_red", TRUE);

    while (g_tour.heat.phase != HEAT_TAPE_UP) step();   /* taśma w trakcie odlotu */
    step(); step(); step();
    bike_sprites(); render(); hud_update();          /* tasma w locie (bez zrzutu - nieuzywany w README) */
    while (g_tour.heat.phase != HEAT_RACING) step();
    for (i = 0; i < 700; i++) step();
    bike_sprites(); render(); hud_update(); shot("05_racing", TRUE);

    {
        HeatPhase ph = g_tour.heat.phase;
        g_tour.heat.phase = HEAT_FALSE_START;
        g_tour.heat.false_starter = 1;
        g_tour.heat.restarts = 1;
        g_tour.heat.phase_timer = 8;
        render(); hud_update(); shot("06_false_start", FALSE);
        g_tour.heat.phase = ph;
    }

    while (g_tour.heat.phase != HEAT_FINISHED) step();
    g_tour.new_records = records_check_heat();
    tournament_award_points();
    g_frame = 0;                      /* faza migania: napis o rekordzie */
    g_tour.state = GS_HEAT_RESULT; render(); hud_update(); shot("07_result", FALSE);
    g_frame = 16;

    {
        /* wpis imienia: zawodnik 0 jako gracz z 2. czasem biegu i rekordem okrazenia */
        HofTrack *h = &g_hof[g_tour.track.type % NUM_TRACKS];
        char nm[HOF_NAME + 1];
        hof_default_name(nm, g_tour.riders[0].name);
        hof_insert(h->heat, (UWORD)(h->heat[0].frames ? h->heat[0].frames + 7 : 3200), nm, 1);
        hof_insert(h->lap, (UWORD)(h->lap[0].frames ? h->lap[0].frames - 3 : 640), nm, 1);
        ui_name_entry_enter(0);
        g_tour.state = GS_NAME_ENTRY; render(); shot("24_imie", FALSE);
        hof_set_name(0, "BARTEK ");
        g_tour.state = GS_RECORDS;  render(); shot("25_rekordy", FALSE);
    }

    g_tour.state = GS_TRACK_GRADING;
    roost_reset();
    tractors_begin();
    for (i = 0; i < 140; i++) { tractors_update(); roost_update(FALSE); }
    g_grading_progress = 110;
    video_blend_track_palette(140);
    render(); tractors_render(); shot("08_grading", TRUE);
    video_blend_track_palette(g_tour.track.wetness);

    for (i = 5; i < FIXED_HEATS; i++) run_heat((UBYTE)i);
    g_tour.current_heat = FIXED_HEATS;
    tournament_prepare_nominated();
    tournament_assign_gate(g_tour.pick_order[0], tournament_ai_choose_gate(g_tour.pick_order[0]));
    g_tour.state = GS_GATE_PICK; render();

    for (i = FIXED_HEATS; i < NUM_HEATS; i++) {
        g_tour.current_heat = (UBYTE)i;
        nominate_all();
        run_heat((UBYTE)i);
    }
    /* remis o 2. miejsce -> program biegu dodatkowego */
    {
        UBYTE order[MAX_RIDERS], k;
        tournament_sort(order);
        g_tour.riders[order[2]].points = g_tour.riders[order[1]].points;
        for (k = 0; k < MAX_RIDERS; k++) g_tour.runoff_rank[k] = RUNOFF_NONE;
        if (tournament_prepare_runoff()) {
            g_tour.current_heat = RUNOFF_HEAT;
            g_tour.state = GS_TABLE; render();
            begin(RUNOFF_HEAT);
            g_tour.state = GS_HEAT;
            while (g_tour.heat.phase != HEAT_FINISHED) step();
            tournament_award_points();
            g_tour.state = GS_HEAT_RESULT; render();
        }
    }
    g_tour.state = GS_PODIUM; render(); shot("10_podium", FALSE);
    news_build();
    g_tour.state = GS_NEWS;   render(); shot("23_gazeta", FALSE);
    g_tour.state = GS_MENU;   render();                         /* kolory linii menu z powrotem */

    /* awaria maszyny startowej: start z flagi narodowej */
    track_select(0);
    begin(0);
    g_tour.heat.emergency = 1;
    g_tour.state = GS_HEAT;
    while (g_tour.heat.phase != HEAT_RED_LIGHT) step();
    bike_sprites(); render(); hud_update(); shot("16_awaria", TRUE);

    /* defekt: zawodnik z pola C krztusi sie i dymi, potem staje; wynik z "D" */
    begin(1);
    g_tour.state = GS_HEAT;
    while (g_tour.heat.phase != HEAT_RACING) step();
    for (i = 0; i < 300; i++) step();
    g_tour.bikes[2].defect_frame = (UWORD)(g_tour.heat.race_frames + 1);
    for (i = 0; i < 40; i++) step();
    bike_sprites(); render(); hud_update(); shot("27_defekt", TRUE);
    while (g_tour.heat.phase != HEAT_FINISHED) step();
    tournament_award_points();
    g_tour.state = GS_HEAT_RESULT; render(); hud_update(); shot("test_defekt_wynik", FALSE);   /* kontrola: tylko tools/shots/out */

    printf("zrzuty: tools/shots/out/\n");
    return 0;
}

int main(int argc, char **argv)
{
    g_defects_enabled = FALSE;           /* zrzuty i trailer powtarzalne - bez losowych defektow */
    if (argc > 1 && !strcmp(argv[1], "trailer")) return trailer();
    if (argc > 1 && !strcmp(argv[1], "endcard")) return endcard();
    if (argc > 1 && !strcmp(argv[1], "present")) return present();
    return screenshots();
}
