/* game.h - glowne struktury danych: zawodnik, motocykl, tor, bieg, turniej. */
#ifndef GAME_H
#define GAME_H

#include <exec/types.h>
#include "fixmath.h"

/* Napisy w dwoch jezykach obok siebie: T("po polsku", "in English"). Jezyk wybiera kompilacja:
 * domyslnie polski, -DLANG_EN angielski (make: SpeedwayParty_PL i SpeedwayParty_EN). */
#ifdef LANG_EN
#define T(pl, en) en
#else
#define T(pl, en) pl
#endif

/* Rzadko wykonywany kod (inicjalizacja, ekrany menu, ustawienia, tabele, instrukcja, gazeta,
 * nakladki z wynikami): GCC kompiluje go na rozmiar i nie wkleja w goraca petle - kod biegu
 * zostaje optymalizowany na szybkosc. */
#define COLD __attribute__((cold))

#define MAX_RIDERS      4
#define NUM_HEATS       15
#define FIXED_HEATS     13        /* biegi 1-13 z tabeli, 14-15 nominowane */
#define RUNOFF_HEAT     NUM_HEATS /* indeks biegu dodatkowego (remis o podium) */
#define RUNOFF_NONE     0xFF
#define LAPS_PER_HEAT   4

/* model jazdy (race.c, ai.c, audio.c) */
#define BIKE_MAX_SPEED  ((fix16)(FIX_ONE * 295 / 100)) /* 2.95 px/klatke = 147 px/s */
#define TURN_K          22        /* skret [1/256 jedn. kata/klatke] na jednostke zlamania przy v maks.;
                                     promien przy pelnym zlamaniu ~45 px */
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
    BIKE_EXCLUDED,      /* wykluczony (np. druga tasma) */
    BIKE_DEFECT,        /* defekt: silnik zgasl, zawodnik nie konczy biegu (0 pkt, "D") */
    BIKE_FALLEN         /* upadek: lezy na torze; bieg przerwany i powtorzony bez niego (W) */
} BikeState;

typedef struct Bike {
    /* pozycja i ruch (16.16) */
    fix16   x, y;
    fix16   speed;            /* predkosc postepowa (px/klatke) */
    fix16   max_speed;
    angle8  heading;          /* kierunek ramy (to widac na ekranie) */
    UWORD   course88;         /* kierunek jazdy, kat 8.8 */
    BYTE    drift;            /* kat zlamania ramy do kierunku jazdy (-10..28 = -14..40 st.), + = w lewo */
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

    /* defekt (race.c): wylosowana chwila awarii i licznik krztuszenia sie / postoju */
    UWORD   defect_frame;     /* klatka biegu, w ktorej silnik zaczyna sie krztusic; 0 = bez defektu */
    UBYTE   defect_timer;     /* 0 = sprawny; krztuszenie: 1..DEFECT_SPUTTER, potem (BIKE_DEFECT) postoj */
    WORD    park_x, park_y;   /* defekt: miejsce na murawie, gdzie motocykl sie zatrzyma */

    /* efekty */
    UBYTE   in_roost;         /* jedzie w szprycy innego motocykla */
    UBYTE   bump_cooldown;    /* zapobiega wielokrotnej kary za ten sam kontakt */

    /* grafika */
    UBYTE   sprite_frame;     /* 0..31 (11.25 st.) */
    UBYTE   skid_frame;       /* 0 = brak, 1..N klatki uslizgu */
    UBYTE   hw_sprite;        /* para duszkow sprzetowych (0,2,4,6) */
    UBYTE   empty;            /* puste pole startowe (bieg dodatkowy) - motocykla nie ma */
    UWORD   lap_start;        /* klatka przeciecia linii na poczatku biezacego okrazenia */
    UWORD   best_lap;         /* najlepsze okrazenie w tym biegu [klatki], 0 = brak */
    UWORD   last_lap;         /* ostatnie pelne okrazenie [klatki] (trening), 0 = brak */

    /* AI */
    WORD    ai_reaction;      /* klatki od zielonego do puszczenia; <0 = antycypacja (tasma!) */
    UWORD   ai_timer;
    BYTE    ai_line_offset;   /* preferowany tor jazdy: - krawężnik, + banda */
    BYTE    ai_target;        /* docelowy uslizg (przeliczany co 4 klatki - oszczednosc CPU) */
    BYTE    ai_line_bias;     /* przesuniecie ulubionej linii w tym biegu (-10..10 px) - rozne boty, rozne linie */
    UBYTE   fence_rub;        /* ocieranie o bande: +2 na klatke kontaktu, -1 bez (race.c, upadki) */
} Bike;

