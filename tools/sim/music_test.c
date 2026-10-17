/* Test muzyki w menu na PC: prawdziwy kod src/audio.c (dekoder ADPCM, dwa bufory DMA,
 * dopelnianie po kawalku) z prosta emulacja Pauli - co klatke "gra" ~320 probek z bufora,
 * na koncu bufora laduje nastepny z LC i ustawia flage AUD0. Kazda zagrana probka musi byc
 * rowna wzorcowi (tools/adpcm.py --decode albo surowy PCM), takze przy dlugich klatkach menu
 * (petla gry wolniejsza niz VBL) i po wielu petlach utworu.
 *   sh tools/sim/music_test.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/audio.c"

volatile UWORD g_fake_custom[0x100];
volatile ULONG g_frame;
Tournament g_tour;
BOOL g_paused;
ULONG g_rng = 1;
BOOL g_training;
UBYTE g_lap_cheer;
static const char *music_path;

APTR AllocMem(ULONG size, ULONG flags) { (void)flags; return calloc(1, size); }
void FreeMem(APTR p, ULONG size) { (void)size; free(p); }
LONG race_leader_distance(void) { return 0; }
LONG race_straight_entry(UBYTE lap) { (void)lap; return 0; }

void *asset_load_alloc(const char *name, ULONG *size, ULONG memflags)
{
    FILE *f;
    void *p;
    long n;
    (void)memflags;
    if (strcmp(name, "PROGDIR:data/music.raw")) return 0;      /* efekty: bez plikow */
    f = fopen(music_path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    p = malloc((size_t)n);
    if (fread(p, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(p); return 0; }
    fclose(f);
    *size = (ULONG)n;
    return p;
}

static unsigned char *load(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *p;
    if (!f) { printf("brak pliku %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    p = malloc((size_t)*n);
    if (fread(p, 1, (size_t)*n, f) != (size_t)*n) exit(1);
    fclose(f);
    return p;
}

int main(int argc, char **argv)
{
    long ref_n, k = 0, bad = 0, frames;
    unsigned char *ref;
    int playing = -1;
    long pos = 0, acc = 0;
    if (argc < 3) { printf("uzycie: music_test <music.raw> <wzorzec_s8.raw>\n"); return 1; }
    music_path = argv[1];
    ref = load(argv[2], &ref_n);

    g_music_enabled = TRUE;
    audio_init();
    if (!music.data) { printf("BLAD: muzyka nie wczytana\n"); return 1; }
    audio_start();
    g_tour.state = GS_TITLE;

    for (frames = 0; frames < 50L * 60; frames++) {
        /* petla gry: zwykle co klatke, czasem 4 klatki naraz (przerysowanie menu) */
        int step = (frames % 37 == 5) ? 4 : 1, s;
        g_frame += (ULONG)step;
        update_music();
        if (REG16(R_INTREQ)) { REG16(R_INTREQR) = 0; REG16(R_INTREQ) = 0; }   /* kasowanie flag */
        if (playing < 0) { playing = music_queued; REG16(R_INTREQR) = INTF_AUD0; continue; }   /* start DMA */
        for (s = 0; s < step; s++) {
            long n;
            acc += 15977;                              /* probek na klatke: 15977 / 50 */
            n = acc / 50; acc %= 50;
            while (n--) {
                /* ADPCM pisze 2 probki slowem (pierwsza w starszym bajcie - na Amidze nizszy adres);
                 * PC jest little endian, wiec tu pierwsza probka slowa lezy pod pos ^ 1 */
                unsigned char v = music_buf[playing][music_adpcm ? pos ^ 1 : pos];
                if (v != ref[k % ref_n]) {
                    if (bad < 5) printf("rozne: probka %ld (bufor %d, pozycja %ld): %d zamiast %d\n",
                                        k, playing, pos, (signed char)v, (signed char)ref[k % ref_n]);
                    bad++;
                }
                k++;
                if (++pos == (long)MUSIC_BUF) {        /* koniec bufora: Paula laduje LC */
                    pos = 0;
                    playing = music_queued;
                    REG16(R_INTREQR) = INTF_AUD0;
                }
            }
        }
    }
    printf("%s: %ld probek (%.1f petli utworu), %s, roznic: %ld\n", music_adpcm ? "ADPCM" : "PCM", k,
           (double)k / (double)ref_n, music_adpcm ? "dekoder zgodny z tools/adpcm.py" : "kopia 1:1", bad);
    printf(bad ? "BLAD: muzyka\n" : "OK: muzyka\n");
    return bad != 0;
}
