/* game.h - glowne struktury danych: zawodnik, motocykl, tor, bieg, turniej. */
#ifndef GAME_H
#define GAME_H

#include <exec/types.h>
#include "fixmath.h"

#define MAX_RIDERS      4
#define NUM_HEATS       15
#define FIXED_HEATS     13        /* biegi 1-13 z tabeli, 14-15 nominowane */
#define LAPS_PER_HEAT   4
#define NAME_LEN        16
#define FPS             50

/* ------------------------------------------------------------------ */
/* Kolory kaskow = pola startowe (jak w prawdziwym zuzlu):            */
/* A=czerwony, B=niebieski, C=bialy, D=zolty. Kask zmienia sie        */
/* w kazdym biegu razem z polem startowym.                            */
/* ------------------------------------------------------------------ */
typedef enum { GATE_A = 0, GATE_B, GATE_C, GATE_D, GATE_NONE = 0xFF } Gate;
typedef enum { HELMET_RED = 0, HELMET_BLUE, HELMET_WHITE, HELMET_YELLOW } HelmetColor;

typedef enum { CTRL_HUMAN = 0, CTRL_AI } ControlType;
typedef enum { AI_AMATEUR = 0, AI_PRO, AI_MASTER } AiLevel;

/* Zrodlo wejscia gracza (2 przyciski: LEWO/PRAWO) */
typedef enum {
    INPUT_JOY1 = 0,     /* port joysticka */
    INPUT_JOY0,         /* port myszy */
    INPUT_JOY2_PAR,     /* adapter 4 graczy na porcie rownoleglym */
    INPUT_JOY3_PAR,
    INPUT_KEYS_ZX,      /* klawiatura: Z=lewo X=prawo C=fire */
    INPUT_KEYS_NM,      /* N=lewo M=prawo ,=fire */
    INPUT_KEYS_ARROWS,  /* strzalki, Return=fire */
    INPUT_NONE
} InputPort;

#define BTN_LEFT   0x01
#define BTN_RIGHT  0x02
#define BTN_FIRE   0x04   /* tylko menu; w biegu nieuzywany */
#define BTN_UP     0x08   /* tylko menu */
#define BTN_DOWN   0x10
#define BTN_DIRS   (BTN_LEFT | BTN_RIGHT)

/* ---------------------------- ZAWODNIK ----------------------------- */
typedef struct Rider {
    char        name[NAME_LEN + 1];  /* edytowalne */
    ControlType control;
    AiLevel     ai_level;            /* uzywane gdy control == CTRL_AI */
    InputPort   input;

    UBYTE       points;              /* suma punktow w turnieju */
    UBYTE       heat_points[NUM_HEATS];  /* 0xFF = nie jechal / wykluczony */
    UBYTE       wins;                /* do rozstrzygania remisow */
    UBYTE       gate_count[4];       /* ile razy startowal z danego pola */
    UBYTE       warnings;            /* ostrzezenia za taśme w biezacym biegu */
    UWORD       best_reaction_ms;    /* statystyka refleksu */
} Rider;

/* --------------------------- MOTOCYKL ------------------------------ */
typedef enum {
    BIKE_AT_GATE = 0,   /* pod tasma, gracz trzyma kierunek */
    BIKE_WHEELSPIN,     /* trzymal za dlugo po zielonym - mieli w miejscu */
    BIKE_RACING,
    BIKE_FINISHED,
    BIKE_EXCLUDED       /* wykluczony (np. druga tasma) */
} BikeState;

typedef struct Bike {
    /* pozycja i ruch (16.16) */
    fix16   x, y;
    fix16   speed;            /* predkosc postepowa (px/klatke) */
    fix16   max_speed;
    angle8  heading;          /* kierunek ramy */
    BYTE    drift;            /* kat uslizgu tylnego kola (-DRIFT_MAX..DRIFT_MAX), + = zlamany w lewo */
    BYTE    drift_target;

    BikeState state;
    UBYTE   rider;            /* indeks w Tournament.riders */
    UBYTE   gate;             /* Gate = kolor kasku w biezacym biegu */
    UBYTE   input;            /* BTN_LEFT|BTN_RIGHT z tej klatki */
    UBYTE   prev_input;

    /* postep w biegu */
    UBYTE   lap;
    UBYTE   waypoint;         /* nastepny waypoint */
    LONG    race_distance;    /* lap*TRACK_LEN + dystans na okrazeniu -> kolejnosc */
    UBYTE   position;         /* 0 = lider */
    UWORD   finish_frame;

    /* start */
    UWORD   reaction_frames;  /* klatki od zielonego do puszczenia */
    UBYTE   launch_quality;   /* 0..255 -> bonus przyspieszenia na starcie */
    UBYTE   launch_timer;     /* klatki pozostalego bonusu startowego */

    /* efekty */
    UBYTE   in_roost;         /* jedzie w szprysze innego motocykla */
    UBYTE   bump_cooldown;    /* zapobiega wielokrotnej kary za ten sam kontakt */

    /* grafika */
    UBYTE   sprite_frame;     /* 0..31 (11.25 st.) */
    UBYTE   skid_frame;       /* 0 = brak, 1..N klatki uslizgu */
    UBYTE   hw_sprite;        /* para duszkow sprzetowych (0,2,4,6) */

    /* AI */
    WORD    ai_reaction;      /* klatki od zielonego do puszczenia; <0 = antycypacja (tasma!) */
    UWORD   ai_timer;
    BYTE    ai_line_offset;   /* preferowany tor jazdy: - krawężnik, + banda */
} Bike;

