/* blit.h - Blitter dla buforow pola gry (8 planow, przeplatane).
 * x i w w pikselach musza byc wielokrotnoscia 16 (pelne slowa, bez masek krawedzi). */
#ifndef BLIT_H
#define BLIT_H

#include <exec/types.h>
#include "video.h"                         /* PLAY_STRIDE, ROW_BYTES, PLAY_DEPTH */

/* piksel w kolorze c (8 planow przeplatanych) procesorem - ui.c i track.c */
void blit_plot(UBYTE *buf, WORD x, WORD y, UBYTE c);

void blit_clear(UBYTE *buf);                                         /* caly bufor (FULL_H) -> kolor 0 */
void blit_fill(UBYTE *buf, WORD x, WORD y, WORD w, WORD h, UBYTE color);
void blit_copy(UBYTE *dst, const UBYTE *src, WORD x, WORD y, WORD w, WORD h);

/* Czasteczki: dowolne x (przesuniecie Blittera), blok 2 slowa x 1-4 linie x 8 planow.
 * Ksztalt: DOT_SMALL 1 px, DOT_BIG 2x2, DOT_RAIN kreska 4 linie skosnie (deszcz - jeden kolor
 * naraz, wzor przeliczany przy zmianie). x <= SCREEN_W - 17, y <= PLAY_H - DOT_ROWS(ksztalt). */
#define DOT_SMALL 0
#define DOT_BIG   1
#define DOT_RAIN  2
#define DOT_ROWS(s) ((s) == DOT_RAIN ? 4 : (s) == DOT_BIG ? 2 : 1)
BOOL blit_init(void);
void blit_wait(void);                                                /* czeka, az Blitter skonczy */
void blit_free(void);
void blit_dots_begin(void);                                           /* raz przed seria blit_dot */
void blit_dot(UBYTE *buf, WORD x, WORD y, UBYTE shape, UBYTE color);  /* cookie-cut, minterm $CA */
void blit_restores_begin(void);                                       /* raz przed seria blit_restore */
void blit_restore(UBYTE *dst, const UBYTE *src, WORD x, WORD y, UBYTE shape);

#endif