/* ----------------------- CZASTECZKI (szpryca) --------------------- */
#define MAX_PARTICLES 24          /* drobina = kilkadziesiat dostepow do Chip RAM (~1 us kazdy); 64 - ok. 170 z 313 linii klatki, THEA500 Mini nie nadazal przy 40 */
typedef struct Particle {
    fix16 x, y;          /* pozycja 16.16 */
    fix16 dx, dy;        /* predkosc 16.16 px/klatke */
    UBYTE life;          /* 0 = wolna */
    UBYTE color;         /* indeks palety toru (ciemny granit -> po polewaniu jeszcze ciemniejszy) */
    UBYTE big;           /* 1 = 2x2 px, 0 = 1x1 */
    UBYTE owner;         /* indeks motocykla - wlasna szpryca nie spowalnia */
} Particle;

/* ------------------------------ TOR -------------------------------- */
typedef enum { GRIP_INNER = 0, GRIP_MIDDLE, GRIP_OUTER, GRIP_DEEP_OUTER, GRIP_WALL } GripZone;

#define NUM_TRACKS     1                  /* owal z bakalaru (track.c: track_defs, genart.py: PRESETS); kod gotowy na wiecej */
#define GRIP_CELL      8                  /* komorka mapy przyczepnosci 8x8 px */
#define GRIP_W         (320 / GRIP_CELL)
#define GRIP_H         (208 / GRIP_CELL)
#define MAX_WAYPOINTS  32

typedef struct Waypoint { WORD x, y; BYTE inner_ofs, outer_ofs; } Waypoint;

typedef struct Track {
    UBYTE    zone[GRIP_H][GRIP_W];        /* GripZone */
    UBYTE    grip[GRIP_H][GRIP_W];        /* 0..255 przyczepnosc (strefa + wilgoc + ubicie) */
    UBYTE    pack[GRIP_H][GRIP_W];        /* 0..255 ubicie: wyjezdzona sciezka trzyma lepiej */
    Waypoint wp[MAX_WAYPOINTS];
    UBYTE    num_wp;
    LONG     lap_length;                  /* jednostki race_distance */
    WORD     tape_x;                      /* tasma: pionowa linia na prostej startowej */
    WORD     tape_y0, tape_y1;
    WORD     gate_x, gate_y[4];           /* A = przy krawezniku, D = przy bandzie */
    LONG     tape_s;                      /* pozycja tasmy wzdluz osi toru */

    UBYTE    type;                        /* rodzaj toru 0..NUM_TRACKS-1 */
    WORD     half_width;                  /* polowa szerokosci nawierzchni [px] */
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
    HEAT_FINISHED,
    HEAT_REPLAY          /* fotofinisz: powtorka konca biegu (main.c) */
} HeatPhase;

#define TAPE_GONE 8                 /* klatki animacji odlotu tasmy (0.16 s) */

typedef struct Heat {
    UBYTE     number;               /* 1..15 */
    UBYTE     gate_rider[4];        /* gate -> indeks zawodnika */
    UBYTE     finish_order[4];      /* indeks zawodnika w kolejnosci mety */
    UBYTE     order[MAX_RIDERS];    /* indeks motocykla na miejscu 0..3 (race.c; wykluczeni na koncu) */
    UBYTE     finished_count;
    HeatPhase phase;
    UWORD     phase_timer;          /* klatki w biezacej fazie */
    UWORD     red_light_frames;     /* wylosowana dlugosc czerwonego */
    UBYTE     tape_anim;            /* 0=ciagla, 1..TAPE_GONE-1 = faza odlotu, TAPE_GONE = brak */
    UBYTE     false_starter;        /* 0xFF = brak */
    UBYTE     restarts;
    UWORD     race_frames;
    UBYTE     danger_timer;         /* >0: zolta flaga (ktos uderzyl w bande) */
    UBYTE     emergency;            /* awaria maszyny startowej: start z flagi narodowej */
    UBYTE     stop_fall;            /* przerwanie (HEAT_FALSE_START) z powodu upadku, nie tasmy */
} Heat;

