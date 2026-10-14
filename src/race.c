/* race.c - model jazdy (auto-gaz, LEWO/PRAWO = kat zlamania motocykla).
 *
 * Motocykl ma KIERUNEK JAZDY (course88, kat 8.8) i RAME ustawiona pod katem
 * zlamania (drift) do kierunku jazdy: heading = course - drift.
 *   LEWO  -> drift rosnie do 40 st. (rama w luk), PRAWO -> kontra do ~14 st. w prawo.
 *   Kierunek jazdy skreca proporcjonalnie do zlamania, predkosci i przyczepnosci,
 *   wiec przy pelnym zlamaniu promien skretu ma ok. 45 px - motocykl nie zawraca w miejscu.
 *   Im mocniej zlamany, tym nizsza predkosc maksymalna i wiekszy opor boczny.
 * Wartosci do strojenia zebrane w #define (symulator: tools/sim). */
#include "game.h"

#define MAX_SPEED       BIKE_MAX_SPEED
#define DRIFT_MAX       28           /* 40 st. (256 jednostek = 360 st.) */
#define DRIFT_MIN       (-10)        /* ~14 st. kontry w prawo */
#define DRIFT_IN        2            /* LEWO: lamanie na klatke (pelne zlamanie w ~0.3 s) */
#define DRIFT_OUT       3            /* PRAWO: kontra na klatke */
#define DRIFT_CAP_DIV   112          /* zlamanie obniza predkosc maks.: 40 st. -> ok. -25% */
#define ACCEL_SHIFT     6            /* przyspieszenie = (max-v)>>6 * grip */
#define DRAG_DIV        3200         /* opor boczny: v*|drift|/DRAG_DIV */
#define LAUNCH_FRAMES   50
#define BUMP_DIST2      (8 * 8)      /* kontakt lokiec w lokiec (motocykl ~9 px szerokosci) */
#define BUMP_COOLDOWN   12
#define OUTER_LIMIT     (TRACK_HALF_WIDTH - 3)
#define INNER_LIMIT     (-TRACK_HALF_WIDTH + 2)
#define DNF_FRAMES      (15 * FPS)   /* po zwyciezcy reszta ma 15 s */
#define DRY_EVERY       (10 * FPS)

static UWORD winner_frame;
static UBYTE end_wait;                   /* odliczanie od przejazdu ostatniego przez mete */
#define END_WAIT_FRAMES (2 * FPS)

void race_init_heat(void)
{
    Track *t = &g_tour.track;
    Heat *h = &g_tour.heat;
    UBYTE g;

    h->finished_count = 0;
    h->race_frames = 0;
    h->danger_timer = 0;
    winner_frame = 0;
    end_wait = 0;
    for (g = 0; g < 4; g++) {
        Bike *b = &g_tour.bikes[g];
        UBYTE r = h->gate_rider[g];
        b->empty = (UBYTE)(r == 0xFF);                    /* puste pole (bieg dodatkowy) */
        if (b->empty) r = 0;
        b->rider = r;
        b->gate = g;
        b->hw_sprite = (UBYTE)(g * 2);
        b->x = INT2FIX(t->gate_x);
        b->y = INT2FIX(t->gate_y[g]);
        b->heading = 0;
        b->course88 = 0;
        b->speed = 0;
        b->max_speed = MAX_SPEED;
        b->drift = 0;
        b->lap = 0;
        b->ai_line_offset = (BYTE)track_lateral(t->gate_x, t->gate_y[g]);   /* bot rusza ze swojego pola */
        b->race_distance = track_progress(t->gate_x, t->gate_y[g]) - t->tape_s;
        if (b->race_distance < 0) b->race_distance += t->lap_length;
        b->skid_frame = 0;
        b->sprite_frame = 0;
        b->bump_cooldown = 0;
        b->in_roost = 0;
        b->ai_timer = 0;
        b->launch_timer = 0;
        b->lap_start = 0;
        b->best_lap = 0;
        b->position = g;
        b->state = (b->empty || g_tour.riders[r].warnings >= 2) ? BIKE_EXCLUDED : BIKE_AT_GATE;
    }
}

void race_launch(Bike *b)
{
    b->state = BIKE_RACING;
    b->speed = FIXPCT(MAX_SPEED, 10 + b->launch_quality * 30 / 255);
    b->launch_timer = LAUNCH_FRAMES;
}

static void push_to_lateral(Bike *b, WORD lat) { track_snap_lateral(&b->x, &b->y, lat); }
static void align_to_tangent(Bike *b)          { b->course88 = (UWORD)(track_tangent(b->x, b->y) << 8); }

/* Szpryca: >= ROOST_HITS cudzych drobin w strefie przed motocyklem (roost.c)
 * -> minimalna kara predkosci; zmusza do szukania czystej linii ("mijanki"). */
#define ROOST_HITS 2

