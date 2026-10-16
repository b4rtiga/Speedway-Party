/* ai.c - boty: te same dwie komendy (LEWO/PRAWO) co gracz.
 * Cel = punkt na osi toru przed motocyklem, przesuniety bocznie wg poziomu. */
#include "game.h"

/* linie jazdy zaprojektowane dla toru o polowie szerokosci 28 px - skalowane do wybranego */
#define BEND_AHEAD 20            /* luk przewidywany 20 px wczesniej - motocykl lamany na czas */
#define LW(v) ((WORD)((v) * TRACK_HALF_WIDTH / 28))

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
    static const WORD lines[4] = { -12, -5, 7, 21 };   /* x LW() */
    WORD best = -6, best_score = -32000, i;
    for (i = 0; i < 4; i++) {
        WORD x, y, score;
        track_point(s_ahead, LW(lines[i]), &x, &y);
        score = track_grip_at(x, y);
        if (lines[i] > 0 && b->speed > b->max_speed / 10 * 9 && track_zone_at(x, y) == GRIP_DEEP_OUTER)
            score += 25;                            /* szeroka niesie - tylko z pelna predkoscia */
        score -= LW(lines[i]) * 3;                      /* dluzsza droga po zewnetrznej kosztuje */
        if (score > best_score) { best_score = score; best = LW(lines[i]); }
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
        if (d > 0 && d < 30 && lat < my_lat) return (WORD)(lat - 2 < LW(-14) ? LW(-14) : lat - 2);
    }
    return LW(-11);
}

/* Jeden regulator uslizgu dla wszystkich poziomow, rozne nastawy:
 *   gain  - jak mocno kat bledu kierunku zamienia sie na docelowy uslizg
 *   cap   - maksymalny uslizg (Mistrz dozuje minimalny -> mniej oporu)
 *   tol   - martwa strefa (wieksza = szarpane, pulsacyjne sterowanie)
 *   hold  - opoznienie reakcji w klatkach (Nowicjusz "przetrzymuje" decyzje) */
/* gain4: wzmocnienie bledu kierunku x4; ff4: mnoznik przewidywania luku x4 (0 = brak) */
typedef struct { BYTE gain4, cap, tol, hold, ff4; } SteerTune;
static const SteerTune tune[3] = {
    {  8, 28, 3, 3, 7 },   /* AMATOR: lamie za mocno i za pozno - marnuje predkosc */
    {  6, 26, 1, 0, 4 },   /* PROFESJONALISTA */
    {  5, 24, 1, 0, 4 },   /* MISTRZ */
};

/* Zlamanie potrzebne, zeby przy obecnej predkosci jechac lukiem o promieniu r
 * (odwrotnosc skretu z race.c: turn88 = drift * TURN_K * vfrac/256 * (128+grip/2)/256). */
static WORD feedforward_drift(const Bike *b, WORD r, UBYTE grip)
{
    LONG vfrac = (b->speed << 8) / BIKE_MAX_SPEED;
    LONG w88, den;
    if (!r || vfrac < 16 || grip < 16) return 0;
    w88 = (b->speed >> 8) * 41 / r;                     /* v/R w jednostkach 1/256 kata */
    den = (LONG)TURN_K * vfrac * (128 + grip / 2) / 256; /* jak gripf w race.c */
    return (WORD)(w88 * 256 / (den ? den : 1));
}

UBYTE ai_steer(Bike *b)
{
    AiLevel lvl = g_tour.riders[b->rider].ai_level;
    const SteerTune *t = &tune[lvl];
    LONG s, ahead;
    WORD lat, tx, ty, tw;
    BYTE err;
    UBYTE out = 0;
    UBYTE idx = (UBYTE)(b - g_tour.bikes);

    if (b->ai_timer) { b->ai_timer--; return b->prev_input; }

    /* Wybor linii i przewidywanie luku (atan2, pierwiastki, dzielenia) tylko co 4 klatki,
     * kazdy bot w innej klatce; pomiedzy - samo porownanie uslizgu z zapamietanym celem.
     * A1200 bez Fast RAM ma na klatke tylko kilkadziesiat tysiecy instrukcji. */
    if (!t->hold && ((g_tour.heat.race_frames + idx) & 3) != 0) {   /* Amator i tak decyduje co hold+1 klatek */
        tw = b->ai_target;
        goto decide;
    }
    s = bike_s(b);
    ahead = s + 28 + FIX2INT(b->speed * 8);

    switch (lvl) {
    case AI_AMATEUR: lat = LW(-14); break;                  /* kurczowo przy krawezniku */
    case AI_PRO:     lat = pro_line(b, s); break;
    default:         lat = master_line(b, ahead); break;
    }
    lat = (WORD)(lat + (b->lap <= 1 ? b->ai_line_bias : b->ai_line_bias / 2));   /* ulubiona linia tego bota */
    /* linia zmienia sie stopniowo (2 px na decyzje): po starcie bot zjezdza ze swojego pola
     * do kraweznika lagodnie, zamiast ciac przez caly tor */
    if (lat > b->ai_line_offset + 2) lat = (WORD)(b->ai_line_offset + 2);
    else if (lat < b->ai_line_offset - 2) lat = (WORD)(b->ai_line_offset - 2);
    b->ai_line_offset = (BYTE)lat;
    track_point(ahead, lat, &tx, &ty);
    err = (BYTE)(atan2_8(ty - FIX2INT(b->y), tx - FIX2INT(b->x)) - (angle8)(b->course88 >> 8));
    /* err < 0: trzeba skrecic w lewo -> potrzebny dodatni uslizg */

    tw = (WORD)(-err * t->gain4 / 4);
    if (t->ff4)
        tw += (WORD)(feedforward_drift(b, track_bend_radius(s + 12 + BEND_AHEAD, lat),
                                       track_grip_at(FIX2INT(b->x), FIX2INT(b->y))) * t->ff4 / 4);
    {
        /* Mistrz dozuje uslizg do przyczepnosci: na mokrym potrzeba go wiecej */
        WORD cap = t->cap, wide;
        if (lvl == AI_MASTER) cap = (WORD)(cap + (220 - (WORD)track_grip_at(FIX2INT(b->x), FIX2INT(b->y))) / 6);
        /* Wynosi go szerzej niz linia - lam mocniej, az do pelnego zlamania. Dawniej limit trzymal,
         * wiec prowadzacy (najszybszy, bez szprycy) wyplywal w luku pod bande, a jadacy za nim
         * wchodzili mu od wewnatrz: pole A (najczesciej pierwsze w 1. luku) wygrywalo 1% biegow,
         * pole C 59% (tools/sim/start_test). */
        wide = (WORD)(track_lateral(FIX2INT(b->x), FIX2INT(b->y)) - lat);
        if (wide > 2) cap = (WORD)(cap + (wide - 2) * 2);
        if (cap > 28) cap = 28;
        if (tw > cap) tw = cap;
    }
    if (tw < -10) tw = -10;                             /* kontra ograniczona jak w race.c */
    b->ai_target = (BYTE)tw;
decide:
    if (b->drift < tw - t->tol) out = BTN_LEFT;
    else if (b->drift > tw + t->tol) out = BTN_RIGHT;
    b->ai_timer = (UWORD)t->hold;
    return out;
}
