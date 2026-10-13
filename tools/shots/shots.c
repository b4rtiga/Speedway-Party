/* shots.c - zrzuty ekranu z PRAWDZIWEGO kodu gry (ui.c, hud.c, text.c, sprites.c,
 * tractors.c, roost.c + logika biegu) skompilowanego na PC. Obraz skladany jak w AGA:
 * bitplany 8 + paleta, kolory per linia z Coppera (kolor 0 i 100), duszki
 * zdekodowane z bajtow sprzetowych (POS/CTL + dane), HUD 5 bitplanow + gradient.
 * Wynik: tools/shots/out/ (PPM) -> python tools/shots/topng.py -> art/screen_NN.png */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "video.h"
#include "ui.h"

extern RGB24 cur_play[256], cur_hud[32];
extern UWORD cap_bg[FULL_H], cap_fg[FULL_H];
extern const UWORD *cap_spr[8];
extern volatile ULONG g_frame;
void host_init(void);
UWORD g_grading_progress;

static UBYTE rgb[256][320][3];

static void set12(UBYTE *o, UWORD c) { o[0] = (UBYTE)(((c >> 8) & 15) * 17); o[1] = (UBYTE)(((c >> 4) & 15) * 17); o[2] = (UBYTE)((c & 15) * 17); }
static void set24(UBYTE *o, const RGB24 *c) { o[0] = c->r; o[1] = c->g; o[2] = c->b; }

static UBYTE planar_px(const UBYTE *buf, int stride, int depth, int x, int y)
{
    UBYTE c = 0;
    int p;
    for (p = 0; p < depth; p++)
        if (buf[y * stride + p * ROW_BYTES + (x >> 3)] & (0x80 >> (x & 7))) c |= (UBYTE)(1 << p);
    return c;
}

/* dane duszkow z pliku sa big-endian (jak na Amidze); naglowek POS/CTL pisze C natywnie */
static UWORD be(UWORD w) { return (UWORD)((w >> 8) | (w << 8)); }

static void draw_sprites(void)
{
    int n, W = SPRITE_W / 16;
    for (n = 0; n < 8; n += 2) {
        const UWORD *ev = cap_spr[n], *od = cap_spr[n + 1];
        int hs, vs, ve, x, y, r;
        if (!ev || ev == g_blank_sprite) continue;
        hs = ((ev[0] & 0xFF) << 1) | (ev[W] & 1);
        vs = (ev[0] >> 8) | (((ev[W] >> 2) & 1) << 8);
        ve = (ev[W] >> 8) | (((ev[W] >> 1) & 1) << 8);
        if (!(od[W] & 0x80)) fprintf(stderr, "UWAGA: duszek %d bez bitu ATT\n", n + 1);
        x = hs - 0x80;
        y = vs - DISPLAY_VSTART;
        for (r = 0; r < ve - vs; r++) {
            const UWORD *le = ev + 2 * W + r * 2 * W, *lo = od + 2 * W + r * 2 * W;
            int px;
            for (px = 0; px < SPRITE_W; px++) {
                int wi = px >> 4, bit = 15 - (px & 15), c;
                c = ((be(le[wi]) >> bit) & 1) | (((be(le[W + wi]) >> bit) & 1) << 1)
                  | (((be(lo[wi]) >> bit) & 1) << 2) | (((be(lo[W + wi]) >> bit) & 1) << 3);
                if (c && x + px >= 0 && x + px < 320 && y + r >= 0 && y + r < 256)
                    set24(rgb[y + r][x + px], &cur_play[240 + c]);
            }
        }
    }
}

static void shot(const char *name, BOOL sprites)
{
    char path[128];
    FILE *f;
    int x, y;
    const UBYTE *pb = g_video.play[g_video.back];

    /* menu: cale 256 linii z bitplanow (Copper-lista bez HUD) */
    for (y = 0; y < (g_video.menu ? FULL_H : PLAY_H); y++)
        for (x = 0; x < 320; x++) {
            UBYTE c = planar_px(pb, PLAY_STRIDE, PLAY_DEPTH, x, y);
            if (c == 0) set12(rgb[y][x], cap_bg[y]);
            else if (c == 100) set12(rgb[y][x], cap_fg[y]);
            else set24(rgb[y][x], &cur_play[c]);
        }
    for (y = 0; y < HUD_H && !g_video.menu; y++)
        for (x = 0; x < 320; x++) {
            UBYTE *o = rgb[PLAY_H + y][x];
            UBYTE c;
            int t;
            if (y < 4) { set24(o, &cur_hud[0]); continue; }      /* przerwa: BPLCON0 bez planow */
            c = planar_px(g_video.hud, HUD_STRIDE, HUD_DEPTH, x, y - 4);
            t = (y - 4) * 15 / (HUD_H - 4);
            if (c == 0) set12(o, (UWORD)((1 << 8) | ((1 + t / 3) << 4) | (4 + t / 2)));
            else set24(o, &cur_hud[c]);
        }
    if (sprites) draw_sprites();
    snprintf(path, sizeof(path), "tools/shots/out/%s.ppm", name);
    f = fopen(path, "wb");
    fprintf(f, "P6\n320 256\n255\n");
    fwrite(rgb, 1, sizeof(rgb), f);
    fclose(f);
}

