/* hof.c - tabela rekordow toru: TOP 5 ukonczonych biegow i TOP 5 okrazen na kazdy tor.
 *
 * Sama logika (bez dysku - zapis i odczyt robi records.c), testowana na PC przez
 * tools/sim/records_test.sh. Czasy w klatkach PAL. Rowny czas nie wypiera istniejacego wpisu:
 * nowy trafia za wczesniejsze remisy, a przy pelnej liscie odpada. Do tabel wchodza tylko
 * gracze (nie boty). Imie: 7 znakow (A-Z, 0-9, polskie litery, '-', spacja). Miejsce gracza
 * czeka na wpis imienia (name[7] = zawodnik + 1). */
#include "hof.h"
#include "text.h"                       /* PL_FIRST, PL_COUNT: polskie litery w nazwiskach */

HofTrack g_hof[NUM_TRACKS];

#define HOF_MAGIC   "SPHF"
#define HOF_VERSION 3                    /* 2: imie 6 znakow (wczytywane), 1: 3 znaki (niewydana) */
#define HOF_MAX_FRAMES 30000             /* 10 minut - wiecej to uszkodzony plik */

void hof_clear(void)
{
    UBYTE t, i;
    for (t = 0; t < NUM_TRACKS; t++)
        for (i = 0; i < HOF_N; i++) {
            g_hof[t].heat[i].frames = g_hof[t].lap[i].frames = 0;
            g_hof[t].heat[i].name[0] = g_hof[t].lap[i].name[0] = 0;
            g_hof[t].heat[i].name[HOF_NAME] = g_hof[t].lap[i].name[HOF_NAME] = 0;
        }
}

BOOL hof_valid_char(char c)
{
    UBYTE u = (UBYTE)c;
    return (BOOL)((u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '-' || u == ' '
                  || (u >= PL_FIRST && u < PL_FIRST + PL_COUNT));
}

WORD hof_insert(HofEntry list[HOF_N], UWORD frames, const char *name, UBYTE pending)
{
    WORD pos, k;
    if (!frames || frames > HOF_MAX_FRAMES) return -1;
    for (pos = 0; pos < HOF_N; pos++)
        if (!list[pos].frames || frames < list[pos].frames) break;   /* remis: za istniejacym */
    if (pos >= HOF_N) return -1;
    for (k = HOF_N - 1; k > pos; k--) list[k] = list[k - 1];
    list[pos].frames = frames;
    for (k = 0; k < HOF_NAME; k++) list[pos].name[k] = name[k];
    list[pos].name[HOF_NAME] = (char)pending;
    return pos;
}

void hof_default_name(char out[HOF_NAME + 1], const char *name)
{
    UBYTE n = 0;
    while (*name == ' ') name++;
    while (*name && *name != ' ' && n < HOF_NAME) {
        UBYTE c = (UBYTE)*name++;
        if (c >= 'a' && c <= 'z') c = (UBYTE)(c - 'a' + 'A');
        if (hof_valid_char((char)c)) out[n++] = (char)c;
    }
    while (n < HOF_NAME) out[n++] = ' ';
    out[HOF_NAME] = 0;
}

static void each_entry(void (*fn)(HofEntry *e, UBYTE rider, const char *name), UBYTE rider, const char *name)
{
    UBYTE t, i;
    for (t = 0; t < NUM_TRACKS; t++)
        for (i = 0; i < HOF_N; i++) { fn(&g_hof[t].heat[i], rider, name); fn(&g_hof[t].lap[i], rider, name); }
}

static void set_one(HofEntry *e, UBYTE rider, const char *name)
{
    UBYTE k;
    if (!e->frames || (UBYTE)e->name[HOF_NAME] != (UBYTE)(rider + 1)) return;
    for (k = 0; k < HOF_NAME; k++) e->name[k] = name[k];
    e->name[HOF_NAME] = 0;
}

static BOOL found;
static void find_one(HofEntry *e, UBYTE rider, const char *name)
{
    (void)name;
    if (e->frames && (UBYTE)e->name[HOF_NAME] == (UBYTE)(rider + 1)) found = TRUE;
}

static void resolve_one(HofEntry *e, UBYTE rider, const char *name)
{
    (void)rider; (void)name;
    e->name[HOF_NAME] = 0;                       /* domyslne imie juz jest w name[0..6] */
}

void hof_set_name(UBYTE rider, const char *name) { each_entry(set_one, rider, name); }
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
                for (k = 0; k < HOF_NAME; k++) *p++ = (UBYTE)(e->frames ? e->name[k] : ' ');
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
    UBYTE t, i, k, c, n;
    if (len < 6) return FALSE;
    for (k = 0; k < 4; k++) if (buf[k] != (UBYTE)HOF_MAGIC[k]) return FALSE;
    if (buf[4] == HOF_VERSION && len == HOF_FILE_LEN) n = HOF_NAME;
    else if (buf[4] == 2 && len == HOF_FILE_LEN_V2) n = 6;     /* starszy plik: imie dopelnione spacja */
    else return FALSE;
    if (buf[5] != NUM_TRACKS) return FALSE;
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
                for (k = 0; k < HOF_NAME; k++) {
                    e->name[k] = k < n ? (char)*p++ : ' ';
                    if (!hof_valid_char(e->name[k])) return FALSE;
                }
                e->name[HOF_NAME] = 0;
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
