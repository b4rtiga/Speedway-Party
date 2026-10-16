/* tournament.c - tabela 15 biegow, punktacja 3-2-1-0, biegi nominowane. */
#include "game.h"
#include "text.h"

Tournament g_tour;

/* Biegi 1-13: [bieg][pole A..D] -> zawodnik. Trzy kwadraty lacinskie 4x4 (biegi 1-12)
 * daja kazdemu kazde pole 3 razy; bieg 13 to dodatkowa permutacja. */
static const UBYTE fixed_table[FIXED_HEATS][4] = {
    {0,1,2,3},{1,2,3,0},{2,3,0,1},{3,0,1,2},
    {3,2,1,0},{0,3,2,1},{1,0,3,2},{2,1,0,3},
    {0,2,3,1},{2,3,1,0},{3,1,0,2},{1,0,2,3},
    {1,3,0,2}
};

/* nazwa domyslna wiersza: "GRACZ n" (EN: "PLAYER n") dla czlowieka, "CPU n" dla bota (n = numer wiersza) */
static void default_name(char *dst, UBYTE i, BOOL human)
{
    char *p = text_cat(dst, human ? T("GRACZ ", "PLAYER ") : "CPU ");
    text_uint(p, (ULONG)(i + 1), 1);
}

static BOOL same(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (BOOL)(*a == *b);
}

/* Nazwy zgodne z liczba graczy: pierwsze num_humans wierszy to GRACZ, reszta CPU.
 * Nazwa wpisana recznie (inna niz domyslna) zostaje. */
void tournament_default_names(void)
{
    UBYTE i;
    for (i = 0; i < MAX_RIDERS; i++) {
        char *n = g_tour.riders[i].name, g[NAME_LEN + 1], c[NAME_LEN + 1];
        default_name(g, i, TRUE);
        default_name(c, i, FALSE);
        if (!n[0] || same(n, g) || same(n, c))
            default_name(n, i, (BOOL)(i < g_tour.num_humans));
    }
}

COLD void tournament_defaults(void)
{
    static const InputPort ports[MAX_RIDERS] = { INPUT_JOY1, INPUT_JOY0, INPUT_KEYS_ZX, INPUT_KEYS_NM };
    UBYTE i;
    g_tour.num_humans = 1;
    g_tour.track_choice = TRACK_RANDOM;
    g_tour.train_cond = 0;                   /* trening: suchy tor */
    g_tour.rain_from = 0xFF;
    tournament_default_names();
    for (i = 0; i < MAX_RIDERS; i++) {
        Rider *r = &g_tour.riders[i];
        r->input = ports[i];
        r->ai_level = (AiLevel)(i % 3);
    }
}

/* Mecz: zawodnicy jednej druzyny zawsze na polach A+C albo B+D, na zmiane co bieg (bieg 1:
 * gospodarze A i C), a w parze co bieg zamiana - po 4 biegach kazdy startowal z kazdego pola.
 * [bieg % 4][pole] -> zawodnik (gospodarze 0, 1; goscie 2, 3). */
static const UBYTE team_table[4][4] = {
    { 0, 2, 1, 3 },
    { 3, 1, 2, 0 },
    { 1, 3, 0, 2 },
    { 2, 0, 3, 1 } };

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
    for (i = FIXED_HEATS; i <= NUM_HEATS; i++)
        for (j = 0; j < 4; j++)
            g_tour.heat_table[i][j] = 0xFF;      /* ustalane przy wyborze pol / biegu dodatkowym */
    if (g_tour.team_mode)                         /* mecz: wszystkie 15 biegow wg par, bez wyboru pol */
        for (i = 0; i < NUM_HEATS; i++)
            for (j = 0; j < 4; j++)
                g_tour.heat_table[i][j] = team_table[i % 4][j];
    for (i = 0; i < MAX_RIDERS; i++) g_tour.runoff_rank[i] = RUNOFF_NONE;
    {
        TourStats *st = &g_tour.stats;          /* fakty do gazety: od zera na kazdy turniej */
        st->false_starts = st->exclusions = st->records = st->defects = st->falls = 0;
        for (i = 0; i < MAX_RIDERS; i++) st->starts[i] = st->excluded[i] = st->defected[i] = 0;
        st->rec_heat_holder = st->rec_lap_holder = 0xFF;
        st->title_runoff = st->other_runoff = FALSE;
    }
    g_tour.current_heat = 0;
    g_tour.heat.restarts = 0;
}

/* Mecz druzynowy: gospodarze to zawodnicy 0 i 1 (kaski czerwony i niebieski), goscie 2 i 3
 * (bialy i zolty) - jak na zuzlu. W kazdym biegu jada obaj z kazdej druzyny. */
UWORD tournament_team_points(UBYTE team)
{
    return (UWORD)(g_tour.riders[team * 2].points + g_tour.riders[team * 2 + 1].points);
}

UBYTE tournament_team_heat(UBYTE team, UBYTE hn)
{
    UBYTE i, sum = 0;
    for (i = 0; i < 2; i++) {
        UBYTE v = g_tour.riders[team * 2 + i].heat_points[hn];
        if (v <= 3) sum = (UBYTE)(sum + v);          /* wykluczenie, defekt, nie jechal = 0 */
    }
    return sum;
}

/* Mistrzostwa: SERIES_ROUNDS turniejow na kolejnych torach. Klasyfikacja generalna to suma
 * punktow biegowych ze wszystkich rund (jak w Grand Prix); przy remisie wiecej wygranych rund,
 * potem lepsze miejsce w ostatniej rundzie. */