static void update_bike(Bike *b)
{
    Track *t = &g_tour.track;
    WORD px = FIX2INT(b->x), py = FIX2INT(b->y);
    UBYTE grip = track_grip_at(px, py);
    GripZone zone = track_zone_fast(px, py);
    UBYTE in;
    angle8 travel;
    fix16 cap = b->max_speed;
    LONG s, prev_s;
    WORD lat;

    in = (g_tour.riders[b->rider].control == CTRL_AI) ? ai_steer(b) : b->input;
    b->prev_input = in;

    /* --- kat zlamania --- */
    if (in & BTN_LEFT)       b->drift = (BYTE)(b->drift + DRIFT_IN);
    else if (in & BTN_RIGHT) b->drift = (BYTE)(b->drift - DRIFT_OUT);
    else if (b->drift > 0 && (g_tour.heat.race_frames & 1)) b->drift--;
    else if (b->drift < 0) b->drift++;
    if (b->drift > DRIFT_MAX) b->drift = DRIFT_MAX;
    if (b->drift < DRIFT_MIN) b->drift = DRIFT_MIN;

    /* --- skret kierunku jazdy: zlamanie x predkosc x przyczepnosc (dokladnosc 1/256) --- */
    {
        LONG vfrac = (b->speed << 8) / MAX_SPEED;                       /* 0..~280 */
        LONG gripf = 128 + grip / 2;                                    /* mokro: skret ~20% slabszy */
        LONG turn88 = (LONG)b->drift * TURN_K * vfrac / 256 * gripf / 256;
        b->course88 = (UWORD)(b->course88 - (UWORD)turn88);
    }
    travel = (angle8)(b->course88 >> 8);
    b->heading = (angle8)(travel - b->drift);                           /* rama w luk */

    /* --- predkosc: auto-gaz minus subtelny opor boczny --- */
    if (zone == GRIP_DEEP_OUTER) {
        if (b->speed < FIXPCT(MAX_SPEED, 70)) b->speed -= b->speed >> 6;          /* zakopuje */
        else if (b->drift >= 3 && b->drift <= 14) {                               /* szeroka niesie */
            cap = FIXPCT(MAX_SPEED, 108);
            b->speed += MAX_SPEED >> 8;
        }
    }
    /* im mocniej zlamany, tym nizsza predkosc, do ktorej motocykl moze sie rozpedzic */
    if (b->drift > 0) cap -= (cap / DRIFT_CAP_DIV) * b->drift;
    if (b->speed < cap)
        b->speed += (((cap - b->speed) >> ACCEL_SHIFT) >> 8) * grip;
    else                                    /* powyzej limitu (np. po zjezdzie z "szerokiej") - wraca do niego */
        b->speed -= (b->speed - cap) >> 4;
    if (b->launch_timer) {
        /* bonus startowy: szybsze rozpedzanie, ale nie ponad predkosc maksymalna */
        if (b->speed < cap) {
            b->speed += (MAX_SPEED >> 9) * b->launch_quality / 64;
            if (b->speed > cap) b->speed = cap;
        }
        b->launch_timer--;
    }
    b->speed -= (fix16)((b->speed >> 4) * (b->drift < 0 ? -b->drift : b->drift) / (DRAG_DIV >> 4));
    if (((g_tour.heat.race_frames + (b - g_tour.bikes)) & 1) == 0)     /* co 2 klatki */
        b->in_roost = (UBYTE)(roost_hits(b) >= ROOST_HITS);
    if (b->in_roost) b->speed -= b->speed >> 8;
    if (b->state == BIKE_FINISHED && b->speed > MAX_SPEED / 2) b->speed -= b->speed >> 5;
    if (b->speed < 0) b->speed = 0;

    /* --- rozpedzony motocykl ubija nawierzchnie (wyjezdzona sciezka) --- */
    if (b->speed > MAX_SPEED / 2) track_wear(px, py);

    /* --- ruch --- */
    b->x += ((b->speed >> 6) * COS(travel)) >> 8;
    b->y += ((b->speed >> 6) * SIN(travel)) >> 8;

    /* --- banda i kraweznik --- */
    lat = track_lateral(FIX2INT(b->x), FIX2INT(b->y));
    if (lat > OUTER_LIMIT) {
        b->speed -= b->speed >> 2;            /* -25% */
        b->drift = 0;
        push_to_lateral(b, OUTER_LIMIT - 1);
        align_to_tangent(b);
        /* TODO audio: uderzenie w bande */
    } else if (lat < INNER_LIMIT) {
        /* kraweznik: lekka strata i motocykl rownolegle do toru - inaczej przy slabym
         * skrecie w prawo nie dalby sie od niego odkleic */
        b->speed -= b->speed >> 5;
        push_to_lateral(b, INNER_LIMIT + 1);
        align_to_tangent(b);
    }

    /* --- okrazenia: przeciecie linii tasmy --- */
    prev_s = b->race_distance - (LONG)b->lap * t->lap_length;
    s = track_progress(FIX2INT(b->x), FIX2INT(b->y)) - t->tape_s;
    if (s < 0) s += t->lap_length;
    if (prev_s > t->lap_length * 3 / 4 && s < t->lap_length / 4) {
        /* przeciecie linii: czas okrazenia (pierwsze przeciecie = start pomiaru) */
        UWORD now = g_tour.heat.race_frames;
        if (b->lap >= 1) {
            UWORD lap = (UWORD)(now - b->lap_start);
            if (!b->best_lap || lap < b->best_lap) b->best_lap = lap;
        }
        b->lap_start = now;
        b->lap++;
    }
    else if (prev_s < t->lap_length / 4 && s > t->lap_length * 3 / 4 && b->lap) b->lap--;
    b->race_distance = (LONG)b->lap * t->lap_length + s;

    if (b->state == BIKE_RACING && b->lap > LAPS_PER_HEAT) {
        Heat *h = &g_tour.heat;
        b->state = BIKE_FINISHED;
        b->finish_frame = h->race_frames;
        h->finish_order[h->finished_count++] = b->rider;
        if (h->finished_count == 1) winner_frame = h->race_frames;
    }

    b->skid_frame = (UBYTE)(b->drift > 18 ? 2 : (b->drift > 8 || b->drift < -6) ? 1 : 0);
    {
        /* klatka kierunku z histereza: przy kacie na granicy dwoch klatek nie mruga miedzy nimi */
        BYTE d = (BYTE)(angle8)(b->heading - (angle8)(b->sprite_frame * 8));
        if (d > 5 || d < -5) b->sprite_frame = ANGLE_TO_FRAME(b->heading);
    }
}

