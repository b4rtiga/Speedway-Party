/* start_test.c - uczciwosc startu: czterech jednakowych botow (MISTRZ), wiele turniejow na
 * losowych nawierzchniach; wyniki wg pola startowego A-D. Przy rownych szansach kazde pole
 * ma ok. 25% zwyciestw i podobne srednie miejsce w pierwszym luku.
 *   sh tools/sim/start_test.sh [turnieje] */
#include <stdio.h>
#include <stdlib.h>
#include "game.h"

void stubs_init(void);

int main(int argc, char **argv)
{
    int tours = argc > 1 ? atoi(argv[1]) : 40;
    long heats = 0, wins[4] = {0}, bend_pos[4] = {0}, fin_pos[4] = {0};
    double v1s[4] = {0};
    long roost[4] = {0}, kerb[4] = {0}, frames_race[4] = {0}, latsum[4] = {0}, spd[4] = {0};
    int t, h, g;

    stubs_init();
    fixmath_init();
    for (t = 0; t < tours; t++) {
        g_rng = (ULONG)(1000 + t * 7919);
        tournament_defaults();
        g_tour.num_humans = 0;
        tournament_reset();
        for (g = 0; g < 4; g++) g_tour.riders[g].ai_level = AI_MASTER;
        track_init();
        track_random_surface();
        for (h = 0; h < FIXED_HEATS; h++) {
            Heat *ht = &g_tour.heat;
            const Track *tr = &g_tour.track;
            LONG bend = tr->lap_length + 2 * 60 - tr->tape_s;      /* wjazd w 1. luk (koniec prostej, HALF_L 60) */
            int frame = 0, race = -1, got_bend = 0, i, ok = 1;
            UBYTE pos_at_bend[4];
            g_tour.current_heat = (UBYTE)h;
            ht->number = (UBYTE)(h + 1);
            ht->restarts = 0;
            for (g = 0; g < 4; g++) ht->gate_rider[g] = g_tour.heat_table[h][g];
            start_begin();
            while (ht->phase != HEAT_FINISHED && frame < 20000) {
                start_update();
                if (ht->phase == HEAT_TAPE_UP || ht->phase == HEAT_RACING) race_update();
                if (race < 0 && ht->phase == HEAT_TAPE_UP) race = frame;
                if (race >= 0 && getenv("DIAG"))
                    for (g = 0; g < 4; g++) {
                        const Bike *bk = &g_tour.bikes[g];
                        WORD lat;
                        if (bk->state != BIKE_RACING) continue;
                        lat = track_lateral(FIX2INT(bk->x), FIX2INT(bk->y));
                        frames_race[g]++; latsum[g] += lat; spd[g] += bk->speed >> 8;
                        if (bk->in_roost) roost[g]++;
                        if (lat < -TRACK_HALF_WIDTH + 3) kerb[g]++;
                    }
                if (race >= 0 && frame == race + 50)
                    for (g = 0; g < 4; g++) v1s[g] += g_tour.bikes[g].speed / (double)BIKE_MAX_SPEED;
                if (!got_bend && race >= 0) {
                    for (g = 0; g < 4; g++)
                        if (g_tour.bikes[g].race_distance >= bend) got_bend = 1;
                    if (got_bend) for (g = 0; g < 4; g++) pos_at_bend[g] = g_tour.bikes[g].position;
                }
                frame++;
            }
            for (g = 0; g < 4; g++) if (g_tour.bikes[g].state != BIKE_FINISHED) ok = 0;
            tournament_award_points();
            if (!ok || !got_bend) continue;
            heats++;
            for (g = 0; g < 4; g++) {                                  /* bikes[g] = pole g */
                bend_pos[g] += pos_at_bend[g];
                fin_pos[g] += g_tour.bikes[g].position;
                if (g_tour.bikes[g].position == 0) wins[g]++;
            }
            (void)i;
        }
    }
    printf("%ld biegow (4 x MISTRZ, %d turniejow, losowe nawierzchnie)\n", heats, tours);
    printf("pole  v po 1 s   miejsce w 1. luku   miejsce na mecie   zwyciestwa\n");
    for (g = 0; g < 4; g++)
        printf("  %c    %5.1f%%        %4.2f               %4.2f          %5.1f%%\n", 'A' + g,
               100.0 * v1s[g] / heats, 1.0 + (double)bend_pos[g] / heats, 1.0 + (double)fin_pos[g] / heats,
               100.0 * wins[g] / heats);
    if (getenv("DIAG"))
        for (g = 0; g < 4; g++)
            printf("  %c: w szprycy %4.1f%% klatek, przy krawezniku %4.1f%%, srednio %+5.1f px od osi, sr. predkosc %5.1f%%\n", 'A' + g,
                   100.0 * roost[g] / frames_race[g], 100.0 * kerb[g] / frames_race[g], (double)latsum[g] / frames_race[g],
                   100.0 * spd[g] / frames_race[g] / (BIKE_MAX_SPEED >> 8));
    return 0;
}