/* ------------------------- REKORDY -------------------------------- */
typedef struct TrackRecord {
    UWORD heat_frames;                   /* najlepszy czas biegu [klatki 1/50 s], 0 = brak */
    UWORD lap_frames;                    /* najlepsze okrazenie */
    char  heat_name[NAME_LEN + 1];
    char  lap_name[NAME_LEN + 1];
} TrackRecord;
#define REC_HEAT 1
#define REC_LAP  2

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
    GS_PRESENT,          /* prezentacja przed biegiem: podprowadzajace z tablicami w kolorach pol */
    GS_MENU,             /* po planszy tytulowej: TURNIEJ / CREDITS */
    GS_CREDITS,          /* plansza TWORCY */
    GS_HELP,             /* INSTRUKCJA: zasady, start, idealna linia (3 strony) */
    GS_NEWS,             /* gazeta po zawodach (po podium) */
    GS_RECORDS,          /* tabela rekordow toru (z ekranu wyboru) */
    GS_NAME_ENTRY,         /* wpisywanie imienia po rekordzie (gracz w TOP 5) */
    GS_OPTIONS,          /* opcje turnieju (z ustawien): stan toru, defekty, muzyka */
    GS_SERIES,           /* mistrzostwa: klasyfikacja generalna po rundzie */
    GS_TRAIN_OPTS,       /* trening: tor, stan toru, pora, deszcz - przed startem */
    GS_QUIT
} GameState;

/* Fakty zawodow do gazety: liczone raz, w miejscu zdarzenia; zerowane w tournament_reset. */
typedef struct TourStats {
    UBYTE false_starts;                    /* tasmy (falstarty) w calym turnieju */
    UBYTE exclusions;                      /* wykluczenia za druga tasme */
    UBYTE defects;                         /* defekty (awarie motocykli) w calym turnieju */
    UBYTE falls;                           /* upadki */
    UBYTE defected[MAX_RIDERS];            /* zawodnik mial juz defekt - najwyzej jeden na turniej */
    UBYTE starts[MAX_RIDERS];              /* starty w biegach punktowanych */
    UBYTE excluded[MAX_RIDERS];            /* wykluczenia zawodnika */
    UBYTE records;                         /* pobite rekordy toru (bieg + okrazenie) */
    UBYTE rec_heat_holder, rec_lap_holder; /* kto trzyma rekord pobity w tych zawodach (0xFF = nikt) */
    BOOL  title_runoff;                    /* bieg dodatkowy o 1. miejsce */
    BOOL  other_runoff;                    /* bieg dodatkowy o 2.-3. miejsce */
} TourStats;

#define SERIES_ROUNDS 3                    /* mistrzostwa: rundy na kolejnych torach */

typedef struct Tournament {
    Rider     riders[MAX_RIDERS];
    UBYTE     num_humans;
    UBYTE     heat_table[NUM_HEATS + 1][4];/* [bieg][pole] -> zawodnik (0xFF = puste pole); +1 = bieg dodatkowy */
    UBYTE     runoff_rank[MAX_RIDERS];     /* miejsce w biegu dodatkowym (RUNOFF_NONE = nie jechal) */
    UBYTE     current_heat;                /* 0..14 */
    UBYTE     pick_order[MAX_RIDERS];      /* kolejnosc wyboru pol (14,15) */
    UBYTE     pick_index;
    Heat      heat;
    Track     track;
    Bike      bikes[MAX_RIDERS];
    Particle  particles[MAX_PARTICLES];
    UBYTE     particle_head;               /* bufor kolowy: nowa nadpisuje najstarsza */
    UBYTE     start_condition;             /* stan toru na start: 0 suchy, 1 wilgotny, 2 mokry */
    UBYTE     track_choice;                /* opcja STAN TORU: 0..2 jak wyzej, TRACK_RANDOM = losowany */
    UBYTE     night;                       /* opcja PORA: 1 = zawody w nocy (track_set_night) */
    UBYTE     rain_opt;                    /* opcja DESZCZ: w ktoryms biegu zaczyna padac */
    UBYTE     team_mode;                   /* opcja ZAWODY: 1 = mecz druzynowy (gospodarze 1+2, goscie 3+4) */
    UBYTE     track_pick;                  /* opcja TOR: 0..NUM_TRACKS-1 (w mistrzostwach tor 1. rundy) */
    UBYTE     series;                      /* opcja ZAWODY: 1 = mistrzostwa (SERIES_ROUNDS turniejow) */
    UBYTE     train_track, train_cond;     /* trening: tor, stan toru (0..2, TRACK_RANDOM) */
    UBYTE     train_night, train_rain;     /* trening: noc, deszcz od startu */
    UBYTE     series_round;                /* biezaca runda mistrzostw: 0..SERIES_ROUNDS-1 */
    UBYTE     series_pts[MAX_RIDERS][SERIES_ROUNDS];    /* punkty zawodnika w rundzie */
    UBYTE     series_place[MAX_RIDERS][SERIES_ROUNDS];  /* miejsce w rundzie (1..4), 0 = nie jechana */
    UBYTE     rain_from;                   /* bieg (0..), od ktorego pada; 0xFF = nie pada */
    UBYTE     new_records;                 /* REC_HEAT|REC_LAP pobite w ostatnim biegu */
    TourStats stats;                       /* fakty zawodow (gazeta) */
    GameState state;
} Tournament;

