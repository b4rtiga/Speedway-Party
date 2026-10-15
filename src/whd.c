/* whd.c - wersja WHDLoad (make whdload): zamiast systemu Amigi.
 *
 * Pamiec: WHDLoad daje BaseMem (Chip, od adresu 0) i ExpMem (Fast, jesli jest). Program gry
 * wczytuje slave pod $1000; za nim (whd_heap, z whdstart.s) zaczyna sie sterta Chip az do
 * WHD_STACK_RESERVE ponizej konca BaseMem (tam stos). MEMF_ANY idzie do ExpMem, a gdy jej
 * brak albo sie skonczyla - do Chip. Zwalniania nie ma (gra zwalnia pamiec dopiero na koncu).
 *
 * Pliki: tylko odczyt przez WHDLoad (resload_GetFileSize, resload_LoadFileOffset); nazwy jak
 * w grze ("PROGDIR:data/bikes.spr") - bez PROGDIR: i data/, bo katalogiem WHDLoad jest data/.
 * Zapis rekordow: whd_save_file (resload_SaveFile, plik w data/ albo w SavePath WHDLoad). */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>

#include "whd.h"

#define WHD_BASEMEM       0x200000UL     /* jak ws_BaseMemSize w slave.s */
#define WHD_STACK_RESERVE 0x10000UL      /* stos na koncu BaseMem */

APTR   whd_resload;                      /* funkcje WHDLoad (whdstart.s) */
UBYTE *whd_heap;                         /* koniec programu (whdstart.s) */
UBYTE *whd_exp;                          /* ExpMem (whdstart.s; 0 = brak) */
ULONG  whd_expsize;
static UBYTE *chip_ptr, *exp_ptr;

APTR AllocMem(ULONG size, ULONG flags)
{
    UBYTE *p = 0, *chip_end = (UBYTE *)(WHD_BASEMEM - WHD_STACK_RESERVE);
    ULONG i;
    if (!chip_ptr) {
        chip_ptr = (UBYTE *)(((ULONG)whd_heap + 15) & ~15UL);
        exp_ptr = whd_exp;
    }
    size = (size + 7) & ~7UL;
    if (!(flags & MEMF_CHIP) && exp_ptr && exp_ptr + size <= whd_exp + whd_expsize) {
        p = exp_ptr;
        exp_ptr += size;
    } else if (chip_ptr + size <= chip_end) {
        p = chip_ptr;
        chip_ptr += size;
    }
    if (p && (flags & MEMF_CLEAR))
        for (i = 0; i < size; i++) p[i] = 0;
    return p;
}

void FreeMem(APTR mem, ULONG size) { (void)mem; (void)size; }

ULONG AvailMem(ULONG flags)
{
    (void)flags;
    return (ULONG)(WHD_BASEMEM - WHD_STACK_RESERVE) - (ULONG)chip_ptr;
}

/* ---------------- pliki ---------------- */

typedef struct { const char *name; ULONG size, pos; } WhdFile;
static WhdFile files[4];

static const char *whd_name(const char *n)
{
    const char *p = n, *s;
    for (s = n; *s; s++)
        if (*s == ':') p = s + 1;                        /* bez PROGDIR: */
    if (p[0] == 'd' && p[1] == 'a' && p[2] == 't' && p[3] == 'a' && p[4] == '/')
        p += 5;                                          /* katalog WHDLoad to data/ */
    return p;
}

BPTR Open(CONST_STRPTR name, LONG mode)
{
    UBYTE i;
    const char *n = whd_name((const char *)name);
    ULONG size;
    if (mode != MODE_OLDFILE) return 0;                  /* zapis tylko przez whd_save_file */
    size = whd_getsize(n);
    if (!size) return 0;
    for (i = 0; i < 4; i++)
        if (!files[i].name) {
            files[i].name = n;
            files[i].size = size;
            files[i].pos = 0;
            return (BPTR)(i + 1);
        }
    return 0;
}

LONG Read(BPTR fh, APTR buf, LONG len)
{
    WhdFile *f = &files[fh - 1];
    ULONG n = (ULONG)len;
    if (f->pos >= f->size) return 0;
    if (n > f->size - f->pos) n = f->size - f->pos;
    if (!whd_loadoffset(n, f->pos, f->name, buf)) return -1;
    f->pos += n;
    return (LONG)n;
}

LONG Seek(BPTR fh, LONG position, LONG mode)
{
    WhdFile *f = &files[fh - 1];
    LONG old = (LONG)f->pos;
    if (mode == OFFSET_END) f->pos = f->size + position;
    else if (mode == OFFSET_BEGINNING) f->pos = (ULONG)position;
    else f->pos += position;
    return old;
}

LONG Close(BPTR fh)
{
    files[fh - 1].name = 0;
    return 1;
}

BOOL whd_save_file(const char *name, const void *src, ULONG size)
{
    return whd_savefile(size, whd_name(name), src);
}

/* kompilator moze wolac te funkcje przy kopiowaniu struktur - bez biblioteki C */
void *memcpy(void *d, const void *s, unsigned long n)
{
    UBYTE *dd = d;
    const UBYTE *ss = s;
    while (n--) *dd++ = *ss++;
    return d;
}

void *memmove(void *d, const void *s, unsigned long n)
{
    UBYTE *dd = d;
    const UBYTE *ss = s;
    if (dd < ss) while (n--) *dd++ = *ss++;
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
    return d;
}

void *memset(void *d, int c, unsigned long n)
{
    UBYTE *dd = d;
    while (n--) *dd++ = (UBYTE)c;
    return d;
}
