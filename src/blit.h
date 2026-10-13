/* blit.h - Blitter dla buforow pola gry (8 planow, przeplatane).
 * x i w w pikselach musza byc wielokrotnoscia 16 (pelne slowa, bez masek krawedzi). */
#ifndef BLIT_H
#define BLIT_H

#include <exec/types.h>

void blit_clear(UBYTE *buf);                                         /* caly bufor (FULL_H) -> kolor 0 */
void blit_fill(UBYTE *buf, WORD x, WORD y, WORD w, WORD h, UBYTE color);
void blit_copy(UBYTE *dst, const UBYTE *src, WORD x, WORD y, WORD w, WORD h);

/* Czasteczki: dowolne x (przesuniecie Blittera), blok 2 slowa x 1-2 linie x 8 planow.
 * x <= SCREEN_W - 17, y <= PLAY_H - 2. */
BOOL blit_init(void);
void blit_free(void);
void blit_dots_begin(void);                                           /* raz przed seria blit_dot */
void blit_dot(UBYTE *buf, WORD x, WORD y, BOOL big, UBYTE color);     /* cookie-cut, minterm $CA */
void blit_restores_begin(void);                                       /* raz przed seria blit_restore */
void blit_restore(UBYTE *dst, const UBYTE *src, WORD x, WORD y, BOOL big);

#endif