extern Tournament g_tour;
extern UBYTE g_lap_cheer;        /* trening: okrazenie z rekordem - okrzyk trybun (race.c -> audio.c) */
extern BOOL g_training;           /* trening (main.c): gracz 1 sam, okrazenia bez limitu, bez kar */
extern BOOL g_paused;             /* ESC: pauza z pytaniem (main.c) */
extern BOOL g_pause_quit;         /* TRUE: pytanie o wyjscie z gry (menu glowne), FALSE: do menu */

#define TRACK_RANDOM 3                     /* track_choice: stan toru losowany na start turnieju */

#define RAINING() (g_tour.current_heat >= g_tour.rain_from)   /* w tym biegu pada */

/* tournament.c */
void tournament_defaults(void);                  /* nazwiska, AI - raz przy starcie programu */
void tournament_default_names(void);              /* GRACZ n / CPU n wg liczby graczy, bez ruszania wpisanych */
void tournament_reset(void);                     /* punkty i tabela - przed kazdym turniejem */
void tournament_sort(UBYTE order[MAX_RIDERS]);   /* wg punktow, biegu dodatkowego, zwyciestw */
BOOL tournament_prepare_runoff(void);            /* remis o podium -> bieg dodatkowy (TRUE) */
#define HP_NONE     0xFF                         /* bieg jeszcze nie jechany */
#define HP_EXCLUDED 0xFE                         /* wykluczony (W) */
#define HP_DEFECT   0xFD                         /* defekt (D) */
void tournament_award_points(void);
UWORD tournament_team_points(UBYTE team);        /* mecz: 0 gospodarze (zawodnicy 0, 1), 1 goscie (2, 3) */
UBYTE tournament_team_heat(UBYTE team, UBYTE hn); /* punkty druzyny w biegu hn (5:1, 4:2, 3:3) */
void  series_reset(void);                        /* mistrzostwa: od rundy 1, bez punktow */
void  series_add_round(void);                    /* wyniki rozstrzygnietego turnieju do klasyfikacji */
UWORD series_total(UBYTE rider);                 /* suma punktow z rund */
void  series_sort(UBYTE order[MAX_RIDERS]);      /* klasyfikacja generalna */
#define SERIES_TRACK() ((UBYTE)((g_tour.track_pick + (g_tour.series ? g_tour.series_round : 0)) % NUM_TRACKS))

/* newspaper.c - gazeta "GLOS OWALU" po zawodach */
#define NEWS_NAME  13                    /* znakow nazwiska w gazecie */
typedef enum { NEWS_RUNOFF = 0, NEWS_WINNER_RECORD, NEWS_CLEAN_SWEEP, NEWS_ONE_POINT, NEWS_FALLBACK } NewsRule;
typedef struct NewsFacts {               /* wejscie wyboru naglowka (testowane na PC) */
    BOOL  title_runoff, winner_owns_record, winner_clean_sweep;
    UBYTE winner_points, runner_points;
} NewsFacts;
typedef struct NewsSummary {             /* niezmienna migawka zakonczonego turnieju */
    UBYTE    number;                     /* numer turnieju w tej sesji */
    UBYTE    order[MAX_RIDERS];
    char     name[MAX_RIDERS][NEWS_NAME + 1];
    UBYTE    points[MAX_RIDERS], wins[MAX_RIDERS];
    BOOL     human[MAX_RIDERS];
    NewsRule rule;
    UBYTE    facts[2], fact_value[2];    /* NEWS_FACT_* i wartosc; 0xFF = brak */
} NewsSummary;
enum { NEWS_FACT_FALSE_STARTS = 0, NEWS_FACT_EXCLUSIONS, NEWS_FACT_WINNER_WINS, NEWS_FACT_HUMANS, NEWS_FACT_DEFECTS,
       NEWS_FACT_FALLS, NEWS_FACT_NONE = 0xFF };
