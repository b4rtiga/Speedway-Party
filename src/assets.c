/* assets.c - ladowanie danych z PROGDIR:data/ (wywolywac PRZED system_takeover,
 * bo potrzebuje dos.library). Dane generuje tools/genart.py. */
#include <dos/dos.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "game.h"

BOOL asset_load(const char *name, void *dst, LONG size)
{
    BPTR fh = Open((CONST_STRPTR)name, MODE_OLDFILE);
    LONG n;
    if (!fh) return FALSE;
    n = Read(fh, dst, size);
    Close(fh);
    return n == size;
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
