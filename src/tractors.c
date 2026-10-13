/* tractors.c - rownanie i polewanie toru po biegach 4, 7, 11, 13.
 * Dwa pojazdy jako duszki sprzetowe (motocykli wtedy nie ma na ekranie):
 *   pary 0/1: czerwony traktor z wloka (blizej kraweznika)
 *   pary 2/3: zolty traktor z beczka - zraszacz to drobiny wody z roost.c
 * Tor ciemnieje plynnie (zamiana palety) - patrz GS_TRACK_GRADING w main.c. */
#include <exec/memory.h>
#include <proto/exec.h>

#include "game.h"
#include "video.h"

#define NUM_VEH      2
#define HALF_WORDS   ((SPRITE_H + 2) * 2 * (SPRITE_W / 16))
#define FRAME_WORDS  (HALF_WORDS * 2)
#define VEH_BYTES    ((ULONG)NUM_VEH * SPRITE_DIRS * FRAME_WORDS * 2)
#define SPEED_88     384             /* 1.5 px/klatke (8.8) */
#define LOOK_AHEAD   8

typedef struct { LONG s88; WORD lat; WORD x, y; angle8 heading; } Vehicle;

static UWORD  *veh_mem;
static Vehicle veh[NUM_VEH];

BOOL tractors_init(void)
{
    veh_mem = AllocMem(VEH_BYTES, MEMF_CHIP | MEMF_CLEAR);
    if (!veh_mem) return FALSE;
    if (!asset_load("PROGDIR:data/tractors.spr", veh_mem, VEH_BYTES)) {
        FreeMem(veh_mem, VEH_BYTES);
        veh_mem = 0;                 /* bez pliku: przerwa bez traktorow */
    }
    return TRUE;
}

void tractors_free(void)
{
    if (veh_mem) FreeMem(veh_mem, VEH_BYTES);
}

static void place(Vehicle *v)
{
    WORD ax, ay;
    LONG s = v->s88 >> 8;
    track_point(s, v->lat, &v->x, &v->y);
    track_point(s + LOOK_AHEAD, v->lat, &ax, &ay);
    v->heading = atan2_8(ay - v->y, ax - v->x);
}

/* start z wyjazdu z parku maszyn: tuz za linia startu, beczka 60 px z tylu */
void tractors_begin(void)
{
    LONG s0 = g_tour.track.tape_s + 20;
    veh[0].s88 = s0 << 8;          veh[0].lat = -12;
    veh[1].s88 = (s0 - 60) << 8;   veh[1].lat = 10;
    place(&veh[0]);
    place(&veh[1]);
}

void tractors_update(void)
{
    UBYTE i;
    for (i = 0; i < NUM_VEH; i++) {
        veh[i].s88 += SPEED_88;
        place(&veh[i]);
    }
    /* zraszacz: rampa na koncu beczki, 12 px za srodkiem pojazdu */
    {
        Vehicle *b = &veh[1];
        fix16 x = INT2FIX(b->x) - (fix16)COS(b->heading) * (12 << 2);
        fix16 y = INT2FIX(b->y) - (fix16)SIN(b->heading) * (12 << 2);
        roost_emit_water(x, y, b->heading);
        roost_emit_water(x, y, b->heading);
    }
}

void tractors_render(void)
{
    UBYTE i;
    for (i = 0; i < 8; i++) video_set_sprite(i, g_blank_sprite);
    if (!veh_mem) return;
    for (i = 0; i < NUM_VEH; i++) {
        UWORD *even = veh_mem + ((ULONG)i * SPRITE_DIRS + ANGLE_TO_FRAME(veh[i].heading)) * FRAME_WORDS;
        UWORD *odd  = even + HALF_WORDS;
        sprite_set_pos(even, veh[i].x, veh[i].y, SPRITE_H, FALSE);
        sprite_set_pos(odd,  veh[i].x, veh[i].y, SPRITE_H, TRUE);
        video_set_sprite((UBYTE)(i * 2), even);
        video_set_sprite((UBYTE)(i * 2 + 1), odd);
    }
}