NewsRule news_pick_rule(const NewsFacts *f);   /* pierwsza spelniona regula wg priorytetu */
void     news_build(void);                     /* migawka po ostatnim rozstrzygnietym biegu */
const NewsSummary *news_summary(void);
BOOL tournament_watering_after(UBYTE heat_number);
void tournament_prepare_nominated(void);
void tournament_assign_gate(UBYTE rider, UBYTE gate);
UBYTE tournament_ai_choose_gate(UBYTE rider);

/* start.c */
void start_begin(void);
void start_stop_fall(UBYTE bike);   /* upadek: czerwona flaga, upadly wykluczony, powtorka biegu */
void start_update(void);   /* wywolywane co klatke w fazach przed HEAT_RACING */

/* race.c */
extern BOOL g_defects_enabled;                   /* opcja DEFEKTY w ustawieniach */
#define DEFECT_SPUTTER  (3 * FPS / 2)            /* silnik krztusi sie 1,5 s, zanim zgasnie */
void race_init_heat(void);
void race_update(void);
LONG race_leader_distance(void);                 /* dystans najdalej jadacego */
BOOL race_fall_active(void);                     /* ktos lezy, sedzia zaraz przerwie bieg (czerwona) */
/* fotofinisz: drugi na mecie najwyzej PHOTO_GAP klatek za zwyciezca - koncowka biegu nagrana */
BOOL  race_photo_finish(void);                   /* jest nagranie do pokazania */
void  race_photo_done(void);                     /* pokazane - nastepny bieg od nowa */
UBYTE race_photo_gap(void);                      /* roznica na mecie [klatki 1/50 s] */
UWORD race_replay_len(void);                     /* klatek nagrania */
UWORD race_replay_cross(void);                   /* klatka nagrania, w ktorej zwyciezca minal linie */
void  race_replay_frame(UWORD k);                /* motocykle w pozycjach z klatki k nagrania */
LONG race_straight_entry(UBYTE lap);             /* wjazd na prosta startowa konczaca okrazenie lap */
void race_launch(Bike *b);

/* track.c */
BOOL     track_init(void);                        /* wczytuje wszystkie tory (przed przejeciem sprzetu) */
void     track_free(void);
void     track_select(UBYTE type);
const char *track_name(UBYTE type);
void     track_render(UBYTE *dst);
UBYTE    track_grip_at(WORD x, WORD y);
GripZone track_zone_at(WORD x, WORD y);
GripZone track_zone_fast(WORD x, WORD y);          /* z tablicy komorek 8x8 */
LONG     track_progress(WORD x, WORD y);          /* 0..lap_length */
BOOL     track_on_start_straight(WORD x, WORD y); /* punkt na dolnej prostej (startowej) */
void     track_point(LONG s, WORD lateral, WORD *x, WORD *y);
WORD     track_lateral(WORD x, WORD y);           /* odleglosc od osi: - do kraweznika, + do bandy */
void     track_snap_lateral(fix16 *x, fix16 *y, WORD lat);
angle8   track_tangent(fix16 x, fix16 y);
WORD     track_bend_radius(LONG s, WORD lat);     /* 0 na prostej */
void     track_apply_watering(void);
void     track_set_wetness(UBYTE wet);           /* np. stan toru wybrany w menu */
void     track_wear(WORD x, WORD y);             /* motocykl ubija nawierzchnie */
void     track_loosen(WORD x, WORD y);           /* szpryca spadla na tor - spulchnia */
void     track_reset_wear(void);                 /* rowny tor (nowy turniej / zmiana toru) */
void     track_random_surface(void);              /* losowe wyjezdzone sciezki na start turnieju */
/* noc (tools/gennight.py): paleta nocna, tor przeliczony wg mapy swiatla reflektorow */
BOOL     track_night_available(void);             /* sa dane nocy i kopia toru (pristine) */
void     track_set_night(BOOL on);                /* przed track_reset_wear - przelicza tlo toru */
UBYTE    track_night_color(WORD x, WORD y, UBYTE c);   /* kolor rysowany na torze w (x, y) */
UBYTE    track_night_lit(UBYTE c);                /* kolor w pelnym swietle (okienko startowe) */
void     track_flush_wear(UBYTE bi, UBYTE *buf, WORD kx, WORD ky, WORD kw, WORD kh);  /* zmienione komorki do bufora (poza prostokatem) */
void     track_forget_wear(UBYTE bi);             /* bufor dostal cale tlo */
void     track_dry_step(void);
UBYTE    track_pack_at(WORD x, WORD y);           /* ubicie nawierzchni 0..255 (wyjezdzona sciezka) */
void     track_tick(UWORD rows);         /* raz na klatke: wiersze komorek do przemalowania, przyczepnosc */
void     track_tick_flush(void);
angle8   atan2_8(LONG dy, LONG dx);
#define TRACK_HALF_WIDTH (g_tour.track.half_width)   /* zalezy od wybranego toru */

