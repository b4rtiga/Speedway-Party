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
#include "game.h"
#include "video.h"
#include "system.h"
#include "blit.h"
#include "hw.h"
#include "ui.h"

#define RESULT_FRAMES   (6 * FPS)
#define RESULT_MIN      FPS
#define GRADING_FRAMES  (8 * FPS)
#define GRADING_SKIP    (2 * FPS)      /* FIRE po 2 s pomija animacje */

UWORD g_grading_progress;
static UWORD state_timer;
static UBYTE grading_start_wet;
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
        even = sprite_frame_ptr(b->gate, b->sprite_frame, b->skid_frame, 0);
        odd  = sprite_frame_ptr(b->gate, b->sprite_frame, b->skid_frame, 1);
        sprite_set_pos(even, FIX2INT(b->x), FIX2INT(b->y), SPRITE_H, FALSE);
        sprite_set_pos(odd,  FIX2INT(b->x), FIX2INT(b->y), SPRITE_H, TRUE);
        video_set_sprite(b->hw_sprite, even);
        video_set_sprite(b->hw_sprite + 1, odd);
    }
}

/* ---------------------------- stany ---------------------------------- */

static void enter(GameState s)
{
    g_tour.state = s;
    state_timer = 0;
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
    start_begin();
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
        tournament_award_points();
        enter(GS_HEAT_RESULT);
    }
}

static void next_heat(void)
{
    g_tour.current_heat++;
    if (g_tour.current_heat >= NUM_HEATS) { enter(GS_PODIUM); return; }
    if (g_tour.current_heat >= FIXED_HEATS) {
        tournament_prepare_nominated();
        enter(GS_GATE_PICK);
        return;
    }
    enter(GS_TABLE);
}

static void update_state(void)
{
    state_timer++;
    if (key_down(KEY_ESC) && !ui_editing()) { enter(GS_QUIT); return; }

    switch (g_tour.state) {
    case GS_TITLE:
        if (ui_title_update()) enter(GS_SETUP);
        break;

    case GS_SETUP:
        if (ui_setup_update()) {
            tournament_reset();
            g_tour.current_heat = 0;
            g_tour.track.wetness = 0;
            track_dry_step();            /* przeliczenie gripu i palety dla suchego toru */
            enter(GS_TABLE);
        }
        break;

    case GS_TABLE:
        if (state_timer > FPS / 2 && ui_confirm()) begin_heat();
        break;

    case GS_HEAT:
        update_heat();
        break;

    case GS_HEAT_RESULT:
        if (state_timer >= RESULT_FRAMES || (state_timer > RESULT_MIN && ui_confirm())) {
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
        break;

    case GS_TRACK_GRADING:
        /* traktory jada, beczka zrasza, a tor ciemnieje plynnie - sama zamiana palety w Copperze */
        tractors_update();
        roost_update(FALSE);
        if ((state_timer & 7) == 0) {
            UWORD wet = (UWORD)(grading_start_wet + (UWORD)(255 - grading_start_wet) * state_timer / GRADING_FRAMES);
            video_blend_track_palette((UBYTE)(wet > 255 ? 255 : wet));
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
        if (state_timer > 2 * FPS && ui_confirm()) enter(GS_SETUP);
        break;

    default:
        break;
    }
}

int main(void)
{
    int rc = 0;

    fixmath_init();
    if (!video_init() || !sprites_init() || !roost_init() || !tractors_init() || !audio_init()) { rc = 20; goto out; }

    tournament_defaults();
    tournament_reset();
    track_init();                       /* palety + renderowanie tla (przed przejeciem) */
    ui_init();
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
        system_wait_vbl();
        input_update();
        update_state();

        roost_erase();                  /* stare drobiny z tego bufora (sprzed 2 klatek) */
        ui_render();
        roost_draw();                   /* aktualne drobiny - Blitter, cookie-cut */
        render_sprites();
        hud_update();
        audio_update();
        hw_wait_blit();                 /* bufor musi byc gotowy, zanim go pokazemy */
        video_swap();
    }

    audio_stop();
    system_restore();
out:
    audio_free();
    tractors_free();
    roost_free();
    sprites_free();
    video_free();
    return rc;
}