/* ----------------------- CZASTECZKI (szprycha) --------------------- */
#define MAX_PARTICLES 128
typedef struct Particle {
    fix16 x, y;          /* pozycja 16.16 */
    fix16 dx, dy;        /* predkosc 16.16 px/klatke */
    UBYTE life;          /* 0 = wolna */
    UBYTE color;         /* indeks palety toru (ciemny granit -> po polewaniu jeszcze ciemniejszy) */
    UBYTE big;           /* 1 = 2x2 px, 0 = 1x1 */
    UBYTE owner;         /* indeks motocykla - wlasna szprycha nie spowalnia */
} Particle;

/* ------------------------------ TOR -------------------------------- */
typedef enum { GRIP_INNER = 0, GRIP_MIDDLE, GRIP_OUTER, GRIP_DEEP_OUTER, GRIP_WALL } GripZone;

#define GRIP_CELL      8                  /* komorka mapy przyczepnosci 8x8 px */
#define GRIP_W         (320 / GRIP_CELL)
#define GRIP_H         (208 / GRIP_CELL)
#define MAX_WAYPOINTS  32

typedef struct Waypoint { WORD x, y; BYTE inner_ofs, outer_ofs; } Waypoint;

typedef struct Track {
    UBYTE    zone[GRIP_H][GRIP_W];        /* GripZone */
    UBYTE    grip[GRIP_H][GRIP_W];        /* 0..255 dynamiczna przyczepnosc */
    Waypoint wp[MAX_WAYPOINTS];
    UBYTE    num_wp;
    LONG     lap_length;                  /* jednostki race_distance */
    WORD     tape_x;                      /* tasma: pionowa linia na prostej startowej */
    WORD     tape_y0, tape_y1;
    WORD     gate_x, gate_y[4];           /* A = przy krawezniku, D = przy bandzie */
    LONG     tape_s;                      /* pozycja tasmy wzdluz osi toru */

    UBYTE    wetness;                     /* 255 = swiezo polany, 0 = suchy */
    UBYTE    heats_since_watering;
} Track;

/* --------------------------- BIEG ---------------------------------- */
typedef enum {
    HEAT_PRE = 0,        /* zawodnicy podjezdzaja pod tasme */
    HEAT_READY,          /* czekamy az wszyscy ludzie trzymaja kierunek */
    HEAT_RED_LIGHT,      /* losowe 1.5-3.5 s */
    HEAT_TAPE_UP,        /* zielone, tasma leci w gore (2-3 klatki) */
    HEAT_RACING,
    HEAT_FALSE_START,    /* czerwona flaga, powtorka */
    HEAT_FINISHED
} HeatPhase;

typedef struct Heat {
    UBYTE     number;               /* 1..15 */
    UBYTE     gate_rider[4];        /* gate -> indeks zawodnika */
    UBYTE     finish_order[4];      /* indeks zawodnika w kolejnosci mety */
    UBYTE     finished_count;
    HeatPhase phase;
    UWORD     phase_timer;          /* klatki w biezacej fazie */
    UWORD     red_light_frames;     /* wylosowana dlugosc czerwonego */
    UBYTE     tape_anim;            /* 0=ciagla, 1-2=przerywana, 3=brak */
    UBYTE     false_starter;        /* 0xFF = brak */
    UBYTE     restarts;
    UWORD     race_frames;
} Heat;

/* -------------------------- TURNIEJ -------------------------------- */
typedef enum {
    GS_TITLE = 0,
    GS_SETUP,            /* liczba graczy, nazwiska, poziom AI */
    GS_TABLE,            /* tabela wynikow / program zawodow */
    GS_GATE_PICK,        /* wybor pol w biegach nominowanych */
    GS_HEAT,             /* bieg (procedura startowa + wyscig) */
    GS_HEAT_RESULT,
    GS_TRACK_GRADING,    /* rownanie i polewanie (traktory) */
    GS_PODIUM,
    GS_QUIT
} GameState;

