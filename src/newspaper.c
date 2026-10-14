/* newspaper.c - gazeta "GLOS OWALU" po zawodach.
 *
 * Po ostatnim rozstrzygnietym biegu (takze dodatkowym) news_build() robi niezmienna migawke
 * turnieju: ranking z tournament_sort (z biegami dodatkowymi), nazwiska, punkty, zwyciestwa
 * i fakty z g_tour.stats (liczone raz, w miejscu zdarzenia). Naglowek wybiera pierwsza
 * spelniona regula wg priorytetu (feature/materials/newspaper-rules.json), neutralny jest
 * zawsze dostepny. Bez losowania - teksty mowia tylko to, co wynika z danych. */
#include "game.h"

static NewsSummary summary;
static UBYTE tournaments;                /* licznik zawodow w tej sesji (numer wydania) */

NewsRule news_pick_rule(const NewsFacts *f)
{
    if (f->title_runoff) return NEWS_RUNOFF;
    if (f->winner_owns_record) return NEWS_WINNER_RECORD;
    if (f->winner_clean_sweep) return NEWS_CLEAN_SWEEP;
    if (f->winner_points == f->runner_points + 1) return NEWS_ONE_POINT;
    return NEWS_FALLBACK;
}

void news_build(void)
{
    const TourStats *st = &g_tour.stats;
    NewsFacts f;
    UBYTE i, k, w, n = 0;

    tournament_sort(summary.order);
    for (i = 0; i < MAX_RIDERS; i++) {
        const Rider *r = &g_tour.riders[summary.order[i]];
        for (k = 0; k < NEWS_NAME && r->name[k]; k++) summary.name[i][k] = r->name[k];
        summary.name[i][k] = 0;
        summary.points[i] = r->points;
        summary.wins[i] = r->wins;
        summary.human[i] = (BOOL)(r->control == CTRL_HUMAN);
    }
    if (tournaments < 99) tournaments++;
    summary.number = tournaments;

    w = summary.order[0];
    f.title_runoff = st->title_runoff;
    f.winner_owns_record = (BOOL)(st->rec_heat_holder == w || st->rec_lap_holder == w);
    f.winner_clean_sweep = (BOOL)(st->starts[w] > 0 && g_tour.riders[w].wins == st->starts[w] && !st->excluded[w]);
    f.winner_points = summary.points[0];
    f.runner_points = summary.points[1];
    summary.rule = news_pick_rule(&f);

    /* najwyzej dwa fakty w kolejnosci, pomijajac zerowe */
    summary.facts[0] = summary.facts[1] = NEWS_FACT_NONE;
    if (st->false_starts) { summary.facts[n] = NEWS_FACT_FALSE_STARTS; summary.fact_value[n++] = st->false_starts; }
    if (st->exclusions)   { summary.facts[n] = NEWS_FACT_EXCLUSIONS;   summary.fact_value[n++] = st->exclusions; }
    if (n < 2)            { summary.facts[n] = NEWS_FACT_WINNER_WINS;  summary.fact_value[n++] = summary.wins[0]; }
    if (n < 2)            { summary.facts[n] = NEWS_FACT_HUMANS;       summary.fact_value[n++] = g_tour.num_humans; }
}

const NewsSummary *news_summary(void) { return &summary; }
