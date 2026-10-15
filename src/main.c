/* main.c - inicjalizacja, przejecie sprzetu i glowna petla 50 Hz.
 *
 * Jedna iteracja = jedna ramka PAL:
 *   1. system_wait_vbl()  - synchronizacja; lista Coppera z poprzedniej
 *                           iteracji jest juz na ekranie, druga jest wolna
 *   2. input_update()     - joysticki, adapter, klawiatura
 *   3. update_state()     - logika: menu / start / wyscig / turniej
 *   4. ui_render()        - ekran lub tor + nakladki + tasma do bufora "back"
 *      render_sprites()   - wskazniki duszkow w liscie "back"
 *   5. video_swap()       - COP1LC = lista back -> widoczna od nastepnej ramki
 *
 * Przeplyw turnieju:
 *   TITLE -> SETUP -> TABLE -> HEAT -> HEAT_RESULT -> [TRACK_GRADING] -> TABLE ...
 *   przed biegami 14 i 15: GATE_PICK -> TABLE -> HEAT;  po 15: PODIUM -> SETUP
 */
#include <exec/memory.h>
#include <exec/tasks.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "game.h"
#include "video.h"
#include "system.h"
#include "blit.h"
#include "hw.h"
#include "ui.h"

#ifdef AUTOPLAY
#define TITLE_FRAMES    (7 * FPS)       /* test bez klawiatury: plansza tytulowa sama przechodzi dalej */
#endif
#define RESULT_FRAMES   (6 * FPS)
#define RESULT_MIN      FPS
#define GRADING_FRAMES  (8 * FPS)
#define GRADING_SKIP    (2 * FPS)      /* FIRE po 2 s pomija animacje */

UWORD g_grading_progress;

#ifdef SHOW_LAG
UWORD g_prof[4];                         /* maks. liczba linii z ostatnich 32 klatek */
void prof_add(UBYTE i, UWORD a, UWORD b)
{
    static UWORD peak[4];
    static UBYTE n;
    UWORD d = (UWORD)(b >= a ? b - a : b + 313 - a);
    if (d > peak[i]) peak[i] = d;
    if (i == 3 && ++n >= 32) {
        UBYTE k;
        for (k = 0; k < 4; k++) { g_prof[k] = peak[k]; peak[k] = 0; }
        n = 0;
    }
}
#endif
static UWORD state_timer;
static UBYTE grading_start_wet;
static BOOL  surface_pending;           /* losowanie nawierzchni czeka na ekran programu */
static HeatPhase last_phase;

/* ---------------------------- render --------------------------------- */

static void render_sprites(void)
{
    UBYTE i;
    if (g_tour.state == GS_TRACK_GRADING) { tractors_render(); return; }
    if (g_tour.state != GS_HEAT) {
        for (i = 0; i < 8; i++) video_set_sprite(i, g_blank_sprite);
        return;
    }
    for (i = 0; i < MAX_RIDERS; i++) {
        Bike *b = &g_tour.bikes[i];
        UWORD *even, *odd;
        if (b->state == BIKE_EXCLUDED) {
            video_set_sprite(b->hw_sprite, g_blank_sprite);
            video_set_sprite(b->hw_sprite + 1, g_blank_sprite);
            continue;
        }
        /* kolor kasku = zawodnik (staly w turnieju), nie pole startowe; wlasna kopia klatki
         * na bufor "back" - pozycja zmienia sie razem z obrazem, x z dokladnoscia 1/4 px */
        even = sprite_bike_frame(i, g_video.back, b->rider, b->sprite_frame, b->skid_frame);
        odd  = even + SPRITE_HALF_WORDS;
        sprite_set_pos_q(even, (b->x + (1L << 13)) >> 14, FIX2INT(b->y + FIX_ONE / 2), SPRITE_H, FALSE);
        sprite_set_pos_q(odd,  (b->x + (1L << 13)) >> 14, FIX2INT(b->y + FIX_ONE / 2), SPRITE_H, TRUE);
        video_set_sprite(b->hw_sprite, even);
        video_set_sprite(b->hw_sprite + 1, odd);
    }
}

