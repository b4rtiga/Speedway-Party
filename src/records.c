/* records.c - rekordy torow: najlepszy czas biegu (4 okrazenia) i najlepsze okrazenie.
 *
 * Wczytywane przy starcie (przed przejeciem sprzetu), zapisywane przy wyjsciu z gry
 * (po oddaniu systemu) do PROGDIR:records.dat - w trakcie gry nie ma dostepu do dysku.
 * Czasy w klatkach PAL (1/50 s), 0 = brak rekordu. */
#include <dos/dos.h>
#include <proto/dos.h>

#include "game.h"

#define RECORDS_FILE  "PROGDIR:records.dat"
#define RECORDS_MAGIC 0x53505231UL            /* "SPR1" */

typedef struct {
    ULONG       magic;
    TrackRecord rec[NUM_TRACKS];
} RecordsFile;

static RecordsFile file;
static BOOL dirty;

TrackRecord *records_for(UBYTE track) { return &file.rec[track % NUM_TRACKS]; }

void records_load(void)
{
    if (!asset_load(RECORDS_FILE, &file, sizeof(file)) || file.magic != RECORDS_MAGIC) {
        UBYTE i;
        file.magic = RECORDS_MAGIC;
        for (i = 0; i < NUM_TRACKS; i++) {
            file.rec[i].heat_frames = 0;
            file.rec[i].lap_frames = 0;
            file.rec[i].heat_name[0] = file.rec[i].lap_name[0] = 0;
        }
    }
    dirty = FALSE;
}

void records_save(void)
{
    BPTR fh;
    if (!dirty) return;
    fh = Open((CONST_STRPTR)RECORDS_FILE, MODE_NEWFILE);
    if (!fh) return;                          /* np. dysk zabezpieczony przed zapisem */
    Write(fh, &file, sizeof(file));
    Close(fh);
    dirty = FALSE;
}

static void copy_name(char *dst, const char *src)
{
    UBYTE i;
    for (i = 0; i < NAME_LEN && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

/* Po biegu: czas zwyciezcy i najlepsze okrazenia wszystkich, ktorzy dojechali.
 * Zwraca bity REC_HEAT / REC_LAP dla nowo pobitych rekordow. */
UBYTE records_check_heat(void)
{
    TrackRecord *r = records_for(g_tour.track.type);
    Heat *h = &g_tour.heat;
    UBYTE i, result = 0;

    for (i = 0; i < MAX_RIDERS; i++) {
        const Bike *b = &g_tour.bikes[i];
        if (b->empty || b->state != BIKE_FINISHED) continue;
        if (h->finished_count && b->rider == h->finish_order[0] &&
            (!r->heat_frames || b->finish_frame < r->heat_frames)) {
            r->heat_frames = b->finish_frame;
            copy_name(r->heat_name, g_tour.riders[b->rider].name);
            g_tour.stats.rec_heat_holder = b->rider;
            g_tour.stats.records++;
            result |= REC_HEAT;
        }
        if (b->best_lap && (!r->lap_frames || b->best_lap < r->lap_frames)) {
            r->lap_frames = b->best_lap;
            copy_name(r->lap_name, g_tour.riders[b->rider].name);
            g_tour.stats.rec_lap_holder = b->rider;
            g_tour.stats.records++;
            result |= REC_LAP;
        }
    }
    if (result) dirty = TRUE;
    return result;
}
