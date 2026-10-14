/* host.c - podmiany sprzetu i systemu dla zrzutow ekranu na PC:
 * Blitter w oprogramowaniu (te same operacje co blit.c), przechwytywanie palet,
 * kolorow per linia i wskaznikow duszkow, ladowanie data/ zamiast PROGDIR:data/. */
#include <dos/dosextens.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "video.h"
#include "blit.h"

volatile UWORD g_fake_custom[0x100];
volatile ULONG g_frame;
BOOL g_paused, g_pause_quit;
ULONG g_lag;
struct ExecBase *SysBase;
Video g_video;

RGB24  cur_play[256], cur_hud[32], cur_menu[256];
UWORD  cap_bg[FULL_H], cap_fg[FULL_H];
const UWORD *cap_spr[8];

APTR AllocMem(ULONG size, ULONG flags) { (void)flags; return calloc(1, size); }
void FreeMem(APTR p, ULONG size) { (void)size; free(p); }

/* audio.c: probki nie sa potrzebne - trailer czyta tylko rejestry okresu i glosnosci */
void *asset_load_alloc(const char *name, ULONG *size, ULONG memflags) { (void)name; (void)size; (void)memflags; return 0; }

BOOL asset_load(const char *name, void *dst, LONG size)
{
    char path[256];
    FILE *f;
    LONG n;
    const char *p = strstr(name, "data/");
    snprintf(path, sizeof(path), "%s", p ? p : name);
    f = fopen(path, "rb");
    if (!f) return FALSE;
    n = (LONG)fread(dst, 1, (size_t)size, f);
    fclose(f);
    return n == size;
}

void video_set_play_palette(const RGB24 *pal) { memcpy(cur_play, pal, sizeof(cur_play)); memcpy(cur_menu, pal, sizeof(cur_menu)); }
void video_set_menu_palette(const RGB24 *pal) { memcpy(cur_menu, pal, sizeof(cur_menu)); }
void video_set_hud_palette(const RGB24 *pal) { memcpy(cur_hud, pal, sizeof(cur_hud)); }
void video_set_hud_color(UBYTE i, UBYTE r, UBYTE g, UBYTE b) { cur_hud[i].r = r; cur_hud[i].g = g; cur_hud[i].b = b; }
void video_palette_tick(void) { }
void video_blend_track_palette_lazy(UBYTE wet);
void video_blend_track_palette(UBYTE wet)
{
    UWORD i;
    for (i = 0; i < 256; i++) {
        const RGB24 *d = &g_video.pal_dry[i], *w = &g_video.pal_wet[i];
        cur_play[i].r = (UBYTE)(d->r + (((WORD)w->r - d->r) * wet >> 8));
        cur_play[i].g = (UBYTE)(d->g + (((WORD)w->g - d->g) * wet >> 8));
        cur_play[i].b = (UBYTE)(d->b + (((WORD)w->b - d->b) * wet >> 8));
    }
    memcpy(cur_menu, cur_play, sizeof(cur_menu));       /* jak w grze: obie listy Coppera */
}
void video_blend_track_palette_lazy(UBYTE wet) { video_blend_track_palette(wet); }   /* PC: od razu */
void video_set_lines(const UWORD *bg, const UWORD *fg)
{
    size_t n = (g_video.menu ? FULL_H : PLAY_H) * sizeof(UWORD);
    memcpy(cap_bg, bg, n); memcpy(cap_fg, fg, n);
}
void video_set_menu(BOOL menu) { g_video.menu = (UBYTE)(menu ? 1 : 0); }
void video_set_sprite(UBYTE n, const UWORD *data) { cap_spr[n] = data; }
UBYTE *video_backbuffer(void) { return g_video.play[g_video.back]; }

BPTR Open(CONST_STRPTR name, LONG mode) { (void)name; (void)mode; return 0; }   /* records.c: bez zapisu */
LONG Read(BPTR f, APTR b, LONG n) { (void)f; (void)b; (void)n; return -1; }
LONG Write(BPTR f, CONST APTR b, LONG n) { (void)f; (void)b; return n; }
BOOL Close(BPTR f) { (void)f; return TRUE; }
BOOL Rename(CONST_STRPTR a, CONST_STRPTR b) { (void)a; (void)b; return FALSE; }
BOOL DeleteFile(CONST_STRPTR a) { (void)a; return FALSE; }
struct Task *FindTask(CONST_STRPTR n) { static struct Process p; (void)n; return (struct Task *)&p; }
UBYTE input_menu(void) { return 0; }
UBYTE key_get(void) { return 0xFF; }
BOOL  key_down(UBYTE k) { (void)k; return FALSE; }
UBYTE input_read(InputPort p) { (void)p; return 0; }

/* ----------------------- Blitter w oprogramowaniu ----------------------- */
static void put_px(UBYTE *buf, WORD x, WORD y, UBYTE c)
{
    UBYTE *p = buf + (LONG)y * PLAY_STRIDE + (x >> 3), bit = (UBYTE)(0x80 >> (x & 7)), pl;
    for (pl = 0; pl < PLAY_DEPTH; pl++, p += ROW_BYTES, c >>= 1)
        *p = (c & 1) ? (UBYTE)(*p | bit) : (UBYTE)(*p & ~bit);
}
BOOL blit_init(void) { return TRUE; }
void blit_free(void) {}
void blit_wait(void) {}
void blit_clear(UBYTE *buf) { memset(buf, 0, FULL_SIZE); }
void blit_fill(UBYTE *buf, WORD x, WORD y, WORD w, WORD h, UBYTE c)
{
    WORD i, j;
    if ((x | w) & 15) fprintf(stderr, "UWAGA blit_fill: x=%d w=%d nie jest wielokrotnoscia 16!\n", x, w);
    for (j = 0; j < h; j++) for (i = 0; i < w; i++) put_px(buf, x + i, y + j, c);
}
void blit_copy(UBYTE *dst, const UBYTE *src, WORD x, WORD y, WORD w, WORD h)
{
    WORD j, pl;
    if ((x | w) & 15) fprintf(stderr, "UWAGA blit_copy: x=%d w=%d nie jest wielokrotnoscia 16!\n", x, w);
    for (j = 0; j < h; j++)
        for (pl = 0; pl < PLAY_DEPTH; pl++) {
            LONG o = (LONG)(y + j) * PLAY_STRIDE + pl * ROW_BYTES + (x >> 3);
            memcpy(dst + o, src + o, (size_t)(w >> 3));
        }
}
void blit_dot(UBYTE *buf, WORD x, WORD y, BOOL big, UBYTE color)
{
    put_px(buf, x, y, color);
    if (big) { put_px(buf, x + 1, y, color); put_px(buf, x, y + 1, color); put_px(buf, x + 1, y + 1, color); }
}
void blit_dots_begin(void) {}
void blit_restores_begin(void) {}
void blit_restore(UBYTE *dst, const UBYTE *src, WORD x, WORD y, BOOL big)
{
    WORD r, pl;
    for (r = 0; r < (big ? 2 : 1); r++)
        for (pl = 0; pl < PLAY_DEPTH; pl++) {
            LONG o = (LONG)(y + r) * PLAY_STRIDE + pl * ROW_BYTES + ((x >> 4) << 1);
            memcpy(dst + o, src + o, 4);
        }
}

void host_init(void)
{
    g_video.play[0] = calloc(1, FULL_SIZE);
    g_video.play[1] = calloc(1, FULL_SIZE);
    g_video.background = calloc(1, PLAY_SIZE);
    g_video.hud = calloc(1, HUD_SIZE);
    g_video.front = 0;
    g_video.back = 1;
}