static void render(void) { ui_invalidate(); roost_erase(); ui_render(); roost_draw(); hud_update(); }

static void bike_sprites(void)
{
    UBYTE i;
    for (i = 0; i < 8; i++) video_set_sprite(i, g_blank_sprite);
    for (i = 0; i < MAX_RIDERS; i++) {
        Bike *b = &g_tour.bikes[i];
        UWORD *ev, *od;
        if (b->state == BIKE_EXCLUDED) continue;
        ev = sprite_frame_ptr(b->gate, b->sprite_frame, b->skid_frame, 0);
        od = sprite_frame_ptr(b->gate, b->sprite_frame, b->skid_frame, 1);
        sprite_set_pos(ev, FIX2INT(b->x), FIX2INT(b->y), SPRITE_H, FALSE);
        sprite_set_pos(od, FIX2INT(b->x), FIX2INT(b->y), SPRITE_H, TRUE);
        video_set_sprite(b->hw_sprite, ev);
        video_set_sprite((UBYTE)(b->hw_sprite + 1), od);
    }
}

static void begin(UBYTE hn)
{
    UBYTE g;
    g_tour.current_heat = hn;
    g_tour.heat.number = (UBYTE)(hn + 1);
    g_tour.heat.restarts = 0;
    for (g = 0; g < 4; g++) g_tour.heat.gate_rider[g] = g_tour.heat_table[hn][g];
    start_begin();
}

static void step(void)
{
    start_update();
    if (g_tour.heat.phase == HEAT_TAPE_UP || g_tour.heat.phase == HEAT_RACING) race_update();
}

static void run_heat(UBYTE hn)
{
    begin(hn);
    while (g_tour.heat.phase != HEAT_FINISHED) step();
    g_tour.new_records = records_check_heat();
    tournament_award_points();
    if (tournament_watering_after((UBYTE)(hn + 1))) track_apply_watering(); else track_dry_step();
}

static void nominate_all(void)
{
    UBYTE k;
    tournament_prepare_nominated();
    for (k = 0; k < 4; k++)
        tournament_assign_gate(g_tour.pick_order[k], tournament_ai_choose_gate(g_tour.pick_order[k]));
}

