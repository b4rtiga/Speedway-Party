/* sprites_test.c - test kopii klatek motocykli na PC: biblioteka z jednym kompletem
 * (data/bikes.spr) + przemalowanie kasku przy kopiowaniu musi dac dokladnie te same dane,
 * co plik z 4 kompletami kaskow (sciezka w argumencie: python tools/genart.py --bikes4 plik).
 *   sh tools/sim/sprites_test.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/memory.h>
#include "video.h"
#include "fixmath.h"
#include "game.h"

#define FRAME_WORDS (SPRITE_HALF_WORDS * 2)

APTR AllocMem(ULONG size, ULONG flags) { (void)flags; return calloc(1, size); }
void FreeMem(APTR p, ULONG size) { (void)size; free(p); }
BOOL asset_load(const char *name, void *dst, LONG size)
{
    const char *p = strstr(name, "data/");
    FILE *f = fopen(p ? p : name, "rb");
    LONG n;
    if (!f) return FALSE;
    n = (LONG)fread(dst, 1, (size_t)size, f);
    fclose(f);
    return n == size;
}

int main(int argc, char **argv)
{
    static UWORD old[4 * SPRITE_DIRS * SKID_FRAMES * FRAME_WORDS];
    FILE *f;
    int color, frame, skid, bad = 0, n = 0, helmet_px = 0;
    if (argc < 2 || !(f = fopen(argv[1], "rb"))) { printf("BLAD: brak starego pliku z 4 kompletami\n"); return 1; }
    if (fread(old, 1, sizeof(old), f) != sizeof(old)) { printf("BLAD: zla dlugosc starego pliku\n"); return 1; }
    fclose(f);
    if (!sprites_init()) { printf("BLAD: sprites_init (data/bikes.spr)\n"); return 1; }
    for (color = 0; color < 4; color++)
        for (frame = 0; frame < SPRITE_DIRS; frame++)
            for (skid = 0; skid < SKID_FRAMES; skid++) {
                /* motocykl i bufor zmieniaja sie, zeby sprawdzic tez pamiec kopii */
                const UWORD *got = sprite_bike_frame((UBYTE)(n & 3), (UBYTE)((n >> 2) & 1), (UBYTE)color, (UBYTE)frame, (UBYTE)skid);
                const UWORD *want = old + (((ULONG)color * SPRITE_DIRS + frame) * SKID_FRAMES + skid) * FRAME_WORDS;
                if (memcmp(got, want, FRAME_WORDS * 2)) bad++;
                if (color == 1 && memcmp(got, old + ((ULONG)frame * SKID_FRAMES + skid) * FRAME_WORDS, FRAME_WORDS * 2)) helmet_px++;
                n++;
            }
    printf("%d klatek sprawdzonych, %d roznych od starego pliku\n", n, bad);
    printf("kolor 1 rozni sie od koloru 0 w %d z %d klatek (kask widoczny)\n", helmet_px, SPRITE_DIRS * SKID_FRAMES);
    sprites_free();
    if (bad || helmet_px == 0) { printf("BLAD\n"); return 1; }
    printf("OK: przemalowane kaski identyczne z dawnymi 4 kompletami\n");
    return 0;
}
