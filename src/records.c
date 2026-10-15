/* records.c - rekordy torow na dysku: tabela TOP 5 biegow i okrazen (logika w hof.c).
 *
 * Wczytywane przy starcie (przed przejeciem sprzetu), zapisywane przy normalnym wyjsciu z gry
 * (po oddaniu systemu) - w trakcie gry nie ma dostepu do dysku, wiec reset albo wylaczenie
 * Amigi w trakcie sesji traci jej wyniki.
 *
 * Pliki w katalogu gry:
 *   halloffame.dat  biezace rekordy (hof.c: wersja, big-endian, suma kontrolna; wczytuje tez wersje 2)
 *   halloffame.bak  poprzednia poprawna wersja (gdy .dat jest uszkodzony)
 *   records.dat     stary format "SPR1" (1 rekord biegu i okrazenia) - tylko import, bez zmian
 * Zapis: plik tymczasowy -> pelny Write i Close -> stary .dat na .bak -> tymczasowy na .dat.
 * Nieudany zapis (zabezpieczony dysk, brak miejsca) zostawia poprzedni poprawny plik, a okna
 * bledow DOS sa na czas zapisu wylaczone, zeby gra mogla sie zamknac. */
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <proto/exec.h>
#ifdef WHDLOAD
#include "whd.h"
#endif

#include "game.h"
#include "hof.h"

#define HOF_DAT  "PROGDIR:halloffame.dat"
#define HOF_BAK  "PROGDIR:halloffame.bak"
#define HOF_TMP  "PROGDIR:halloffame.tmp"
#define OLD_FILE "PROGDIR:records.dat"
#define OLD_MAGIC 0x53505231UL            /* "SPR1" */

typedef struct {                          /* stary plik records.dat (struktura zapisana wprost) */
    ULONG       magic;
    TrackRecord rec[NUM_TRACKS];
} OldRecordsFile;

static BOOL dirty;
static UBYTE filebuf[HOF_FILE_LEN];

/* plik dowolnej dlugosci do HOF_FILE_LEN (starsza wersja jest krotsza) - sprawdza hof_deserialize */
static BOOL load_hof(const char *name)
{
    BPTR fh = Open((CONST_STRPTR)name, MODE_OLDFILE);
    LONG n;
    if (!fh) return FALSE;
    n = Read(fh, filebuf, HOF_FILE_LEN);
    Close(fh);
    return (BOOL)(n > 0 && hof_deserialize(filebuf, n));
}

/* nazwisko bota ("CPU 2") - takich wpisow ze starego pliku nie przenosimy */
static BOOL bot_name(const char *n)
{
    return (BOOL)(n[0] == 'C' && n[1] == 'P' && n[2] == 'U' && (n[3] == ' ' || n[3] == 0));
}

/* stary format: najlepszy bieg i okrazenie toru (tylko graczy), imie z nazwiska */
static BOOL import_old(void)
{
    static OldRecordsFile old;
    UBYTE t;
    char nm[HOF_NAME + 1];
    if (!asset_load(OLD_FILE, &old, sizeof(old)) || old.magic != OLD_MAGIC) return FALSE;
    for (t = 0; t < NUM_TRACKS; t++) {
        old.rec[t].heat_name[NAME_LEN] = old.rec[t].lap_name[NAME_LEN] = 0;
        if (!bot_name(old.rec[t].heat_name)) {
            hof_default_name(nm, old.rec[t].heat_name);
            hof_insert(g_hof[t].heat, old.rec[t].heat_frames, nm, 0);
        }
        if (!bot_name(old.rec[t].lap_name)) {
            hof_default_name(nm, old.rec[t].lap_name);
            hof_insert(g_hof[t].lap, old.rec[t].lap_frames, nm, 0);
        }
    }
    return TRUE;
}

COLD void records_load(void)
{
    hof_clear();
    dirty = FALSE;
    if (load_hof(HOF_DAT) || load_hof(HOF_BAK)) return;
    hof_clear();
    if (import_old()) dirty = TRUE;               /* przy wyjsciu powstanie halloffame.dat */
}

