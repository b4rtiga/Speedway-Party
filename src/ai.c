/* ai.c - boty: te same dwie komendy (LEWO/PRAWO) co gracz.
 * Cel = punkt na osi toru przed motocyklem, przesuniety bocznie wg poziomu. */
#include "game.h"

/* Reakcja na tasme (klatki po 20 ms). Ujemna = antycypacja -> tasma. */
WORD ai_reaction_frames(AiLevel lvl)
{
    UWORD roll = rng_range(0, 999);
    switch (lvl) {
    case AI_AMATEUR: return roll < 40 ? -(WORD)rng_range(3, 20) : (WORD)rng_range(12, 22);  /* 240-440 ms, spozniony */
    case AI_PRO:     return roll < 10 ? -(WORD)rng_range(2, 10) : (WORD)rng_range(8, 13);   /* 160-260 ms */
    default:         return roll < 5  ? -(WORD)rng_range(1, 4)  : (WORD)rng_range(6, 9);    /* 120-180 ms */
    }
}

static LONG bike_s(const Bike *b)
{
    return track_progress(FIX2INT(b->x), FIX2INT(b->y));
}

/* MISTRZ: probkuje mape przyczepnosci w trzech liniach i wybiera najlepsza;
 * przy duzej predkosci chetnie idzie szeroko ("szeroka niesie"). */
static WORD master_line(const Bike *b, LONG s_ahead)
{
    static const WORD lines[4] = { -10, -4, 6, 19 };
    WORD best = -6, best_score = -32000, i;
    for (i = 0; i < 4; i++) {
        WORD x, y, score;
        track_point(s_ahead, lines[i], &x, &y);
        score = track_grip_at(x, y);
        if (lines[i] > 0 && b->speed > b->max_speed / 10 * 9 && track_zone_at(x, y) == GRIP_DEEP_OUTER)
            score += 25;                            /* szeroka niesie - tylko z pelna predkoscia */
        score -= lines[i] * 3;                      /* dluzsza droga po zewnetrznej kosztuje */
        if (score > best_score) { best_score = score; best = lines[i]; }
    }
    return best;
}

/* PROFESJONALISTA: "zamyka plot" - jesli rywal jest tuz za nim od wewnatrz,
 * schodzi na jego linie. */
static WORD pro_line(const Bike *b, LONG s)
{
    UBYTE i;
    WORD my_lat = track_lateral(FIX2INT(b->x), FIX2INT(b->y));
    for (i = 0; i < MAX_RIDERS; i++) {
        const Bike *o = &g_tour.bikes[i];
        LONG d;
        WORD lat;
        if (o == b || o->state != BIKE_RACING) continue;
        d = s - bike_s(o);
        if (d < 0) d += g_tour.track.lap_length;
        lat = track_lateral(FIX2INT(o->x), FIX2INT(o->y));
        if (d > 0 && d < 30 && lat < my_lat) return (WORD)(lat - 2);
    }
    return -6;
}

/* Jeden regulator uslizgu dla wszystkich poziomow, rozne nastawy:
 *   gain  - jak mocno kat bledu kierunku zamienia sie na docelowy uslizg
 *   cap   - maksymalny uslizg (Mistrz dozuje minimalny -> mniej oporu)
 *   tol   - martwa strefa (wieksza = szarpane, pulsacyjne sterowanie)
 *   hold  - opoznienie reakcji w klatkach (Amator "przetrzymuje" decyzje) */
typedef struct { BYTE gain, cap, tol, hold; } SteerTune;
static const SteerTune tune[3] = {
    { 4, 48, 4, 2 },     /* AMATOR: lamie za mocno i za pozno */
    { 3, 40, 2, 0 },     /* PROFESJONALISTA */
    { 3, 30, 1, 0 },     /* MISTRZ */
};

UBYTE ai_steer(Bike *b)
{
    AiLevel lvl = g_tour.riders[b->rider].ai_level;
    const SteerTune *t = &tune[lvl];
    LONG s = bike_s(b);
    LONG ahead = s + 28 + FIX2INT(b->speed * 8);
    WORD lat, tx, ty, tw;
    BYTE err;
    UBYTE out = 0;

    if (b->ai_timer) { b->ai_timer--; return b->prev_input; }

    switch (lvl) {
    case AI_AMATEUR: lat = -12; break;                  /* kurczowo przy krawezniku */
    case AI_PRO:     lat = pro_line(b, s); break;
    default:         lat = master_line(b, ahead); break;
    }
    b->ai_line_offset = (BYTE)lat;
    track_point(ahead, lat, &tx, &ty);
    err = (BYTE)(atan2_8(ty - FIX2INT(b->y), tx - FIX2INT(b->x)) - b->heading);
    /* err < 0: trzeba skrecic w lewo -> potrzebny dodatni uslizg */

    tw = (WORD)(-err * t->gain);
    {
        /* Mistrz dozuje uslizg do przyczepnosci: na mokrym potrzeba go wiecej */
        WORD cap = t->cap;
        if (lvl == AI_MASTER) cap = (WORD)(cap + (220 - (WORD)track_grip_at(FIX2INT(b->x), FIX2INT(b->y))) / 4);
        if (tw > cap) tw = cap;
    }
    if (tw < -12) tw = -12;
    if (b->drift < tw - t->tol) out = BTN_LEFT;
    else if (b->drift > tw + t->tol) out = BTN_RIGHT;
    b->ai_timer = (UWORD)t->hold;
    return out;
}