/* kontakt lokiec w lokiec: obaj -8%, krotki cooldown, rozsuniecie */
static void collisions(void)
{
    UBYTE i, j;
    for (i = 0; i < MAX_RIDERS; i++) {
        Bike *a = &g_tour.bikes[i];
        if (a->bump_cooldown) a->bump_cooldown--;
    }
    for (i = 0; i < MAX_RIDERS; i++)
        for (j = (UBYTE)(i + 1); j < MAX_RIDERS; j++) {
            Bike *a = &g_tour.bikes[i], *b = &g_tour.bikes[j];
            LONG dx, dy;
            if (a->state < BIKE_WHEELSPIN || b->state < BIKE_WHEELSPIN) continue;
            if (a->state == BIKE_EXCLUDED || b->state == BIKE_EXCLUDED) continue;
            dx = FIX2INT(a->x - b->x);
            dy = FIX2INT(a->y - b->y);
            if (dx * dx + dy * dy >= BUMP_DIST2) continue;
            if (!a->bump_cooldown && !b->bump_cooldown) {
                a->speed -= FIXPCT(a->speed, 8);
                b->speed -= FIXPCT(b->speed, 8);
                a->bump_cooldown = b->bump_cooldown = BUMP_COOLDOWN;
            }
            /* rozsuniecie o 1 px na klatke, zeby nie sklejaly sie */
            if (dx >= 0) { a->x += FIX_ONE; b->x -= FIX_ONE; } else { a->x -= FIX_ONE; b->x += FIX_ONE; }
            if (dy >= 0) { a->y += FIX_ONE; b->y -= FIX_ONE; } else { a->y -= FIX_ONE; b->y += FIX_ONE; }
        }
}

static void update_positions(void)
{
    UBYTE order[MAX_RIDERS], i, j, n = 0;
    for (i = 0; i < MAX_RIDERS; i++) order[n++] = i;
    for (i = 1; i < n; i++)
        for (j = i; j > 0; j--) {
            Bike *a = &g_tour.bikes[order[j - 1]], *b = &g_tour.bikes[order[j]];
            BOOL swap;
            if (a->state == BIKE_EXCLUDED) swap = b->state != BIKE_EXCLUDED;
            else if (a->state == BIKE_FINISHED && b->state == BIKE_FINISHED) swap = b->finish_frame < a->finish_frame;
            else if (b->state == BIKE_FINISHED) swap = a->state != BIKE_FINISHED;
            else swap = a->state != BIKE_FINISHED && b->state != BIKE_EXCLUDED && b->race_distance > a->race_distance;
            if (swap) { UBYTE t = order[j]; order[j] = order[j - 1]; order[j - 1] = t; }
        }
    for (i = 0; i < n; i++) g_tour.bikes[order[i]].position = i;
}

void race_update(void)
{
    Heat *h = &g_tour.heat;
    UBYTE i, active = 0, done = 0;

    h->race_frames++;
    if (h->danger_timer) h->danger_timer--;
    for (i = 0; i < MAX_RIDERS; i++) {
        Bike *b = &g_tour.bikes[i];
        if (b->state == BIKE_RACING || b->state == BIKE_FINISHED) update_bike(b);
        else if (b->state == BIKE_WHEELSPIN) b->speed = 0;
        if (b->state != BIKE_EXCLUDED) active++;
        if (b->state == BIKE_FINISHED) done++;
    }
    roost_update(TRUE);
    collisions();
    update_positions();

    if (h->race_frames % DRY_EVERY == 0) track_dry_step();

    /* ostatni na mecie -> jeszcze 2 s jazdy wybiegu, dopiero potem plansza wynikow */
    if (end_wait) {
        if (--end_wait == 0) h->phase = HEAT_FINISHED;
    } else if (done == active || (winner_frame && h->race_frames - winner_frame > DNF_FRAMES))
        end_wait = END_WAIT_FRAMES;
}
