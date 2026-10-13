/* roost.c - szprycha zuzlowa: drobiny nawierzchni spod tylnego kola.
 *
 * Emisja: motocykl w uslizgu (drift > ROOST_DRIFT) albo mielacy kolem pod tasma.
 * Drobiny leca do tylu i na zewnatrz luku (przy lewym skrecie - w prawo),
 * hamowane oporem powietrza, zyja 12-22 klatek.
 *
 * Rysowanie (Blitter, podwojny bufor):
 *   klatka N, bufor "back":  roost_erase -> zdejmij to, co narysowano w tym
 *                            buforze w klatce N-2 (kopia tla, 1 blit na drobine)
 *                            ui_render   -> nakladki / tasma
 *                            roost_draw  -> cookie-cut aktualnych drobin
 * Kazdy bufor ma osobna liste narysowanych pozycji. */
#include "game.h"
#include "video.h"
#include "blit.h"

#define ROOST_DRIFT     14
#define SPEED_MIN       (FIX_ONE / 2)
#define LIFE_MIN        8
#define LIFE_RAND       8
#define HIT_RADIUS2     (6 * 6)
#define HIT_MIN_LIFE    3
#define REAR_OFFSET     9          /* tylne kolo za srodkiem motocykla [px] */
#define FRONT_OFFSET    5

/* kolory drobin: koleiny i ciemny granit (po polewaniu paleta sama je przyciemnia) */
static const UBYTE dust_colors[4] = { 48, 49, 50, 17 };

typedef struct { WORD x, y; UBYTE big; } Drawn;
static Drawn drawn[2][MAX_PARTICLES];
static UBYTE drawn_n[2];

BOOL roost_init(void) { return blit_init(); }
void roost_free(void) { blit_free(); }

void roost_reset(void)
{
    UBYTE i;
    for (i = 0; i < MAX_PARTICLES; i++) g_tour.particles[i].life = 0;
    g_tour.particle_head = 0;
}

void roost_forget_buffer(UBYTE buf) { drawn_n[buf] = 0; }

/* losowe przesuniecie +-range (16.16) */
static fix16 jitter(fix16 range)
{
    return (fix16)(((LONG)(rng_next() & 0xFFFF) - 0x8000) * (range >> 15));
}

static void emit(UBYTE owner, const Bike *b, BOOL spin)
{
    Particle *p = &g_tour.particles[g_tour.particle_head];
    WORD fc = COS(b->heading), fs = SIN(b->heading);            /* przod, Q14 */
    fix16 back, side;

    g_tour.particle_head = (UBYTE)((g_tour.particle_head + 1) % MAX_PARTICLES);

    p->x = b->x - (fix16)fc * (REAR_OFFSET << 2);                /* Q14 * px<<2 = 16.16 */
    p->y = b->y - (fix16)fs * (REAR_OFFSET << 2);
    if (spin) {
        back = FIX_ONE * 2;                                      /* mielenie: prosto do tylu */
        side = 0;
    } else {
        back = (b->speed >> 1) + (fix16)b->drift * (FIX_ONE / 20);
        side = (fix16)b->drift * (FIX_ONE / 22);                 /* wachlarz na zewnatrz luku */
    }
    back += jitter(FIX_ONE);
    side += jitter(FIX_ONE);
    /* predkosc = -przod * back + prawo * side;  prawo = (-sin, cos) */
    p->dx = (fix16)(((LONG)-fc * (back >> 8) - (LONG)fs * (side >> 8)) >> 6);
    p->dy = (fix16)(((LONG)-fs * (back >> 8) + (LONG)fc * (side >> 8)) >> 6);
    p->life = (UBYTE)(LIFE_MIN + rng_range(0, LIFE_RAND));
    p->color = dust_colors[rng_next() & 3];
    p->big = (UBYTE)((rng_next() & 3) != 0);
    p->owner = owner;
}

#define OWNER_WATER 0xFF
static const UBYTE water_colors[2] = { 214, 215 };

/* Woda z rampy: na boki (obie strony) i lekko do tylu, krotkie zycie.
 * Drobiny wody nie spowalniaja nikogo (owner = OWNER_WATER). */