/* input.c */
void  input_init(void);
void  input_update(void);                         /* raz na klatke */
UBYTE input_read(InputPort port);
BOOL  key_down(UBYTE rawkey);
BOOL  input_esc(void);                            /* ESC albo drugi przycisk joysticka (port 0/1) */
UBYTE key_get(void);                              /* nastepny wcisniety klawisz lub 0xFF */
InputPort input_menu_fire_port(void);             /* skad przyszedl ostatni FIRE w menu */
UBYTE input_menu(void);                           /* nowe wcisniecia: JOY1|strzalki|Return (bez myszy w porcie 0) */
#define KEY_RETURN    0x44
#define KEY_BACKSPACE 0x41
#define KEY_LALT      0x64
#define KEY_RALT      0x65
#define KEY_ESC 0x45

/* roost.c - szpryca (czasteczki Blittera) */
BOOL  roost_init(void);
void  roost_free(void);
void  roost_reset(void);                          /* nowy bieg / powtorka startu */
void  roost_update(BOOL bikes);                   /* ruch drobin; bikes = emisja spod kol */
void  roost_emit_water(fix16 x, fix16 y, angle8 heading);   /* zraszacz beczki */
UBYTE roost_hits(const Bike *b);                  /* ile cudzych drobin trafia w motocykl */
void  roost_erase(void);                          /* przed ui_render: zdejmij stare z "back" */
void  roost_draw(void);                           /* po ui_render: narysuj aktualne w "back" */
void  roost_forget_buffer(UBYTE buf);             /* bufor przerysowany w calosci */

/* records.c - rekordy torow (PROGDIR:records.dat) */
void         records_load(void);                  /* przed przejeciem sprzetu */
void         records_save(void);                  /* po oddaniu systemu */
void         settings_load(void);                 /* settings.dat: zawodnicy i opcje (po tournament_defaults) */
void         settings_save(void);                 /* przy wyjsciu, tylko po zmianie */
UBYTE        records_check_heat(void);            /* po biegu: bity REC_* nowych rekordow */
TrackRecord *records_for(UBYTE track);            /* najlepszy bieg i okrazenie (1. miejsca TOP 5) */
void         records_resolve_pending(void);       /* nowy turniej: niewpisane imiona -> domyslne */
BOOL         records_needs_name(UBYTE rider); /* gracz wpadl do TOP 5 - czeka na imie */
void         records_set_name(UBYTE rider, const char *init);
#ifdef TEST_HOF                                   /* test w WinUAE bez klawiatury: zawodnik 0 jak gracz */
#define RECORDS_HUMAN(r) (g_tour.riders[r].control == CTRL_HUMAN || (r) == 0)
#else
#define RECORDS_HUMAN(r) (g_tour.riders[r].control == CTRL_HUMAN)
#endif

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
extern BOOL g_music_enabled;                      /* muzyka w menu (opcja w ustawieniach) */
void sfx_buzzer(void);
void sfx_groan(void);                            /* jek trybun: tasma, przerwany bieg */
void audio_music_cut(void);                      /* muzyka: cisza od razu (przed dluzszym liczeniem) */

/* hud.c */
void hud_init(void);
void hud_update(void);

/* ai.c */
UBYTE ai_steer(Bike *b);
WORD  ai_reaction_frames(AiLevel lvl);

#endif
