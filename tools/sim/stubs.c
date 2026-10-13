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
BOOL blit_init(void) { return TRUE; }
void blit_free(void) {}
void blit_dot(UBYTE *buf, WORD x, WORD y, BOOL big, UBYTE color) { (void)buf; (void)x; (void)y; (void)big; (void)color; }
void blit_restore(UBYTE *d, const UBYTE *s, WORD x, WORD y, BOOL big) { (void)d; (void)s; (void)x; (void)y; (void)big; }
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
