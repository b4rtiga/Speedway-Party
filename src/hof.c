/* hof.c - tabela rekordow toru: TOP 5 ukonczonych biegow i TOP 5 okrazen na kazdy tor.
 *
 * Sama logika (bez dysku - zapis i odczyt robi records.c), testowana na PC przez
 * tools/sim/records_test.sh. Czasy w klatkach PAL. Rowny czas nie wypiera istniejacego wpisu:
 * nowy trafia za wczesniejsze remisy, a przy pelnej liscie odpada. Inicjaly: 3 znaki A-Z,
 * 0-9 albo '-'. Miejsce czlowieka czeka na wpis inicjalow (init[3] = zawodnik + 1). */
#include "hof.h"

HofTrack g_hof[NUM_TRACKS];

#define HOF_MAGIC   "SPHF"
#define HOF_VERSION 1
#define HOF_MAX_FRAMES 30000             /* 10 minut - wiecej to uszkodzony plik */

void hof_clear(void)
{
    UBYTE t, i;
    for (t = 0; t < NUM_TRACKS; t++)
        for (i = 0; i < HOF_N; i++) {
            g_hof[t].heat[i].frames = g_hof[t].lap[i].frames = 0;
            g_hof[t].heat[i].init[0] = g_hof[t].lap[i].init[0] = 0;
            g_hof[t].heat[i].init[3] = g_hof[t].lap[i].init[3] = 0;
        }
}

BOOL hof_valid_char(char c)
{
    return (BOOL)((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-');
}

WORD hof_insert(HofEntry list[HOF_N], UWORD frames, const char *init, UBYTE pending)
{
    WORD pos, k;
    if (!frames || frames > HOF_MAX_FRAMES) return -1;
    for (pos = 0; pos < HOF_N; pos++)
        if (!list[pos].frames || frames < list[pos].frames) break;   /* remis: za istniejacym */
    if (pos >= HOF_N) return -1;
    for (k = HOF_N - 1; k > pos; k--) list[k] = list[k - 1];
    list[pos].frames = frames;
    for (k = 0; k < HOF_INIT; k++) list[pos].init[k] = init[k];
    list[pos].init[HOF_INIT] = (char)pending;
    return pos;
}

void hof_default_initials(char out[HOF_INIT + 1], const char *name)
{
    static const char pl[] = "ACELNOSZZ";        /* Ą Ć Ę Ł Ń Ó Ś Ź Ż bez ogonkow */
    UBYTE n = 0;
    while (*name && n < HOF_INIT) {
        UBYTE c = (UBYTE)*name++;
        if (c >= 'a' && c <= 'z') c = (UBYTE)(c - 'a' + 'A');
        if (c >= 0x80 && c < 0x80 + sizeof(pl) - 1) c = (UBYTE)pl[c - 0x80];
        if (hof_valid_char((char)c) && c != '-') out[n++] = (char)c;
    }
    while (n < HOF_INIT) out[n++] = '-';
    out[HOF_INIT] = 0;
}

static void each_entry(void (*fn)(HofEntry *e, UBYTE rider, const char *init), UBYTE rider, const char *init)
{
    UBYTE t, i;
    for (t = 0; t < NUM_TRACKS; t++)
        for (i = 0; i < HOF_N; i++) { fn(&g_hof[t].heat[i], rider, init); fn(&g_hof[t].lap[i], rider, init); }
}

static void set_one(HofEntry *e, UBYTE rider, const char *init)
{
    UBYTE k;
    if (!e->frames || (UBYTE)e->init[HOF_INIT] != (UBYTE)(rider + 1)) return;
    for (k = 0; k < HOF_INIT; k++) e->init[k] = init[k];
    e->init[HOF_INIT] = 0;
}

static BOOL found;
static void find_one(HofEntry *e, UBYTE rider, const char *init)
{
    (void)init;
    if (e->frames && (UBYTE)e->init[HOF_INIT] == (UBYTE)(rider + 1)) found = TRUE;
}

static void resolve_one(HofEntry *e, UBYTE rider, const char *init)
{
    (void)rider; (void)init;
    e->init[HOF_INIT] = 0;                       /* domyslne inicjaly juz sa w init[0..2] */
}

void hof_set_initials(UBYTE rider, const char *init) { each_entry(set_one, rider, init); }
BOOL hof_pending(UBYTE rider) { found = FALSE; each_entry(find_one, rider, 0); return found; }
void hof_resolve_pending(void) { each_entry(resolve_one, 0, 0); }

/* ---------------- plik: jawne bajty big-endian, suma kontrolna ---------------- */

static UWORD checksum(const UBYTE *b, UWORD n)
{
    UWORD s = 0x5350, i;                         /* "SP" */
    for (i = 0; i < n; i++) s = (UWORD)(((s << 1) | (s >> 15)) + b[i]);
    return s;
}

UWORD hof_serialize(UBYTE *buf)
{
    UBYTE *p = buf, t, i, k, c;
    UWORD sum;
    for (k = 0; k < 4; k++) *p++ = (UBYTE)HOF_MAGIC[k];
    *p++ = HOF_VERSION;
    *p++ = NUM_TRACKS;
    for (t = 0; t < NUM_TRACKS; t++)
        for (c = 0; c < 2; c++)
            for (i = 0; i < HOF_N; i++) {
                const HofEntry *e = c ? &g_hof[t].lap[i] : &g_hof[t].heat[i];
                *p++ = (UBYTE)(e->frames >> 8);
                *p++ = (UBYTE)e->frames;
                for (k = 0; k < HOF_INIT; k++) *p++ = (UBYTE)(e->frames ? e->init[k] : '-');
            }
    sum = checksum(buf, (UWORD)(p - buf));
    *p++ = (UBYTE)(sum >> 8);
    *p++ = (UBYTE)sum;
    return (UWORD)(p - buf);
}

BOOL hof_deserialize(const UBYTE *buf, LONG len)
{
    static HofTrack tmp[NUM_TRACKS];
    const UBYTE *p = buf;
    UBYTE t, i, k, c;
    if (len != HOF_FILE_LEN) return FALSE;
    for (k = 0; k < 4; k++) if (buf[k] != (UBYTE)HOF_MAGIC[k]) return FALSE;
    if (buf[4] != HOF_VERSION || buf[5] != NUM_TRACKS) return FALSE;
    if (checksum(buf, (UWORD)(len - 2)) != (UWORD)((buf[len - 2] << 8) | buf[len - 1])) return FALSE;
    p += 6;
    for (t = 0; t < NUM_TRACKS; t++)
        for (c = 0; c < 2; c++) {
            UWORD prev = 0;
            BOOL empty = FALSE;
            for (i = 0; i < HOF_N; i++) {
                HofEntry *e = c ? &tmp[t].lap[i] : &tmp[t].heat[i];
                e->frames = (UWORD)((p[0] << 8) | p[1]);
                p += 2;
                for (k = 0; k < HOF_INIT; k++) {
                    e->init[k] = (char)*p++;
                    if (!hof_valid_char(e->init[k])) return FALSE;
                }
                e->init[HOF_INIT] = 0;
                /* czasy rosnace, puste tylko na koncu, w rozsadnym zakresie */
                if (e->frames) {
                    if (empty || e->frames > HOF_MAX_FRAMES || e->frames < prev) return FALSE;
                    prev = e->frames;
                } else empty = TRUE;
            }
        }
    for (t = 0; t < NUM_TRACKS; t++) g_hof[t] = tmp[t];
    return TRUE;
}
