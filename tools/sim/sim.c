/* sim.c - symulator turnieju bez grafiki: te same pliki logiki co gra (race/start/ai/
 * tournament/track/roost). 4 boty, 15 biegow, statystyki + slad biegu 1 do CSV.
 * Budowanie i uruchomienie: sh tools/sim/run.sh [biegi] [ziarno] [tor 0-2] */
#include <stdio.h>
#include <stdlib.h>
#include "game.h"

void stubs_init(void);

static const char *lvl_name[3] = { "NOWICJ", "PROFES", "MISTRZ" };

int main(int argc, char **argv)
{
    int heats = argc > 1 ? atoi(argv[1]) : NUM_HEATS;
    FILE *trace = fopen("tools/sim/trace.csv", "w");
    int h, i;
    long total_frames = 0;

    stubs_init();
    fixmath_init();
    g_rng = argc > 2 ? (ULONG)atol(argv[2]) : 12345;
    tournament_defaults();
    g_tour.num_humans = 0;
    tournament_reset();
    g_tour.riders[0].ai_level = AI_AMATEUR;
    g_tour.riders[1].ai_level = AI_PRO;
    g_tour.riders[2].ai_level = AI_MASTER;
    g_tour.riders[3].ai_level = AI_PRO;
    track_init();
    track_random_surface();                 /* jak w grze: losowe sciezki na start turnieju */
    fprintf(trace, "frame,bike,gate,x,y,speed,drift,lap,state\n");

    for (h = 0; h < heats; h++) {
        Heat *ht = &g_tour.heat;
        int frame = 0, wall[4] = {0}, roost[4] = {0}, falsestarts = 0;
        long dsum[4] = {0}, ssum[4] = {0}, n[4] = {0};
        fix16 prev[4];

        g_tour.current_heat = (UBYTE)h;
        if (h >= FIXED_HEATS) {
            tournament_prepare_nominated();
            for (i = 0; i < 4; i++) {
                UBYTE r = g_tour.pick_order[i];
                tournament_assign_gate(r, tournament_ai_choose_gate(r));
            }
        }
        ht->number = (UBYTE)(h + 1);
        ht->restarts = 0;
        for (i = 0; i < 4; i++) ht->gate_rider[i] = g_tour.heat_table[h][i];
        start_begin();
        for (i = 0; i < 4; i++) prev[i] = 0;

        while (ht->phase != HEAT_FINISHED && frame < 20000) {
            HeatPhase before = ht->phase;
            start_update();
            if (ht->phase == HEAT_FALSE_START && before != HEAT_FALSE_START) falsestarts++;
            if (ht->phase == HEAT_TAPE_UP || ht->phase == HEAT_RACING) race_update();
            for (i = 0; i < 4; i++) {
                Bike *b = &g_tour.bikes[i];
                if (b->state == BIKE_RACING) {
                    if (prev[i] && b->speed < prev[i] - prev[i] / 6) wall[i]++;
                    if (b->in_roost) roost[i]++;
                    dsum[i] += b->drift < 0 ? -b->drift : b->drift;
                    ssum[i] += b->speed;
                    n[i]++;
                }
                prev[i] = b->speed;
                if (h == 0 && (frame & 1) == 0)
                    fprintf(trace, "%d,%d,%d,%d,%d,%.3f,%d,%d,%d\n", frame, i, b->gate,
                            FIX2INT(b->x), FIX2INT(b->y), b->speed / 65536.0, b->drift, b->lap, b->state);
            }
            frame++;
        }
        total_frames += frame;
        tournament_award_points();

        {
            long sum = 0, cnt = 0; int mx = 0, gx, gy;
            for (gy = 0; gy < GRIP_H; gy++)
                for (gx = 0; gx < GRIP_W; gx++)
                    if (g_tour.track.zone[gy][gx] != GRIP_WALL) {
                        sum += g_tour.track.pack[gy][gx]; cnt++;
                        if (g_tour.track.pack[gy][gx] > mx) mx = g_tour.track.pack[gy][gx];
                    }
            printf("Bieg %2d  (taśmy: %d, czas %5.1f s, wilgotność %3d, ubicie śr. %3ld maks. %3d)\n", h + 1,
                   falsestarts, ht->race_frames / 50.0, g_tour.track.wetness, cnt ? sum / cnt : 0, mx);
        }
        for (i = 0; i < 4; i++) {
            Bike *b = &g_tour.bikes[i];
            Rider *r = &g_tour.riders[b->rider];
            int place = -1, k;
            for (k = 0; k < ht->finished_count; k++) if (ht->finish_order[k] == b->rider) place = k + 1;
            printf("   pole %c %-11s %s  miejsce %s  meta %5.1f s  reakcja %3d ms  śr.v %.2f  śr.|drift| %4.1f  banda %3d  szpryca %4d kl.\n",
                   'A' + i, r->name, lvl_name[r->ai_level],
                   place > 0 ? (char[]){(char)('0' + place), 0} : (b->state == BIKE_EXCLUDED ? "W" : "-"),
                   place > 0 ? b->finish_frame / 50.0 : 0.0, b->reaction_frames * 20,
                   n[i] ? ssum[i] / (double)n[i] / 65536.0 : 0.0, n[i] ? dsum[i] / (double)n[i] : 0.0,
                   wall[i], roost[i]);
        }
        if (tournament_watering_after((UBYTE)(h + 1))) track_apply_watering();
        else track_dry_step();
    }

    /* remis o podium -> bieg(i) dodatkowy(e), jak w grze */
    {
        int guard = 0;
        while (heats == NUM_HEATS && tournament_prepare_runoff() && guard++ < 4) {
            Heat *ht = &g_tour.heat;
            int frame = 0, k;
            g_tour.current_heat = RUNOFF_HEAT;
            ht->number = RUNOFF_HEAT + 1;
            ht->restarts = 0;
            for (i = 0; i < 4; i++) ht->gate_rider[i] = g_tour.heat_table[RUNOFF_HEAT][i];
            start_begin();
            while (ht->phase != HEAT_FINISHED && frame < 20000) {
                start_update();
                if (ht->phase == HEAT_TAPE_UP || ht->phase == HEAT_RACING) race_update();
                frame++;
            }
            tournament_award_points();
            printf("BIEG DODATKOWY (remis po %d pkt):", g_tour.riders[g_tour.heat_table[RUNOFF_HEAT][0]].points);
            for (k = 0; k < ht->finished_count; k++) printf(" %d.%s", k + 1, g_tour.riders[ht->finish_order[k]].name);
            for (i = 0; i < 4; i++)
                if (g_tour.bikes[i].empty) printf(" [pole %c puste]", 'A' + i);
            printf("\n");
        }
    }

    printf("\nKLASYFIKACJA KOŃCOWA\n");
    {
        UBYTE order[4];
        tournament_sort(order);
        for (i = 0; i < 4; i++) {
            Rider *r = &g_tour.riders[order[i]];
            printf("  %d. %-11s %s %2d pkt  %d zw.\n", i + 1, r->name, lvl_name[r->ai_level], r->points, r->wins);
        }
    }
    printf("średnio %.1f s na bieg (z procedurą startową)\n", total_frames / 50.0 / heats);
    {
        FILE *w = fopen("tools/sim/wear.csv", "w");
        int gx, gy;
        for (gy = 0; gy < GRIP_H; gy++) {
            for (gx = 0; gx < GRIP_W; gx++) fprintf(w, gx ? ",%d" : "%d", g_tour.track.pack[gy][gx]);
            fprintf(w, "\n");
        }
        fclose(w);
    }
    fclose(trace);
    return 0;
}