void roost_emit_water(fix16 x, fix16 y, angle8 heading)
{
    Particle *p = &g_tour.particles[g_tour.particle_head];
    WORD fc = COS(heading), fs = SIN(heading);
    fix16 side = FIX_ONE + (fix16)(rng_next() & 0xFFFF);          /* 1..2 px/klatke */
    fix16 back = FIX_ONE / 4;
    if (rng_next() & 1) side = -side;
    g_tour.particle_head = (UBYTE)((g_tour.particle_head + 1) % MAX_PARTICLES);
    p->x = x;
    p->y = y;
    p->dx = (fix16)(((LONG)-fc * (back >> 8) - (LONG)fs * (side >> 8)) >> 6);
    p->dy = (fix16)(((LONG)-fs * (back >> 8) + (LONG)fc * (side >> 8)) >> 6);
    p->life = (UBYTE)(5 + rng_range(0, 5));
    p->color = water_colors[rng_next() & 1];
    p->big = (UBYTE)(rng_next() & 1);
    p->owner = OWNER_WATER;
}

void roost_update(BOOL bikes)
{
    UBYTE i;

    for (i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g_tour.particles[i];
        if (!p->life) continue;
        p->x += p->dx;
        p->y += p->dy;
        p->dx -= p->dx >> 2;              /* opor: drobina szybko opada na tor */
        p->dy -= p->dy >> 2;
        p->life--;
        if (p->x < 0 || p->y < 0 || FIX2INT(p->x) > SCREEN_W - 17 || FIX2INT(p->y) > PLAY_H - 2)
            p->life = 0;
        else if (p->owner == OWNER_WATER && track_lateral(FIX2INT(p->x), FIX2INT(p->y)) < -TRACK_HALF_WIDTH)
            p->life = 0;             /* woda tylko na nawierzchni, nie na murawie (panel) */
    }
    if (!bikes) return;

    for (i = 0; i < MAX_RIDERS; i++) {
        const Bike *b = &g_tour.bikes[i];
        if (b->state == BIKE_WHEELSPIN) {
            emit(i, b, TRUE);
            emit(i, b, TRUE);
        } else if (b->state == BIKE_RACING && b->drift > ROOST_DRIFT && b->speed > SPEED_MIN) {
            UBYTE n = (UBYTE)(1 + (b->drift - ROOST_DRIFT) / 10);     /* 1..4 na klatke */
            while (n--) emit(i, b, FALSE);
        }
    }
}

/* Drobiny innych zawodnikow w strefie przed motocyklem (jezdziec "jedzie w szprysze"). */
UBYTE roost_hits(const Bike *b)
{
    UBYTE i, hits = 0, me = (UBYTE)(b - g_tour.bikes);
    WORD fx = (WORD)(FIX2INT(b->x) + ((COS(b->heading) * FRONT_OFFSET) >> 14));
    WORD fy = (WORD)(FIX2INT(b->y) + ((SIN(b->heading) * FRONT_OFFSET) >> 14));
    for (i = 0; i < MAX_PARTICLES; i++) {
        const Particle *p = &g_tour.particles[i];
        LONG dx, dy;
        if (p->life < HIT_MIN_LIFE || p->owner == me) continue;
        dx = FIX2INT(p->x) - fx;
        dy = FIX2INT(p->y) - fy;
        if (dx * dx + dy * dy < HIT_RADIUS2) hits++;
    }
    return hits;
}

void roost_erase(void)
{
    UBYTE bi = g_video.back, i;
    UBYTE *buf = g_video.play[bi];
    for (i = 0; i < drawn_n[bi]; i++)
        blit_restore(buf, g_video.background, drawn[bi][i].x, drawn[bi][i].y, drawn[bi][i].big);
    drawn_n[bi] = 0;
}

void roost_draw(void)
{
    UBYTE bi = g_video.back, i, n = 0;
    UBYTE *buf = g_video.play[bi];
    HeatPhase ph = g_tour.heat.phase;

    if (g_tour.state == GS_TRACK_GRADING) ph = HEAT_RACING;     /* zraszacz */
    else if (g_tour.state != GS_HEAT) return;
    if (ph == HEAT_FALSE_START || ph < HEAT_TAPE_UP) return;
    for (i = 0; i < MAX_PARTICLES; i++) {
        const Particle *p = &g_tour.particles[i];
        WORD x, y;
        if (!p->life) continue;
        x = FIX2INT(p->x);
        y = FIX2INT(p->y);
        blit_dot(buf, x, y, p->big, p->color);
        drawn[bi][n].x = x;
        drawn[bi][n].y = y;
        drawn[bi][n].big = p->big;
        n++;
    }
    drawn_n[bi] = n;
}
