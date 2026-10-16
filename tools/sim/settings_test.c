/* settings_test.c - zapis i odczyt ustawien (settings.dat, records.c) na "dysku w pamieci":
 * zastepcze Open/Read/Write/Close/Rename/DeleteFile, wiec sprawdzany jest caly prawdziwy kod
 * zapisu przez plik tymczasowy.   sh tools/sim/settings_test.sh */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include "game.h"
#include "hof.h"

static int fails;
#define CHECK(cond, what) do { if (!(cond)) { printf("BLAD: %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)

/* ---- dysk w pamieci: kilka plikow po nazwie ---- */
#define MAXF 8
static struct { char name[64]; UBYTE data[4096]; LONG len; int used; } disk[MAXF];
static struct { int f; LONG pos; int write; } handles[8];
static int writes;                                   /* ile razy cos zapisano (Open do zapisu) */

static int find(const char *n) { int i; for (i = 0; i < MAXF; i++) if (disk[i].used && !strcmp(disk[i].name, n)) return i; return -1; }
static int create(const char *n) { int i = find(n); if (i < 0) for (i = 0; i < MAXF && disk[i].used; i++) ; if (i >= MAXF) return -1;
                                   strcpy(disk[i].name, n); disk[i].len = 0; disk[i].used = 1; return i; }

BPTR Open(CONST_STRPTR name, LONG mode)
{
    int f = mode == MODE_NEWFILE ? create((const char *)name) : find((const char *)name), h;
    if (f < 0) return 0;
    if (mode == MODE_NEWFILE) writes++;
    for (h = 1; h < 8 && handles[h].f; h++) ;
    handles[h].f = f + 1; handles[h].pos = 0; handles[h].write = mode == MODE_NEWFILE;
    return (BPTR)h;
}
LONG Read(BPTR fh, APTR buf, LONG n)
{
    int f = handles[fh].f - 1;
    LONG left = disk[f].len - handles[fh].pos;
    if (n > left) n = left;
    memcpy(buf, disk[f].data + handles[fh].pos, (size_t)n);
    handles[fh].pos += n;
    return n;
}
LONG Write(BPTR fh, CONST APTR buf, LONG n)
{
    int f = handles[fh].f - 1;
    memcpy(disk[f].data + handles[fh].pos, buf, (size_t)n);
    handles[fh].pos += n;
    if (handles[fh].pos > disk[f].len) disk[f].len = handles[fh].pos;
    return n;
}
BOOL Close(BPTR fh) { handles[fh].f = 0; return TRUE; }
BOOL DeleteFile(CONST_STRPTR n) { int f = find((const char *)n); if (f < 0) return FALSE; disk[f].used = 0; return TRUE; }
BOOL Rename(CONST_STRPTR a, CONST_STRPTR b)
{
    int f = find((const char *)a);
    if (f < 0 || find((const char *)b) >= 0) return FALSE;
    strcpy(disk[f].name, (const char *)b);
    return TRUE;
}
static struct Process proc;
struct Task *FindTask(CONST_STRPTR n) { (void)n; return (struct Task *)&proc; }
BOOL asset_load(const char *name, void *dst, LONG size)
{
    BPTR fh = Open((CONST_STRPTR)name, MODE_OLDFILE);
    LONG n;
    if (!fh) return FALSE;
    n = Read(fh, dst, size);
    Close(fh);
    return n == size;
}

/* reszta gry, ktorej records.c nie potrzebuje w tym tescie */
Tournament g_tour;
BOOL g_music_enabled = TRUE, g_defects_enabled = TRUE;
void tournament_default_names(void) { }

int main(void)
{
    int f;
    g_tour.num_humans = 1;
    g_tour.track_choice = TRACK_RANDOM;
    strcpy(g_tour.riders[0].name, "MAREK \x83OPATA");
    strcpy(g_tour.riders[1].name, "CPU 2");
    strcpy(g_tour.riders[2].name, "CPU 3");
    strcpy(g_tour.riders[3].name, "CPU 4");

    settings_load();                                  /* brak pliku - zostaja domyslne */
    CHECK(g_tour.num_humans == 1 && !strcmp(g_tour.riders[0].name, "MAREK \x83OPATA"), "bez pliku: domyslne");
    settings_save();
    CHECK(writes == 0 && find("PROGDIR:settings.dat") < 0, "bez zmian: bez zapisu");

    g_tour.num_humans = 3;
    g_tour.track_choice = 2;
    g_tour.night = 1;
    g_defects_enabled = FALSE;
    g_music_enabled = FALSE;
    strcpy(g_tour.riders[1].name, "ADA");
    g_tour.riders[1].input = INPUT_KEYS_ZX;
    g_tour.riders[3].ai_level = AI_MASTER;
    settings_save();
    f = find("PROGDIR:settings.dat");
    CHECK(f >= 0 && disk[f].len == 4 + 6 + MAX_RIDERS * (NAME_LEN + 3) && !memcmp(disk[f].data, "SPS1", 4), "zapis: plik i naglowek");
    CHECK(find("PROGDIR:settings.tmp") < 0, "plik tymczasowy usuniety");

    memset(&g_tour, 0, sizeof(g_tour));               /* "nowe uruchomienie gry" */
    g_defects_enabled = g_music_enabled = TRUE;
    settings_load();
    CHECK(g_tour.num_humans == 3 && g_tour.track_choice == 2 && g_tour.night == 1, "odczyt: gracze, stan toru, pora");
    CHECK(!g_defects_enabled && !g_music_enabled, "odczyt: defekty i muzyka");
    CHECK(!strcmp(g_tour.riders[0].name, "MAREK \x83OPATA") && !strcmp(g_tour.riders[1].name, "ADA"), "odczyt: nazwiska (z polska litera)");
    CHECK(g_tour.riders[1].input == INPUT_KEYS_ZX && g_tour.riders[3].ai_level == AI_MASTER, "odczyt: sterowanie i poziom bota");

    writes = 0;
    settings_save();
    CHECK(writes == 0, "po odczycie bez zmian: bez zapisu");

    disk[f].data[4] = 9;                              /* zla liczba graczy - plik odrzucony */
    memset(&g_tour, 0, sizeof(g_tour));
    g_tour.num_humans = 1;
    settings_load();
    CHECK(g_tour.num_humans == 1 && g_tour.night == 0, "uszkodzony plik: domyslne");

    printf(fails ? "BLEDY: %d\n" : "OK: ustawienia\n", fails);
    return fails != 0;
}
