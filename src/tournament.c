/* tournament.c - tabela 15 biegow, punktacja 3-2-1-0, biegi nominowane. */
#include "game.h"

Tournament g_tour;

/* Biegi 1-13: [bieg][pole A..D] -> zawodnik. Trzy kwadraty lacinskie 4x4 (biegi 1-12)
 * daja kazdemu kazde pole 3 razy; bieg 13 to dodatkowa permutacja. */
static const UBYTE fixed_table[FIXED_HEATS][4] = {
    {0,1,2,3},{1,2,3,0},{2,3,0,1},{3,0,1,2},
    {3,2,1,0},{0,3,2,1},{1,0,3,2},{2,1,0,3},
    {0,2,3,1},{2,3,1,0},{3,1,0,2},{1,0,2,3},
    {1,3,0,2}
};

static const char *default_names[MAX_RIDERS] = { "KOWALSKI", "NOWAK", "WISNIEWSKI", "ZIELINSKI" };

void tournament_defaults(void)
{
    static const InputPort ports[MAX_RIDERS] = { INPUT_JOY1, INPUT_JOY0, INPUT_KEYS_ZX, INPUT_KEYS_NM };
    UBYTE i, j;
    g_tour.num_humans = 1;
    for (i = 0; i < MAX_RIDERS; i++) {
        Rider *r = &g_tour.riders[i];
        const char *n = default_names[i];
        for (j = 0; n[j] && j < NAME_LEN; j++) r->name[j] = n[j];
        r->name[j] = 0;
        r->input = ports[i];
        r->ai_level = (AiLevel)(i % 3);
    }
}

void tournament_reset(void)
{
    UBYTE i, j;
    for (i = 0; i < MAX_RIDERS; i++) {
        Rider *r = &g_tour.riders[i];
        r->control = (i < g_tour.num_humans) ? CTRL_HUMAN : CTRL_AI;
        r->points = r->wins = 0;
        r->warnings = 0;
        r->best_reaction_ms = 0;
        for (j = 0; j < NUM_HEATS; j++) r->heat_points[j] = HP_NONE;
        for (j = 0; j < 4; j++) r->gate_count[j] = 0;
    }
    for (i = 0; i < FIXED_HEATS; i++)
        for (j = 0; j < 4; j++)
            g_tour.heat_table[i][j] = fixed_table[i][j];
    for (i = FIXED_HEATS; i < NUM_HEATS; i++)
        for (j = 0; j < 4; j++)
            g_tour.heat_table[i][j] = 0xFF;      /* ustalane przy wyborze pol */
    g_tour.current_heat = 0;
    g_tour.heat.restarts = 0;
}

void tournament_award_points(void)
{
    static const UBYTE pts[4] = { 3, 2, 1, 0 };
    Heat *h = &g_tour.heat;
    UBYTE i, hn = g_tour.current_heat;

    for (i = 0; i < MAX_RIDERS; i++) {
        const Bike *b = &g_tour.bikes[i];
        g_tour.riders[b->rider].heat_points[hn] = b->state == BIKE_EXCLUDED ? HP_EXCLUDED : 0;  /* nieukonczeni = 0 */
    }
    for (i = 0; i < h->finished_count; i++) {
        Rider *r = &g_tour.riders[h->finish_order[i]];
        r->heat_points[hn] = pts[i];
        r->points += pts[i];
        if (i == 0) r->wins++;
    }
    for (i = 0; i < 4; i++) {
        Rider *r = &g_tour.riders[g_tour.heat_table[hn][i]];
        r->gate_count[i]++;
        r->warnings = 0;
    }
}

/* polewanie po biegach 4, 7, 11, 13 */
BOOL tournament_watering_after(UBYTE heat_number)
{
    return heat_number == 4 || heat_number == 7 || heat_number == 11 || heat_number == 13;
}

/* Ranking: wiecej punktow, potem wiecej zwyciestw, potem nizszy numer. */
void tournament_sort(UBYTE order[MAX_RIDERS])
{
    UBYTE i, j;
    for (i = 0; i < MAX_RIDERS; i++) order[i] = i;
    for (i = 1; i < MAX_RIDERS; i++)
        for (j = i; j > 0; j--) {
            Rider *a = &g_tour.riders[order[j - 1]];
            Rider *b = &g_tour.riders[order[j]];
            if (b->points > a->points || (b->points == a->points && b->wins > a->wins)) {
                UBYTE t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
            }
        }
}

/* Kolejnosc wyboru pol w biegach nominowanych = ranking. */
void tournament_prepare_nominated(void)
{
    UBYTE i, n = g_tour.current_heat;
    tournament_sort(g_tour.pick_order);
    for (i = 0; i < 4; i++) g_tour.heat_table[n][i] = 0xFF;
    g_tour.pick_index = 0;
}

void tournament_assign_gate(UBYTE rider, UBYTE gate)
{
    g_tour.heat_table[g_tour.current_heat][gate] = rider;
    g_tour.pick_index++;
}

/* AI wybiera wolne pole o najlepszej przyczepnosci na pierwszych metrach. */
UBYTE tournament_ai_choose_gate(UBYTE rider)
{
    Track *t = &g_tour.track;
    UBYTE g, best = 0xFF;
    WORD best_score = -1;
    (void)rider;
    for (g = 0; g < 4; g++) {
        WORD score;
        if (g_tour.heat_table[g_tour.current_heat][g] != 0xFF) continue;
        score = track_grip_at(t->gate_x + 24, t->gate_y[g]) + track_grip_at(t->gate_x + 48, t->gate_y[g]);
        score += (WORD)(3 - g) * 4;          /* lekka preferencja wewnetrznego pola */
        if (score > best_score) { best_score = score; best = g; }
    }
    return best;
}