int main(void)
{
    static const char test_name[] = "MAREK \x83" "OPATA";   /* test polskiej litery Ł */
    int i;

    host_init();
    fixmath_init();
    sprites_init();
    roost_init();
    tractors_init();
    tournament_defaults();
    g_tour.num_humans = 0;            /* boty - zeby biegi same sie rozgrywaly */
    strcpy(g_tour.riders[0].name, test_name);
    tournament_reset();
    track_init();
    ui_init();
    hud_init();
    g_frame = 16;                     /* migajace napisy widoczne */

    g_tour.state = GS_TITLE;   render(); shot("01_title", FALSE);
    g_tour.num_humans = 2;
    g_tour.state = GS_SETUP;   render(); shot("02_setup", FALSE);
    g_tour.num_humans = 0;

    for (i = 0; i < 3; i++) run_heat((UBYTE)i);
    /* suchy tor po 3 biegach: widac wyjezdzone sciezki */
    begin(3);
    g_tour.state = GS_HEAT;
    while (g_tour.heat.phase != HEAT_RACING) step();
    for (i = 0; i < 330; i++) step();
    bike_sprites(); render(); shot("13_sciezki", TRUE);
    while (g_tour.heat.phase != HEAT_FINISHED) step();
    tournament_award_points();
    if (tournament_watering_after(4)) track_apply_watering();
    g_tour.current_heat = 4;
    g_tour.state = GS_TABLE;   render(); shot("03_table", FALSE);

    begin(4);                         /* bieg 5: czerwone swiatlo pod tasma */
    g_tour.state = GS_HEAT;
    while (g_tour.heat.phase != HEAT_RED_LIGHT) step();
    bike_sprites(); render(); hud_update(); shot("04_gate_red", TRUE);

    while (g_tour.heat.phase != HEAT_TAPE_UP) step();   /* taśma w trakcie odlotu */
    step(); step(); step();
    bike_sprites(); render(); hud_update(); shot("04b_tape_up", TRUE);
    while (g_tour.heat.phase != HEAT_RACING) step();
    for (i = 0; i < 700; i++) step();
    bike_sprites(); render(); hud_update(); shot("05_racing", TRUE);

    {
        HeatPhase ph = g_tour.heat.phase;
        g_tour.heat.phase = HEAT_FALSE_START;
        g_tour.heat.false_starter = 1;
        g_tour.heat.restarts = 1;
        g_tour.heat.phase_timer = 8;
        render(); hud_update(); shot("06_false_start", FALSE);
        g_tour.heat.phase = ph;
    }

    while (g_tour.heat.phase != HEAT_FINISHED) step();
    g_tour.new_records = records_check_heat();
    tournament_award_points();
    g_frame = 0;                      /* faza migania: napis o rekordzie */
    g_tour.state = GS_HEAT_RESULT; render(); hud_update(); shot("07_result", FALSE);
    g_frame = 16;

    g_tour.state = GS_TRACK_GRADING;
    roost_reset();
    tractors_begin();
    for (i = 0; i < 140; i++) { tractors_update(); roost_update(FALSE); }
    g_grading_progress = 110;
    video_blend_track_palette(140);
    render(); tractors_render(); shot("08_grading", TRUE);
    video_blend_track_palette(g_tour.track.wetness);

    for (i = 5; i < FIXED_HEATS; i++) run_heat((UBYTE)i);
    g_tour.current_heat = FIXED_HEATS;
    tournament_prepare_nominated();
    tournament_assign_gate(g_tour.pick_order[0], tournament_ai_choose_gate(g_tour.pick_order[0]));
    g_tour.state = GS_GATE_PICK; render(); shot("09_gate_pick", FALSE);

    for (i = FIXED_HEATS; i < NUM_HEATS; i++) {
        g_tour.current_heat = (UBYTE)i;
        nominate_all();
        run_heat((UBYTE)i);
    }
    /* remis o 2. miejsce -> program biegu dodatkowego */
    {
        UBYTE order[MAX_RIDERS], k;
        tournament_sort(order);
        g_tour.riders[order[2]].points = g_tour.riders[order[1]].points;
        for (k = 0; k < MAX_RIDERS; k++) g_tour.runoff_rank[k] = RUNOFF_NONE;
        if (tournament_prepare_runoff()) {
            g_tour.current_heat = RUNOFF_HEAT;
            g_tour.state = GS_TABLE; render(); shot("14_dodatkowy", FALSE);
            begin(RUNOFF_HEAT);
            g_tour.state = GS_HEAT;
            while (g_tour.heat.phase != HEAT_FINISHED) step();
            tournament_award_points();
            g_tour.state = GS_HEAT_RESULT; render(); shot("15_dodatkowy_wynik", FALSE);
        }
    }
    g_tour.state = GS_PODIUM; render(); shot("10_podium", FALSE);

    /* awaria maszyny startowej: start z flagi narodowej */
    track_select(0);
    begin(0);
    g_tour.heat.emergency = 1;
    g_tour.state = GS_HEAT;
    while (g_tour.heat.phase != HEAT_RED_LIGHT) step();
    bike_sprites(); render(); hud_update(); shot("16_awaria", TRUE);

    /* pozostale tory: bieg w trakcie */
    {
        static const char *names[NUM_TRACKS] = { "05_racing", "11_bakalar", "12_piasek" };
        UBYTE t;
        for (t = 1; t < NUM_TRACKS; t++) {
            track_select(t);
            track_set_wetness(0);
            begin(0);
            g_tour.state = GS_HEAT;
            while (g_tour.heat.phase != HEAT_RACING) step();
            for (i = 0; i < 560; i++) step();
            bike_sprites(); render(); shot(names[t], TRUE);
        }
        track_select(0);
    }
    printf("zrzuty: tools/shots/out/\n");
    return 0;
}
