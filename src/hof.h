/* hof.h - tabela rekordow toru (TOP 5 okrazen i biegow) - sama logika, bez dysku. */
#ifndef HOF_H
#define HOF_H

#include "game.h"

#define HOF_N        5                   /* miejsc w kazdej tabeli */
#define HOF_INIT     3                   /* znakow inicjalow */
#define HOF_FILE_LEN (6 + NUM_TRACKS * 2 * HOF_N * (2 + HOF_INIT) + 2)

typedef struct {
    UWORD frames;                        /* czas w klatkach PAL (1/50 s), 0 = puste miejsce */
    char  init[HOF_INIT + 1];            /* inicjaly; init[3] = 0 albo (zawodnik+1), gdy czeka na wpis */
} HofEntry;

typedef struct {
    HofEntry heat[HOF_N];                /* ukonczone biegi (4 okrazenia) */
    HofEntry lap[HOF_N];                 /* okrazenia */
} HofTrack;

extern HofTrack g_hof[NUM_TRACKS];

void  hof_clear(void);
/* wstawia czas do listy (rosnaco; rowny czas za istniejacymi), zwraca miejsce 0..4 albo -1 */
WORD  hof_insert(HofEntry list[HOF_N], UWORD frames, const char *init, UBYTE pending);
/* inicjaly z nazwiska: litery A-Z i cyfry (polskie litery bez ogonkow), dopelnione '-' */
void  hof_default_initials(char out[HOF_INIT + 1], const char *name);
/* wpisane inicjaly do wszystkich miejsc zawodnika czekajacych na wpis */
void  hof_set_initials(UBYTE rider, const char *init);
BOOL  hof_pending(UBYTE rider);          /* zawodnik ma miejsca czekajace na inicjaly */
void  hof_resolve_pending(void);         /* przed zapisem: czekajace miejsca zostaja z domyslnymi */
/* plik halloffame.dat: "SPHF", wersja, liczba torow, wpisy (czas big-endian + 3 znaki), suma */
UWORD hof_serialize(UBYTE *buf);         /* buf >= HOF_FILE_LEN, zwraca dlugosc */
BOOL  hof_deserialize(const UBYTE *buf, LONG len);   /* FALSE = zly plik (g_hof bez zmian) */
BOOL  hof_valid_char(char c);

#endif
