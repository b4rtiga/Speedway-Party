/* pack_test.c - test rozpakowywania danych na PC: asset_load z gry (src/assets.c) czyta pliki
 * spakowane przez tools/pack.py i musi dac dokladnie dane z data/; uszkodzone pliki maja dac
 * FALSE (bez wyjscia poza bufor), a niespakowane czytac sie jak dotad.
 *   sh tools/sim/pack_test.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include "game.h"

/* atrapy DOS i exec na stdio (BPTR = numer w tablicy + 1) */
static FILE *files[8];
BPTR Open(CONST_STRPTR name, LONG mode)
{
    int i;
    (void)mode;
    for (i = 0; i < 8; i++)
        if (!files[i]) { files[i] = fopen((const char *)name, "rb"); return files[i] ? i + 1 : 0; }
    return 0;
}
LONG Read(BPTR f, APTR b, LONG n) { return (LONG)fread(b, 1, (size_t)n, files[f - 1]); }
LONG Seek(BPTR f, LONG pos, LONG mode)
{
    LONG old = ftell(files[f - 1]);
    fseek(files[f - 1], pos, mode == OFFSET_END ? SEEK_END : mode == OFFSET_BEGINNING ? SEEK_SET : SEEK_CUR);
    return old;
}
BOOL Close(BPTR f) { fclose(files[f - 1]); files[f - 1] = 0; return TRUE; }
APTR AllocMem(ULONG size, ULONG flags) { (void)flags; return calloc(1, size); }
void FreeMem(APTR p, ULONG size) { (void)size; free(p); }

static int fails;
#define CHECK(cond, what) do { if (!(cond)) { printf("BLAD: %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)

static long file_size(const char *p)
{
    FILE *f = fopen(p, "rb");
    long n;
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n;
}

static UBYTE *read_all(const char *p, long n)
{
    UBYTE *b = malloc((size_t)n);
    FILE *f = fopen(p, "rb");
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = 0; }
    fclose(f);
    return b;
}

static void write_all(const char *p, const UBYTE *b, long n)
{
    FILE *f = fopen(p, "wb");
    fwrite(b, 1, (size_t)n, f);
    fclose(f);
}

int main(void)
{
    static const char *names[] = { "bikes.spr", "tractors.spr", "track0.bpl", "girls.bin" };
    char raw[64], pk[64], msg[96];
    int i;
    for (i = 0; i < 4; i++) {
        long n, pn;
        UBYTE *want, *got, *blob;
        sprintf(raw, "data/%s", names[i]);
        sprintf(pk, "tools/sim/out/%s", names[i]);
        n = file_size(raw);
        pn = file_size(pk);
        want = read_all(raw, n);
        got = calloc(1, (size_t)n);
        sprintf(msg, "%s: %ld -> %ld B, rozpakowany identyczny", names[i], n, pn);
        CHECK(pn > 0 && pn < n && asset_load(pk, got, n) && !memcmp(got, want, (size_t)n), msg);
        sprintf(msg, "%s: niespakowany czyta sie jak dotad", names[i]);
        memset(got, 0, (size_t)n);
        CHECK(asset_load(raw, got, n) && !memcmp(got, want, (size_t)n), msg);
        blob = read_all(pk, pn);
        if (i == 0) {
            /* uszkodzenia: obciety, zla dlugosc w naglowku, zla odleglosc, zly magic */
            write_all("tools/sim/out/bad.bin", blob, pn - 3);
            CHECK(!asset_load("tools/sim/out/bad.bin", got, n), "obciety plik odrzucony");
            blob[7] ^= 1;
            write_all("tools/sim/out/bad.bin", blob, pn);
            CHECK(!asset_load("tools/sim/out/bad.bin", got, n), "zla dlugosc w naglowku odrzucona");
            blob[7] ^= 1;
            {
                long s = 8;                               /* pierwszy token powtorzenia -> odleglosc za daleko */
                while (s < pn && blob[s] < 0x80) s += blob[s] + 2;
                if (s + 2 < pn) { blob[s + 1] = 0xFF; blob[s + 2] = 0xFF; }
            }
            write_all("tools/sim/out/bad.bin", blob, pn);
            CHECK(!asset_load("tools/sim/out/bad.bin", got, n), "odleglosc poza rozpakowanymi danymi odrzucona");
            blob = read_all(pk, pn);
            blob[0] = 'X';
            write_all("tools/sim/out/bad.bin", blob, pn);
            CHECK(!asset_load("tools/sim/out/bad.bin", got, n), "zly naglowek odrzucony");
        }
        free(blob); free(want); free(got);
    }
    printf(fails ? "\n%d BLEDOW\n" : "\nOK: rozpakowywanie danych\n", fails);
    return fails ? 1 : 0;
}