/* ---------------------------- stany ---------------------------------- */

static void enter(GameState s)
{
    g_tour.state = s;
    state_timer = 0;
    if (s == GS_SETUP) {
        ui_setup_enter();                /* kursor na START TURNIEJU */
        /* stan toru losowy przy kazdym wejsciu do ustawien (gracz moze go zmienic);
         * do ziarna dokladamy pozycje wiazki i licznik klatek - zalezne od chwili nacisniecia */
        g_rng ^= ((ULONG)g_frame << 16) ^ hw_vpos();
        if (!g_rng) g_rng = 0x2026B4u;
        g_tour.start_condition = (UBYTE)rng_range(0, 2);
    }
    while (key_get() != 0xFF) ;      /* stare wcisniecia nie przechodza do nowego ekranu */
    ui_invalidate();
}

static void begin_heat(void)
{
    Heat *h = &g_tour.heat;
    UBYTE g;
    h->number = (UBYTE)(g_tour.current_heat + 1);
    h->restarts = 0;
    for (g = 0; g < 4; g++) h->gate_rider[g] = g_tour.heat_table[g_tour.current_heat][g];
    track_tick_flush();                 /* reszta przemalowan toru przed startem */
    start_begin();
    g_lag = 0;                          /* diagnostyka: licz zgubione klatki od startu biegu */
    last_phase = h->phase;
    enter(GS_HEAT);
}

static void read_bike_inputs(void)
{
    UBYTE i;
    for (i = 0; i < MAX_RIDERS; i++) {
        Bike *b = &g_tour.bikes[i];
        Rider *r = &g_tour.riders[b->rider];
        if (r->control == CTRL_HUMAN) b->input = (UBYTE)(input_read(r->input) & BTN_DIRS);
    }
}

static void update_heat(void)
{
    Heat *h = &g_tour.heat;
    read_bike_inputs();
    start_update();
    if (h->phase == HEAT_TAPE_UP || h->phase == HEAT_RACING)
        race_update();
    if (h->phase != last_phase) {     /* np. TASMA! / powtorka -> nakladka sie zmienia */
        last_phase = h->phase;
        ui_invalidate();
    }
    if (h->phase == HEAT_FINISHED) {
        g_tour.new_records = records_check_heat();
        tournament_award_points();
        enter(GS_HEAT_RESULT);
    }
}

static void next_heat(void)
{
    g_tour.current_heat++;
    if (g_tour.current_heat >= NUM_HEATS) {
        /* remis o podium -> bieg dodatkowy (moze byc kilka po kolei), potem podium */
        if (tournament_prepare_runoff()) { g_tour.current_heat = RUNOFF_HEAT; enter(GS_TABLE); }
        else { news_build(); enter(GS_PODIUM); }        /* turniej rozstrzygniety: migawka do gazety */
        return;
    }
    if (g_tour.current_heat >= FIXED_HEATS) {
        tournament_prepare_nominated();
        enter(GS_GATE_PICK);
        return;
    }
    enter(GS_TABLE);
}

/* po wyniku biegu: polewanie toru albo nastepny bieg */
static void after_result(void)
{
#ifdef TEST_HOF
    if (g_tour.current_heat >= 1) { enter(GS_QUIT); return; }   /* test: po 2 biegach wyjscie i zapis */
#endif
    if (tournament_watering_after(g_tour.current_heat + 1)) {
        grading_start_wet = g_tour.track.wetness;
        g_grading_progress = 0;
        roost_reset();
        tractors_begin();
        enter(GS_TRACK_GRADING);
    } else {
        track_dry_step();
        next_heat();
    }
}

