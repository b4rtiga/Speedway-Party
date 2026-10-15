/* assets.c - ladowanie danych z PROGDIR:data/ (wywolywac PRZED system_takeover,
 * bo potrzebuje dos.library). Dane generuje tools/genart.py.
 *
 * Pliki na dyskietce moga byc spakowane (tools/pack.py, przy make dist): "SPZ1",
 * dlugosc po rozpakowaniu, tokeny LZ. asset_load rozpoznaje je po tym, ze plik jest
 * krotszy od oczekiwanego rozmiaru danych - plik pelnej dlugosci czyta jak dotad. */
#include <dos/dos.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "game.h"

#define PACK_MAGIC 0x53505A31UL           /* "SPZ1" */

/* tokeny: c < 0x80 -> c + 1 literalow; c >= 0x80 -> (c & 0x7F) + 3 bajtow
 * z odleglosci d (2 bajty) wstecz; kazde wyjscie poza bufory = zly plik */
static ULONG be32(const UBYTE *p) { return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3]; }

static BOOL unpack(const UBYTE *s, LONG n, UBYTE *d, LONG size)
{
    const UBYTE *se = s + n;
    UBYTE *o = d, *oe = d + size;
    while (s < se) {
        UBYTE c = *s++;
        UWORD k;
        if (c < 0x80) {
            k = (UWORD)(c + 1);
            if (k > se - s || k > oe - o) return FALSE;
            while (k--) *o++ = *s++;
        } else {
            const UBYTE *m;
            UWORD off;
            k = (UWORD)((c & 0x7F) + 3);
            if (se - s < 2) return FALSE;
            off = (UWORD)((s[0] << 8) | s[1]);
            s += 2;
            if (!off || off > o - d || k > oe - o) return FALSE;
            m = o - off;
            while (k--) *o++ = *m++;                     /* moze nachodzic (off < k) */
        }
    }
    return (BOOL)(o == oe);
}

BOOL asset_load(const char *name, void *dst, LONG size)
{
    BPTR fh = Open((CONST_STRPTR)name, MODE_OLDFILE);
    LONG len, n;
    BOOL ok = FALSE;
    if (!fh) return FALSE;
    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    if (len >= size) {
        n = Read(fh, dst, size);                         /* niespakowany */
        ok = (BOOL)(n == size);
    } else if (len > 8) {
        UBYTE hdr[8];
        if (Read(fh, hdr, 8) == 8 && be32(hdr) == PACK_MAGIC && (LONG)be32(hdr + 4) == size) {
            UBYTE *pk = AllocMem((ULONG)(len - 8), MEMF_ANY);
            if (pk) {
                if (Read(fh, pk, len - 8) == len - 8) ok = unpack(pk, len - 8, (UBYTE *)dst, size);
                FreeMem(pk, (ULONG)(len - 8));
            }
        }
    }
    Close(fh);
    return ok;
}

/* plik o nieznanej dlugosci -> nowy blok pamieci (np. MEMF_CHIP dla probek Pauli) */
void *asset_load_alloc(const char *name, ULONG *size, ULONG memflags)
{
    BPTR fh = Open((CONST_STRPTR)name, MODE_OLDFILE);
    LONG len;
    void *mem;
    if (!fh) return 0;
    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    mem = len > 0 ? AllocMem((ULONG)len, memflags) : 0;
    if (mem && Read(fh, mem, len) != len) { FreeMem(mem, (ULONG)len); mem = 0; }
    Close(fh);
    if (mem) *size = (ULONG)len;
    return mem;
}
