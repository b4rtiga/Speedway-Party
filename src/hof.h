/* hof.h - tabela rekordow toru (TOP 5 okrazen i biegow) - sama logika, bez dysku. */
#ifndef HOF_H
#define HOF_H

#include "game.h"

#define HOF_N        5                   /* miejsc w kazdej tabeli */
#define HOF_NAME     6                   /* znakow imienia */
#define HOF_FILE_LEN (6 + NUM_TRACKS * 2 * HOF_N * (2 + HOF_NAME) + 2)

typedef struct {
    UWORD frames;                        /* czas w klatkach PAL (1/50 s), 0 = puste miejsce */
    char  name[HOF_NAME + 1];            /* imie (dopelnione spacjami); name[6] = 0 albo
                                          * (zawodnik+1), gdy miejsce czeka na wpis imienia */
} HofEntry;

typedef struct {
    HofEntry heat[HOF_N];                /* ukonczone biegi (4 okrazenia) */
    HofEntry lap[HOF_N];                 /* okrazenia */
} HofTrack;

extern HofTrack g_hof[NUM_TRACKS];

void  hof_clear(void);
/* wstawia czas do listy (rosnaco; rowny czas za istniejacymi), zwraca miejsce 0..4 albo -1 */
WORD  hof_insert(HofEntry list[HOF_N], UWORD frames, const char *name, UBYTE pending);
/* imie domyslne: pierwsze slowo nazwiska zawodnika (do 6 znakow), dopelnione spacjami */
void  hof_default_name(char out[HOF_NAME + 1], const char *name);
/* wpisane imie do wszystkich miejsc zawodnika czekajacych na wpis */
void  hof_set_name(UBYTE rider, const char *name);
BOOL  hof_pending(UBYTE rider);          /* zawodnik ma miejsca czekajace na imie */
void  hof_resolve_pending(void);         /* przed zapisem: czekajace miejsca zostaja z domyslnym */
/* plik halloffame.dat: "SPHF", wersja, liczba torow, wpisy (czas big-endian + 6 znakow), suma */
UWORD hof_serialize(UBYTE *buf);         /* buf >= HOF_FILE_LEN, zwraca dlugosc */
BOOL  hof_deserialize(const UBYTE *buf, LONG len);   /* FALSE = zly plik (g_hof bez zmian) */
BOOL  hof_valid_char(char c);            /* A-Z, 0-9, polskie litery (0x80..0x88), '-', spacja */

#endif