/* Gracze, ktorzy wjechali do TOP 5 rekordow toru, po kolei wpisuja imie. */
static UBYTE entry_from;
static void next_name_entry(void)
{
    UBYTE r;
    for (r = entry_from; r < MAX_RIDERS; r++)
        if (RECORDS_HUMAN(r) && records_needs_name(r)) {
            entry_from = (UBYTE)(r + 1);
            ui_name_entry_enter(r);
            enter(GS_NAME_ENTRY);
            return;
        }
    after_result();
}

static void name_entry_done(void)
{
#ifdef TEST_HOF
    records_set_name((UBYTE)(entry_from - 1), "TEST   ");
#else
    records_set_name((UBYTE)(entry_from - 1), ui_name_entry());
#endif
    next_name_entry();
}

/* JEDZIEMY! zatwierdzone joystickiem w porcie 0 -> GRACZ 1 na JOY 0, z klawiatury -> STRZALKI,
 * joystickiem w porcie 1 -> JOY 1. Gracz, ktory mial ten port, dostaje dotychczasowy port gracza 1. */
static void player1_port(InputPort p)
{
    UBYTE i;
    InputPort old = g_tour.riders[0].input;
    if (old == p) return;
    for (i = 1; i < MAX_RIDERS; i++)
        if (g_tour.riders[i].input == p) g_tour.riders[i].input = old;
    g_tour.riders[0].input = p;
}

BOOL g_paused, g_pause_quit;

/* ESC: pauza z pytaniem. W turnieju - "wyjsc do menu glownego?", w menu glownym -
 * "zakonczyc gre?". Drugie ESC = tak, FIRE/Return = graj dalej. */
static BOOL handle_pause(void)
{
    static BOOL esc_prev;
    BOOL esc = (BOOL)(input_esc() && !ui_editing());      /* ESC albo drugi przycisk joysticka */
    BOOL edge = (BOOL)(esc && !esc_prev);
    esc_prev = esc;
    if (g_paused) {
        if (edge) {
            g_paused = FALSE;
            if (g_pause_quit) enter(GS_QUIT);
            else enter(GS_SETUP);
        } else if (ui_confirm()) {
            g_paused = FALSE;
            while (key_get() != 0xFF) ;  /* Enter z pauzy nie przechodzi do ekranu pod spodem */
            ui_invalidate();
        }
        return TRUE;
    }
    if (edge && g_tour.state == GS_SETUP) {          /* ESC w ustawieniach: powrot do ekranu wyboru */
        ui_menu_enter(0);
        enter(GS_MENU);
        return TRUE;
    }
    if (edge && g_tour.state == GS_NAME_ENTRY) {       /* ESC: zostaje imie z ekranu */
        name_entry_done();
        return TRUE;
    }
    if (edge && (g_tour.state == GS_CREDITS || g_tour.state == GS_HELP || g_tour.state == GS_RECORDS)) {
        /* ESC: powrot do wyboru */
        ui_menu_enter((UBYTE)(g_tour.state == GS_HELP ? 1 : g_tour.state == GS_RECORDS ? 2 : 3));
        enter(GS_MENU);
        return TRUE;
    }
    if (edge) {
        g_paused = TRUE;
        g_pause_quit = (BOOL)(g_tour.state == GS_TITLE || g_tour.state == GS_MENU);
        ui_invalidate();
        return TRUE;
    }
    return FALSE;
}

