/* stubs.c - zastepniki sprzetu dla symulatora (bez obrazu i dzwieku). */
#include <stdlib.h>
#include "game.h"
#include "video.h"
#include "blit.h"

Video g_video;

APTR AllocMem(ULONG size, ULONG flags) { (void)flags; return calloc(1, size); }
void FreeMem(APTR p, ULONG size) { (void)size; free(p); }
UWORD *g_blank_sprite;

void video_set_play_palette(const RGB24 *pal) { (void)pal; }
void video_set_hud_palette(const RGB24 *pal) { (void)pal; }
void video_set_hud_color(UBYTE i, UBYTE r, UBYTE g, UBYTE b) { (void)i; (void)r; (void)g; (void)b; }
void video_blend_track_palette(UBYTE wet) { (void)wet; }
void video_blend_track_palette_lazy(UBYTE wet) { (void)wet; }
void video_palette_tick(void) { }
BOOL blit_init(void) { return TRUE; }
void blit_free(void) {}
void blit_wait(void) {}
void blit_dot(UBYTE *buf, WORD x, WORD y, BOOL big, UBYTE color) { (void)buf; (void)x; (void)y; (void)big; (void)color; }
void blit_restore(UBYTE *d, const UBYTE *s, WORD x, WORD y, BOOL big) { (void)d; (void)s; (void)x; (void)y; (void)big; }
void blit_dots_begin(void) {}
void blit_plot(UBYTE *buf, WORD x, WORD y, UBYTE c)          /* jak blit.c (tlo toru w track_render) */
{
    UBYTE *p = buf + (LONG)y * PLAY_STRIDE + (x >> 3);
    UBYTE bit = (UBYTE)(0x80 >> (x & 7)), pl;
    for (pl = 0; pl < PLAY_DEPTH; pl++, p += ROW_BYTES, c >>= 1)
        if (c & 1) *p |= bit; else *p &= (UBYTE)~bit;
}
void blit_restores_begin(void) {}
void blit_copy(UBYTE *d, const UBYTE *s, WORD x, WORD y, WORD w, WORD h) { (void)d; (void)s; (void)x; (void)y; (void)w; (void)h; }
void sfx_buzzer(void) {}
void sfx_whistle(void) {}
BOOL asset_load(const char *name, void *dst, LONG size) { (void)name; (void)dst; (void)size; return FALSE; }

void stubs_init(void)
{
    g_video.background = calloc(1, PLAY_SIZE);
    g_video.play[0] = calloc(1, PLAY_SIZE);
    g_video.play[1] = calloc(1, PLAY_SIZE);
}