#ifdef WHDLOAD
COLD void records_save(void)
{
    /* WHDLoad: zapis przez WHDLoad (katalog data/ albo SavePath) - bez pliku tymczasowego */
    UWORD len;
    if (!dirty) return;
    hof_resolve_pending();
    len = hof_serialize(filebuf);
    if (whd_save_file(HOF_DAT, filebuf, len)) dirty = FALSE;
}
#else
COLD void records_save(void)
{
    struct Process *pr;
    APTR old_win;
    BPTR fh;
    UWORD len;
    BOOL ok = FALSE;
    if (!dirty) return;
    hof_resolve_pending();                        /* niewpisane imie -> domyslne */
    len = hof_serialize(filebuf);
    pr = (struct Process *)FindTask(0);
    old_win = pr->pr_WindowPtr;
    pr->pr_WindowPtr = (APTR)-1;                  /* bez okien "dysk zabezpieczony" / "dysk pelny" */
    fh = Open((CONST_STRPTR)HOF_TMP, MODE_NEWFILE);
    if (fh) {
        ok = (BOOL)(Write(fh, filebuf, len) == len);
        if (!Close(fh)) ok = FALSE;
        if (ok) {
            DeleteFile((CONST_STRPTR)HOF_BAK);
            Rename((CONST_STRPTR)HOF_DAT, (CONST_STRPTR)HOF_BAK);       /* moze nie istniec */
            if (!Rename((CONST_STRPTR)HOF_TMP, (CONST_STRPTR)HOF_DAT)) {
                Rename((CONST_STRPTR)HOF_BAK, (CONST_STRPTR)HOF_DAT);   /* przywroc poprzedni */
                ok = FALSE;
            }
        }
        if (!ok) DeleteFile((CONST_STRPTR)HOF_TMP);
    }
    pr->pr_WindowPtr = old_win;
    if (ok) dirty = FALSE;
}
#endif

/* widok najlepszego wyniku dla dotychczasowych ekranow (tabela, wyniki biegu) */
TrackRecord *records_for(UBYTE track)
{
    static TrackRecord view;
    const HofTrack *h = &g_hof[track % NUM_TRACKS];
    UBYTE k;
    view.heat_frames = h->heat[0].frames;
    view.lap_frames = h->lap[0].frames;
    for (k = 0; k < HOF_NAME; k++) { view.heat_name[k] = h->heat[0].name[k]; view.lap_name[k] = h->lap[0].name[k]; }
    view.heat_name[HOF_NAME] = view.lap_name[HOF_NAME] = 0;
    return &view;
}

/* Po biegu (raz): czas kazdego gracza, ktory ukonczyl, i jego najlepsze okrazenie do TOP 5 toru.
 * Boty nie wchodza do tabel. Miejsce gracza czeka na wpis imienia (domyslne z nazwiska).
 * Zwraca bity REC_HEAT / REC_LAP, gdy pobity jest rekord toru (1. miejsce). */
UBYTE records_check_heat(void)
{
    HofTrack *h = &g_hof[g_tour.track.type % NUM_TRACKS];
    UBYTE i, result = 0;
    for (i = 0; i < MAX_RIDERS; i++) {
        const Bike *b = &g_tour.bikes[i];
        char nm[HOF_NAME + 1];
        UBYTE pending;
        WORD ph, pl;
        if (b->empty || b->state != BIKE_FINISHED || !RECORDS_HUMAN(b->rider)) continue;
        hof_default_name(nm, g_tour.riders[b->rider].name);
        pending = (UBYTE)(b->rider + 1);
        ph = hof_insert(h->heat, b->finish_frame, nm, pending);
        pl = b->best_lap ? hof_insert(h->lap, b->best_lap, nm, pending) : -1;
        if (ph >= 0 || pl >= 0) dirty = TRUE;              /* plik zapisuje sie tylko po zmianie */
        if (ph == 0) {
            result |= REC_HEAT;
            g_tour.stats.rec_heat_holder = b->rider;
            g_tour.stats.records++;
        }
        if (pl == 0) {
            result |= REC_LAP;
            g_tour.stats.rec_lap_holder = b->rider;
            g_tour.stats.records++;
        }
    }
    return result;
}

/* miejsca czekajace na imie (przerwany turniej) dostaja imie domyslne z nazwiska */
void records_resolve_pending(void) { hof_resolve_pending(); }

/* czy zawodnik ma wpisac imie (zakwalifikowal sie w turnieju do TOP 5) */
BOOL records_needs_name(UBYTE rider) { return hof_pending(rider); }
void records_set_name(UBYTE rider, const char *nm) { hof_set_name(rider, nm); dirty = TRUE; }