static void update_state(void)
{
    if (handle_pause()) return;          /* pauza: logika gry zatrzymana */
    state_timer++;
    /* poza biegiem tor przemalowuje sie w tle (np. po losowaniu nawierzchni) */
    if (g_tour.state == GS_TABLE || g_tour.state == GS_PRESENT || g_tour.state == GS_GATE_PICK ||
        g_tour.state == GS_HEAT_RESULT || g_tour.state == GS_TRACK_GRADING) track_tick(16);   /* 2 komorki */

    switch (g_tour.state) {
    case GS_TITLE:                       /* plansza tytulowa: czeka na FIRE / Return */
#ifdef AUTOPLAY
#ifdef TEST_HOF
        if (state_timer > 2 * FPS) { enter(GS_RECORDS); break; }
#endif
        if (state_timer > TITLE_FRAMES) { enter(GS_SETUP); break; }
#endif
        if (ui_title_update() || (state_timer > FPS / 2 && ui_confirm())) { ui_menu_enter(0); enter(GS_MENU); }
        break;

    case GS_MENU:                        /* JEDZIEMY! -> ustawienia, CREDITS -> plansza tworcow */
        if (state_timer > FPS / 4) {
            UBYTE pick = ui_menu_update();
            if (pick == 1) { player1_port(input_menu_fire_port()); enter(GS_SETUP); }
            else if (pick == 2) { ui_help_enter(0); enter(GS_HELP); }
            else if (pick == 3) enter(GS_RECORDS);
            else if (pick == 4) enter(GS_CREDITS);
        }
        break;

    case GS_CREDITS:                     /* bez limitu czasu: FIRE / Return (albo ESC) wraca */
        if (state_timer > FPS / 4 && ui_confirm()) { ui_menu_enter(3); enter(GS_MENU); }
        break;

    case GS_RECORDS:                     /* tabela rekordow: FIRE / Return (albo ESC) wraca */
#ifdef TEST_HOF
        if (state_timer > 6 * FPS) { enter(GS_SETUP); break; }
#endif
        if (state_timer > FPS / 4 && ui_confirm()) { ui_menu_enter(2); enter(GS_MENU); }
        break;

    case GS_NAME_ENTRY:                    /* wpis imienia; swiezy FIRE po chwili */
#ifdef TEST_HOF
        if (state_timer > 4 * FPS) { name_entry_done(); break; }
#endif
        if (state_timer > FPS / 2) { if (ui_name_entry_update()) name_entry_done(); }
        else while (key_get() != 0xFF) ;  /* wcisniecia sprzed ekranu nie wpisuja znakow */
        break;

    case GS_HELP:                        /* LEWO/PRAWO - strony, FIRE / Return (albo ESC) wraca */
        if (state_timer > FPS / 4 && ui_help_update()) { ui_menu_enter(1); enter(GS_MENU); }
        break;

    case GS_SETUP:
#ifdef AUTOPLAY
        /* test bez klawiatury: po 3 s pokaz (same boty) i start turnieju */
        if (state_timer == 3 * FPS) { g_tour.num_humans = 0; tournament_default_names(); ui_invalidate(); }
        if (state_timer > 4 * FPS) {        /* bez reakcji na klawisze - nic nie zepsuje testu */
#else
        if (ui_setup_update()) {
#endif
            tournament_reset();
            g_tour.current_heat = 0;
            {
                static const UBYTE start_wet[3] = { 0, 130, 255 };   /* suchy / wilgotny / mokry */
                track_set_wetness(start_wet[g_tour.start_condition]);
            }
            surface_pending = TRUE;         /* nowa nawierzchnia - na programie zawodow, po muzyce */
            enter(GS_TABLE);
        }
        break;

    case GS_TABLE:
        if (surface_pending && state_timer >= 15) {
            /* kazdy turniej na innym torze; liczone dopiero po wyciszeniu muzyki z menu
             * (12 klatek) - dluzsza chwila liczenia nie przytnie dzwieku */
            track_reset_wear();
            track_random_surface();
            surface_pending = FALSE;
        }
#ifdef AUTOPLAY
        if (state_timer > 3 * FPS) { ui_present_start(); enter(GS_PRESENT); break; }
#endif
        if (state_timer > FPS / 2 && ui_confirm()) { ui_present_start(); enter(GS_PRESENT); }
        break;

    case GS_PRESENT:                     /* podprowadzajace na polach: 3 s albo FIRE */
        if (ui_present_update() || (state_timer > FPS / 4 && ui_confirm())) begin_heat();
        break;

    case GS_HEAT:
        update_heat();
        break;

    case GS_HEAT_RESULT:
        if (state_timer >= RESULT_FRAMES || (state_timer > RESULT_MIN && ui_confirm())) {
            entry_from = 0;
            next_name_entry();
        }
        break;

    case GS_TRACK_GRADING:
        /* traktory jada, beczka zrasza, a tor ciemnieje plynnie - sama zamiana palety w Copperze */
        tractors_update();
        roost_update(FALSE);
        if ((state_timer & 7) == 0) {
            UWORD wet = (UWORD)(grading_start_wet + (UWORD)(255 - grading_start_wet) * state_timer / GRADING_FRAMES);
            video_blend_track_palette_lazy((UBYTE)(wet > 255 ? 255 : wet));
            g_grading_progress = (UWORD)(255UL * state_timer / GRADING_FRAMES);
            ui_invalidate();
        }
        if (state_timer >= GRADING_FRAMES || (state_timer > GRADING_SKIP && ui_confirm())) {
            track_apply_watering();
            roost_reset();
            next_heat();
        }
        break;

    case GS_GATE_PICK:
        ui_pick_update();
        if (g_tour.pick_index >= MAX_RIDERS && state_timer > FPS) enter(GS_TABLE);
        break;

    case GS_PODIUM:
        if (state_timer > 2 * FPS && ui_confirm()) enter(GS_NEWS);
        break;

    case GS_NEWS:                        /* gazeta: czeka na swiezy FIRE / Return */
        if (state_timer > FPS / 2 && ui_confirm()) enter(GS_SETUP);
        break;

    default:
        break;
    }
}

/* Gra na wlasnym stosie 32 KB: program uruchamiany z s/startup-sequence dostaje domyslnie
 * tylko 4 KB, a przepelnienie stosu po cichu nadpisuje dane gry (losowe usterki).
 * StackSwap (exec, Kickstart 2.0+): zmienne main musza byc statyczne - po zamianie
 * stosu lokalne adresowane wzgledem SP przestaja byc dostepne. */
#define GAME_STACK 32768UL
static struct StackSwapStruct game_stack;
static int game_rc;
static int game_main(void) __attribute__((noinline));

int main(void)
{
    APTR mem = AllocMem(GAME_STACK, MEMF_ANY);
    if (!mem) return game_main();                   /* bez pamieci: na stosie z Shella */
    game_stack.stk_Lower = mem;
    game_stack.stk_Upper = (ULONG)mem + GAME_STACK;
    game_stack.stk_Pointer = (APTR)game_stack.stk_Upper;
    StackSwap(&game_stack);
    game_rc = game_main();
    StackSwap(&game_stack);
    FreeMem(game_stack.stk_Lower, GAME_STACK);
    return game_rc;
}

static int game_main(void)
{
    int rc = 0;

    fixmath_init();
    {
        /* ziarno z zegara systemowego (jesli komputer ma zegar z bateria); dalej mieszane
         * w enter() pozycja wiazki przy wejsciu do ustawien */
        struct DateStamp ds;
        DateStamp(&ds);
        g_rng ^= (ULONG)ds.ds_Tick * 2654435761UL ^ (ULONG)ds.ds_Minute << 12 ^ (ULONG)ds.ds_Days;
        if (!g_rng) g_rng = 0x2026B4u;
    }
    if (!video_init()) { rc = 20; goto out; }
    /* z dyskietki wczytywanie trwa kilkadziesiat sekund: najpierw plansza tytulowa z napisem
     * WCZYTYWANIE (system dziala dalej), potem reszta danych */
    ui_init_title();
    if (system_show_early()) ui_loading_screen();
    if (!sprites_init() || !roost_init() || !tractors_init() || !audio_init()) { rc = 20; goto out; }

    tournament_defaults();
    tournament_reset();
    records_load();
    if (!track_init()) { rc = 20; goto out; }   /* wszystkie tory (przed przejeciem) */
    ui_init();
#ifdef SHOW_LAG
    { extern ULONG g_chip_free; g_chip_free = AvailMem(MEMF_CHIP); }   /* po wczytaniu wszystkiego */
#endif
    hud_init();
    {
        /* obie listy Coppera z pustymi duszkami; bez zapisu COP1LC - OS jeszcze dziala */
        UBYTE i, k;
        for (k = 0; k < 2; k++) {
            for (i = 0; i < 8; i++) video_set_sprite(i, g_blank_sprite);
            g_video.back ^= 1; g_video.front ^= 1;
        }
    }

    if (!system_takeover()) { rc = 20; goto out; }
    input_init();
    video_start();
    audio_start();
    enter(GS_TITLE);

    while (g_tour.state != GS_QUIT) {
#ifdef SHOW_LAG
        UWORD v0, v1, v2, v3;
#endif
#ifdef SHOW_START
        BOOL start_win;
#endif
        system_wait_vbl();
#ifdef SHOW_LAG
        v0 = hw_vpos();
#endif
#ifdef SHOW_START
        {
            /* okno startu (zielone + 20 klatek): kolumna 0 = zgubione klatki w oknie */
            extern UWORD g_prof[4];
            static ULONG last_lag;
            const Heat *hh = &g_tour.heat;
            UWORD lost;
            if (g_lag < last_lag) last_lag = 0;          /* licznik wyzerowany na nowy bieg */
            lost = (UWORD)(g_lag - last_lag);
            last_lag = g_lag;
            start_win = (BOOL)(g_tour.state == GS_HEAT && (hh->phase == HEAT_TAPE_UP ||
                               (hh->phase == HEAT_RACING && hh->race_frames < 20)));
            if (start_win) g_prof[0] = (UWORD)(g_prof[0] + lost);
        }
#endif
        input_update();
        update_state();
        video_palette_tick();           /* mieszanie palety toru po kawalku (schniecie, polewanie) */
#ifdef SHOW_LAG
        v1 = hw_vpos();
#endif

        roost_erase();                  /* stare drobiny z tego bufora (sprzed 2 klatek) */
        ui_render();
        roost_draw();                   /* aktualne drobiny - Blitter, cookie-cut */
#ifdef SHOW_LAG
        v2 = hw_vpos();
#endif
        render_sprites();
        hud_update();
        audio_update();
#ifdef SHOW_LAG
        v3 = hw_vpos();
#endif
        hw_wait_blit();                 /* bufor musi byc gotowy, zanim go pokazemy */
#ifdef SHOW_LAG
        /* profil (linie wiazki, 313 na klatke PAL): logika, rysowanie toru, duszki+HUD+dzwiek,
         * czekanie na Blitter */
        /* kolumny 0-2: czesci logiki biegu (race.c), 3: cala logika (wejscie + update_state) */
        (void)v2; (void)v3;
#ifndef SHOW_START
        prof_add(3, v0, v1);
#else
        if (start_win) {                 /* szczyty w oknie startu: logika, rysowanie, duszki+HUD */
            extern UWORD g_prof[4];
            UWORD d1 = (UWORD)(v1 >= v0 ? v1 - v0 : v1 + 313 - v0), d2 = (UWORD)(v2 >= v1 ? v2 - v1 : v2 + 313 - v1),
                  d3 = (UWORD)(v3 >= v2 ? v3 - v2 : v3 + 313 - v2);
            if (d1 > g_prof[1]) g_prof[1] = d1;
            if (d2 > g_prof[2]) g_prof[2] = d2;
            if (d3 > g_prof[3]) g_prof[3] = d3;
        }
#endif
#endif
        video_swap();
    }

    audio_stop();
    system_restore();
    records_save();                     /* dysk dostepny dopiero po oddaniu systemu */
out:
    system_hide_early();                /* blad przy wczytywaniu: z powrotem ekran systemu */
    audio_free();
    ui_free();
    tractors_free();
    roost_free();
    sprites_free();
    track_free();
    video_free();
    return rc;
}
