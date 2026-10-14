/* news_test.c - test gazety na PC: wybor naglowka (scenariusze z feature/materials/fixtures.json),
 * migawka turnieju, zerowanie faktow przy nowym turnieju, obciecie dlugiego nazwiska.
 *   sh tools/sim/news_test.sh */
#include <stdio.h>
#include <string.h>
#include "game.h"

static int fails;
#define CHECK(cond, what) do { if (!(cond)) { printf("BLAD: %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)

static NewsRule rule(BOOL runoff, BOOL record, BOOL sweep, UBYTE wp, UBYTE rp)
{
    NewsFacts f;
    f.title_runoff = runoff; f.winner_owns_record = record; f.winner_clean_sweep = sweep;
    f.winner_points = wp; f.runner_points = rp;
    return news_pick_rule(&f);
}

/* turniej "na sucho": punkty i zwyciestwa ustawione recznie */
static void tour(const UBYTE pts[4], const UBYTE wins[4])
{
    UBYTE i;
    tournament_reset();
    for (i = 0; i < MAX_RIDERS; i++) {
        g_tour.riders[i].points = pts[i];
        g_tour.riders[i].wins = wins[i];
        g_tour.stats.starts[i] = 5;
    }
}

int main(void)
{
    static const UBYTE p_ord[4] = { 12, 9, 7, 3 }, w_ord[4] = { 3, 1, 1, 0 };
    static const UBYTE p_one[4] = { 11, 10, 6, 3 };
    static const UBYTE p_sw[4] = { 15, 9, 6, 0 }, w_sw[4] = { 5, 0, 0, 0 };
    const NewsSummary *n = news_summary();

    tournament_defaults();
    /* fixtures.json */
    CHECK(rule(FALSE, FALSE, FALSE, 12, 9) == NEWS_FALLBACK, "ordinary -> fallback");
    CHECK(rule(TRUE, TRUE, FALSE, 12, 12) == NEWS_RUNOFF, "title_runoff_beats_record -> runoff");
    CHECK(rule(FALSE, FALSE, FALSE, 11, 10) == NEWS_ONE_POINT, "one_point");
    CHECK(rule(FALSE, TRUE, FALSE, 12, 9) == NEWS_WINNER_RECORD, "record_holder_wins");
    CHECK(rule(FALSE, FALSE, TRUE, 15, 9) == NEWS_CLEAN_SWEEP, "all_scoring_starts_won");

    /* migawka: zwykly turniej */
    tour(p_ord, w_ord);
    news_build();
    CHECK(n->rule == NEWS_FALLBACK && n->order[0] == 0 && n->points[0] == 12, "build: ordinary");
    CHECK(n->facts[0] == NEWS_FACT_WINNER_WINS && n->fact_value[0] == 3 && n->facts[1] == NEWS_FACT_HUMANS,
          "build: bez falstartow - zwyciestwa lidera i gracze");

    /* bieg dodatkowy tylko o 3. miejsce nie daje naglowka o dogrywce */
    tour(p_ord, w_ord);
    g_tour.stats.other_runoff = TRUE;
    news_build();
    CHECK(n->rule == NEWS_FALLBACK, "runoff_for_third_only -> fallback");

    /* rekord pobity wczesniej przez zwyciezce, potem przez kogos innego */
    tour(p_ord, w_ord);
    g_tour.stats.rec_lap_holder = 0;
    g_tour.stats.rec_lap_holder = 2;
    news_build();
    CHECK(n->rule == NEWS_FALLBACK, "earlier_record_later_beaten -> fallback");

    /* jeden punkt roznicy */
    tour(p_one, w_ord);
    news_build();
    CHECK(n->rule == NEWS_ONE_POINT, "build: one_point");

    /* komplet zwyciestw, ale z wykluczeniem - nie "bez straty punktu" */
    tour(p_sw, w_sw);
    news_build();
    CHECK(n->rule == NEWS_CLEAN_SWEEP, "build: clean sweep");
    tour(p_sw, w_sw);
    g_tour.stats.excluded[0] = 1;
    news_build();
    CHECK(n->rule != NEWS_CLEAN_SWEEP, "build: wykluczenie odbiera komplet");

    /* fakty: falstarty i wykluczenia przed reszta */
    tour(p_ord, w_ord);
    g_tour.stats.false_starts = 3; g_tour.stats.exclusions = 1;
    news_build();
    CHECK(n->facts[0] == NEWS_FACT_FALSE_STARTS && n->fact_value[0] == 3 &&
          n->facts[1] == NEWS_FACT_EXCLUSIONS && n->fact_value[1] == 1, "fakty: falstarty, wykluczenia");

    /* nowy turniej zeruje fakty */
    tournament_reset();
    CHECK(!g_tour.stats.false_starts && !g_tour.stats.exclusions && !g_tour.stats.title_runoff &&
          g_tour.stats.rec_heat_holder == 0xFF && !g_tour.stats.starts[0], "new_tournament: fakty od zera");

    /* dlugie nazwisko */
    tour(p_ord, w_ord);
    strcpy(g_tour.riders[0].name, "BARDZO DLUGIE");
    news_build();
    CHECK(strlen(n->name[0]) <= NEWS_NAME, "long_name: obciete do NEWS_NAME");

    printf(fails ? "\n%d BLEDOW\n" : "\nwszystko OK\n", fails);
    return fails ? 1 : 0;
}