void series_reset(void)
{
    UBYTE r, k;
    g_tour.series_round = 0;
    for (r = 0; r < MAX_RIDERS; r++)
        for (k = 0; k < SERIES_ROUNDS; k++) g_tour.series_pts[r][k] = g_tour.series_place[r][k] = 0;
}

void series_add_round(void)
{
    UBYTE order[MAX_RIDERS], i, k = g_tour.series_round;
    tournament_sort(order);                      /* z biegiem dodatkowym i zwyciestwami */
    for (i = 0; i < MAX_RIDERS; i++) {
        g_tour.series_pts[order[i]][k] = g_tour.riders[order[i]].points;
        g_tour.series_place[order[i]][k] = (UBYTE)(i + 1);
    }
}

UWORD series_total(UBYTE rider)
{
    UWORD t = 0;
    UBYTE k;
    for (k = 0; k < SERIES_ROUNDS; k++) t += g_tour.series_pts[rider][k];
    return t;
}

static UBYTE series_firsts(UBYTE rider)
{
    UBYTE k, n = 0;
    for (k = 0; k < SERIES_ROUNDS; k++) n += (UBYTE)(g_tour.series_place[rider][k] == 1);
    return n;
}

void series_sort(UBYTE order[MAX_RIDERS])
{
    UBYTE i, j, k = g_tour.series_round;
    for (i = 0; i < MAX_RIDERS; i++) order[i] = i;
    for (i = 1; i < MAX_RIDERS; i++)
        for (j = i; j > 0; j--) {
            UBYTE a = order[j - 1], b = order[j];
            UWORD ta = series_total(a), tb = series_total(b);
            BOOL swap;
            if (ta != tb) swap = tb > ta;
            else if (series_firsts(a) != series_firsts(b)) swap = series_firsts(b) > series_firsts(a);
            else swap = g_tour.series_place[b][k] && g_tour.series_place[b][k] < g_tour.series_place[a][k];
            if (swap) { order[j] = a; order[j - 1] = b; }
        }
}

void tournament_award_points(void)
{
    static const UBYTE pts[4] = { 3, 2, 1, 0 };
    Heat *h = &g_tour.heat;
    UBYTE i, hn = g_tour.current_heat;

    if (hn == RUNOFF_HEAT) {
        /* bieg dodatkowy: bez punktow, tylko kolejnosc; nieukonczeni i wykluczeni na koncu */
        for (i = 0; i < MAX_RIDERS; i++) {
            const Bike *b = &g_tour.bikes[i];
            if (!b->empty) g_tour.runoff_rank[b->rider] = (UBYTE)(b->state >= BIKE_EXCLUDED ? 20 : 10);
        }
        for (i = 0; i < h->finished_count; i++) g_tour.runoff_rank[h->finish_order[i]] = i;
        for (i = 0; i < MAX_RIDERS; i++) g_tour.riders[i].warnings = 0;
        return;
    }

    for (i = 0; i < MAX_RIDERS; i++) {
        const Bike *b = &g_tour.bikes[i];
        if (b->empty) continue;
        g_tour.riders[b->rider].heat_points[hn] = (b->state == BIKE_EXCLUDED || b->state == BIKE_FALLEN) ? HP_EXCLUDED :
                                                  b->state == BIKE_DEFECT ? HP_DEFECT : 0;
        g_tour.stats.starts[b->rider]++;                 /* start w biegu punktowanym (gazeta) */
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
            UBYTE ra = g_tour.runoff_rank[order[j - 1]], rb = g_tour.runoff_rank[order[j]];
            BOOL swap;
            if (b->points != a->points) swap = b->points > a->points;
            else if (ra != RUNOFF_NONE && rb != RUNOFF_NONE) swap = rb < ra;   /* rozstrzygnal bieg dodatkowy */
            else swap = b->wins > a->wins;
            if (swap) { UBYTE t = order[j]; order[j] = order[j - 1]; order[j - 1] = t; }
        }
}

/* Remis punktowy obejmujacy miejsca 1-3 i jeszcze nierozstrzygniety biegiem dodatkowym
 * -> zawodnicy z remisu jada bieg dodatkowy, pola startowe losowane. */
BOOL tournament_prepare_runoff(void)
{
    UBYTE order[MAX_RIDERS], p, i, n;
    UBYTE *row = g_tour.heat_table[RUNOFF_HEAT];
    tournament_sort(order);
    for (p = 0; p < 3; p++) {
        UBYTE pts = g_tour.riders[order[p]].points, group[MAX_RIDERS];
        BOOL open = FALSE;
        n = 0;
        for (i = 0; i < MAX_RIDERS; i++)
            if (g_tour.riders[order[i]].points == pts) {
                group[n++] = order[i];
                if (g_tour.runoff_rank[order[i]] == RUNOFF_NONE) open = TRUE;
            }
        if (n < 2 || !open) continue;
        if (p == 0) g_tour.stats.title_runoff = TRUE;   /* o tytul (gazeta) */
        else g_tour.stats.other_runoff = TRUE;
        /* nowy bieg dodatkowy dla calej grupy: kasujemy ich stare miejsca */
        for (i = 0; i < n; i++) g_tour.runoff_rank[group[i]] = RUNOFF_NONE;
        for (i = 0; i < 4; i++) row[i] = 0xFF;
        for (i = n; i > 1; i--) {                             /* losowanie pol */
            UBYTE k = (UBYTE)rng_range(0, (UWORD)(i - 1)), t = group[k];
            group[k] = group[i - 1]; group[i - 1] = t;
        }
        for (i = 0; i < n; i++) row[i] = group[i];
        return TRUE;
    }
    return FALSE;
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
