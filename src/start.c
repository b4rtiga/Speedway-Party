/* start.c - procedura startowa spod tasmy i pomiar refleksu.
 *
 *  HEAT_PRE        1 s: zawodnicy ustawiaja sie pod tasma
 *  HEAT_READY      czekamy, az kazdy czlowiek trzyma DOWOLNY kierunek przez 0.2 s
 *  HEAT_RED_LIGHT  losowo 1.5-3.5 s (75-175 klatek). Puszczenie = TASMA (falstart)
 *  HEAT_TAPE_UP    zielone + brzeczyk; tasma odlatuje przez TAPE_GONE klatek:
 *                  rozmyta -> gruba przerywana -> strzepy -> punkty -> brak
 *                  Start = PUSZCZENIE przycisku. Czas reakcji = klatki od zielonego.
 *                  Trzymanie > WHEELSPIN_FRAMES po zielonym = mielenie kolem w miejscu.
 *  HEAT_FALSE_START czerwona flaga sedziego, 3 s, ostrzezenie; druga tasma = wykluczenie.
 *                  Tez po upadku (race.c): upadly wykluczony, bieg od nowa spod tasmy. */
#include "game.h"

#define PRE_FRAMES        50
#define READY_HOLD_FRAMES 10
#define RED_MIN_FRAMES    75     /* 1.5 s */
#define RED_MAX_FRAMES    175    /* 3.5 s */
#define WHEELSPIN_FRAMES  15     /* 300 ms trzymania po zielonym */
#define PERFECT_FRAMES    5      /* <= 100 ms = start idealny */
#define FALSE_START_SHOW  150
#define MAX_WARNINGS      2

static UBYTE ready_hold;

static BOOL bike_active(const Bike *b) { return b->state != BIKE_EXCLUDED; }
static BOOL is_human(const Bike *b)    { return g_tour.riders[b->rider].control == CTRL_HUMAN; }

void start_begin(void)
{
    Heat *h = &g_tour.heat;
    UBYTE i;

    h->phase = HEAT_PRE;
    h->phase_timer = 0;
    h->tape_anim = 0;
    h->false_starter = 0xFF;
    h->red_light_frames = rng_range(RED_MIN_FRAMES, RED_MAX_FRAMES);
    /* rzadko (~1 na 20 startow) awaria maszyny startowej i swiatel: sedzia startuje
     * flaga narodowa - bez tasmy, sygnalem jest machniecie flaga */
    h->emergency = (UBYTE)(rng_range(0, 19) == 0);
    ready_hold = 0;

    race_init_heat();     /* ustawia motocykle na polach, zeruje predkosci */
    roost_reset();

    for (i = 0; i < MAX_RIDERS; i++) {
        Bike *b = &g_tour.bikes[i];
        if (!bike_active(b)) continue;
        b->state = BIKE_AT_GATE;
        b->reaction_frames = 0;
        b->launch_quality = 0;
        if (!is_human(b))
            b->ai_reaction = ai_reaction_frames(g_tour.riders[b->rider].ai_level);
    }
}

/* Jakosc startu 0..255: liniowo od idealnego (<=100 ms) do zera przy progu mielenia. */
static UBYTE launch_quality(UWORD frames)
{
    if (frames <= PERFECT_FRAMES) return 255;
    if (frames >= WHEELSPIN_FRAMES) return 0;
    return (UBYTE)(255 - (frames - PERFECT_FRAMES) * 255 / (WHEELSPIN_FRAMES - PERFECT_FRAMES));
}

static void call_false_start(UBYTE bike_idx)
{
    Heat *h = &g_tour.heat;
    Bike *b = &g_tour.bikes[bike_idx];
    Rider *r = &g_tour.riders[b->rider];

    h->phase = HEAT_FALSE_START;
    h->phase_timer = 0;
    h->false_starter = bike_idx;
    h->stop_fall = 0;
    h->restarts++;
    g_tour.stats.false_starts++;      /* raz na zdarzenie (gazeta) */
    if (!g_training && ++r->warnings >= MAX_WARNINGS) {      /* trening: tylko powtorka */
        b->state = BIKE_EXCLUDED;     /* wykluczenie: 0 pkt, powtorka bez niego */
        g_tour.stats.exclusions++;
        g_tour.stats.excluded[b->rider]++;
    }
    sfx_groan();
}

/* Upadek w biegu: czerwona flaga, upadly wykluczony (W, jak po drugiej tasmie - race_init_heat
 * go nie ustawi), po FALSE_START_SHOW bieg od nowa spod tasmy bez niego. */