typedef struct Tournament {
    Rider     riders[MAX_RIDERS];
    UBYTE     num_humans;
    UBYTE     heat_table[NUM_HEATS][4];    /* [bieg][pole] -> zawodnik */
    UBYTE     current_heat;                /* 0..14 */
    UBYTE     pick_order[MAX_RIDERS];      /* kolejnosc wyboru pol (14,15) */
    UBYTE     pick_index;
    Heat      heat;
    Track     track;
    Bike      bikes[MAX_RIDERS];
    Particle  particles[MAX_PARTICLES];
    UBYTE     particle_head;               /* bufor kolowy: nowa nadpisuje najstarsza */
    GameState state;
} Tournament;

extern Tournament g_tour;

/* tournament.c */
void tournament_defaults(void);                  /* nazwiska, AI - raz przy starcie programu */
void tournament_reset(void);                     /* punkty i tabela - przed kazdym turniejem */
void tournament_sort(UBYTE order[MAX_RIDERS]);   /* wg punktow, potem zwyciestw */
#define HP_NONE     0xFF                         /* bieg jeszcze nie jechany */
#define HP_EXCLUDED 0xFE                         /* wykluczony (W) */
void tournament_award_points(void);
BOOL tournament_watering_after(UBYTE heat_number);
void tournament_prepare_nominated(void);
void tournament_assign_gate(UBYTE rider, UBYTE gate);
UBYTE tournament_ai_choose_gate(UBYTE rider);

/* start.c */
void start_begin(void);
void start_update(void);   /* wywolywane co klatke w fazach przed HEAT_RACING */

/* race.c */
void race_init_heat(void);
void race_update(void);
void race_launch(Bike *b);

/* track.c */
void     track_init(void);
void     track_render(UBYTE *dst);
UBYTE    track_grip_at(WORD x, WORD y);
GripZone track_zone_at(WORD x, WORD y);
LONG     track_progress(WORD x, WORD y);          /* 0..lap_length */
void     track_point(LONG s, WORD lateral, WORD *x, WORD *y);
WORD     track_lateral(WORD x, WORD y);           /* odleglosc od osi: - do kraweznika, + do bandy */
void     track_snap_lateral(fix16 *x, fix16 *y, WORD lat);
angle8   track_tangent(fix16 x, fix16 y);
void     track_apply_watering(void);
void     track_dry_step(void);
angle8   atan2_8(LONG dy, LONG dx);
#define TRACK_HALF_WIDTH 28        /* (R_OUT - R_IN) / 2 z track.c */

/* input.c */
void  input_init(void);
void  input_update(void);                         /* raz na klatke */
UBYTE input_read(InputPort port);
BOOL  key_down(UBYTE rawkey);
UBYTE key_get(void);                              /* nastepny wcisniety klawisz lub 0xFF */
UBYTE input_menu(void);                           /* nowe wcisniecia: JOY1|strzalki|Return (bez myszy w porcie 0) */
#define KEY_RETURN    0x44
#define KEY_BACKSPACE 0x41
#define KEY_LALT      0x64
#define KEY_RALT      0x65
#define KEY_ESC 0x45

/* roost.c - szprycha (czasteczki Blittera) */
BOOL  roost_init(void);
void  roost_free(void);
void  roost_reset(void);                          /* nowy bieg / powtorka startu */
void  roost_update(BOOL bikes);                   /* ruch drobin; bikes = emisja spod kol */
void  roost_emit_water(fix16 x, fix16 y, angle8 heading);   /* zraszacz beczki */
UBYTE roost_hits(const Bike *b);                  /* ile cudzych drobin trafia w motocykl */
void  roost_erase(void);                          /* przed ui_render: zdejmij stare z "back" */
void  roost_draw(void);                           /* po ui_render: narysuj aktualne w "back" */
void  roost_forget_buffer(UBYTE buf);             /* bufor przerysowany w calosci */

/* tractors.c - przerwa na tor */
BOOL tractors_init(void);
void tractors_free(void);
void tractors_begin(void);
void tractors_update(void);
void tractors_render(void);

/* assets.c - pliki z PROGDIR:data/ (tylko przed przejeciem sprzetu) */
BOOL asset_load(const char *name, void *dst, LONG size);
void *asset_load_alloc(const char *name, ULONG *size, ULONG memflags);   /* NULL gdy brak */

/* audio.c - Paula */
BOOL audio_init(void);                            /* przed przejeciem (ladowanie probek) */
void audio_free(void);
void audio_start(void);                           /* po przejeciu */
void audio_stop(void);                            /* przed oddaniem systemu */
void audio_update(void);                          /* raz na klatke */
void sfx_buzzer(void);
void sfx_whistle(void);

/* hud.c */
void hud_init(void);
void hud_update(void);

/* ai.c */
UBYTE ai_steer(Bike *b);
WORD  ai_reaction_frames(AiLevel lvl);

#endif