void start_stop_fall(UBYTE bike_idx)
{
    Heat *h = &g_tour.heat;
    Bike *b = &g_tour.bikes[bike_idx];
    h->phase = HEAT_FALSE_START;
    h->phase_timer = 0;
    h->false_starter = bike_idx;
    h->stop_fall = 1;
    h->restarts++;
    g_tour.riders[b->rider].warnings = MAX_WARNINGS;
    b->state = BIKE_EXCLUDED;
    g_tour.stats.exclusions++;
    g_tour.stats.excluded[b->rider]++;
    sfx_groan();
}

/* Wejscie "trzyma" przed startem: czlowiek - realny przycisk, AI - zawsze trzyma
 * az do swojego czasu reakcji. */
static BOOL holding(Bike *b, UWORD since_green, BOOL green)
{
    if (is_human(b))
        return (b->input & BTN_DIRS) != 0;
    if (!green) {
        /* antycypacja AI: ujemna reakcja = puszcza |ai_reaction| klatek przed zielonym */
        Heat *h = &g_tour.heat;
        return !(b->ai_reaction < 0 && h->phase_timer + (UWORD)(-b->ai_reaction) >= h->red_light_frames);
    }
    return (WORD)since_green < b->ai_reaction;
}

static void update_launches(UWORD since_green)
{
    UBYTE i;
    for (i = 0; i < MAX_RIDERS; i++) {
        Bike *b = &g_tour.bikes[i];
        if (b->state != BIKE_AT_GATE && b->state != BIKE_WHEELSPIN) continue;

        if (!holding(b, since_green, TRUE)) {
            b->reaction_frames = since_green;
            b->launch_quality = (b->state == BIKE_WHEELSPIN) ? 0 : launch_quality(since_green);
            if (is_human(b)) {
                Rider *r = &g_tour.riders[b->rider];
                UWORD ms = (UWORD)(since_green * 20);
                if (!r->best_reaction_ms || ms < r->best_reaction_ms) r->best_reaction_ms = ms;
            }
            race_launch(b);
        } else if (since_green >= WHEELSPIN_FRAMES) {
            b->state = BIKE_WHEELSPIN;
            b->skid_frame = (UBYTE)(1 + ((since_green >> 1) & 1));  /* drgajacy tyl */
        }
    }
}

void start_update(void)
{
    Heat *h = &g_tour.heat;
    UBYTE i;

    h->phase_timer++;

    switch (h->phase) {
    case HEAT_PRE:
        if (h->phase_timer >= PRE_FRAMES) { h->phase = HEAT_READY; h->phase_timer = 0; }
        break;

    case HEAT_READY: {
        BOOL all = TRUE;
        for (i = 0; i < MAX_RIDERS; i++) {
            Bike *b = &g_tour.bikes[i];
            if (bike_active(b) && is_human(b) && !(b->input & BTN_DIRS)) all = FALSE;
        }
        ready_hold = all ? (UBYTE)(ready_hold + 1) : 0;
        if (ready_hold >= READY_HOLD_FRAMES) { h->phase = HEAT_RED_LIGHT; h->phase_timer = 0; }
        break;
    }

    case HEAT_RED_LIGHT:
        /* pierwszy, kto puscil (przy remisie - nizsze pole) */
        for (i = 0; i < MAX_RIDERS; i++) {
            Bike *b = &g_tour.bikes[i];
            if (bike_active(b) && !holding(b, 0, FALSE)) { call_false_start(i); return; }
        }
        if (h->phase_timer >= h->red_light_frames) {
            h->phase = HEAT_TAPE_UP;
            h->phase_timer = 0;
            h->tape_anim = 1;
            if (!h->emergency) sfx_buzzer();         /* przy awarii: tylko flaga */
            update_launches(0);
        }
        break;

    case HEAT_TAPE_UP:
        h->tape_anim = (UBYTE)(h->phase_timer < TAPE_GONE ? h->phase_timer : TAPE_GONE);
        update_launches(h->phase_timer);
        if (h->tape_anim == TAPE_GONE) h->phase = HEAT_RACING;   /* timer liczy dalej od zielonego */
        break;

    case HEAT_RACING:
        update_launches(h->phase_timer);   /* spoznialscy i mielacy kolem */
        break;

    case HEAT_FALSE_START:
        if (h->phase_timer >= FALSE_START_SHOW) start_begin();
        break;

    default:
        break;
    }
}
