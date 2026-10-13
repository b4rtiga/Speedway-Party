/* video.h - ekran AGA 8bpl, podwojne buforowanie, Copper, duszki 64 px. */
#ifndef VIDEO_H
#define VIDEO_H

#include <exec/types.h>

/* Uklad ekranu PAL lowres 320x256, dwa tryby (osobne Copper-listy):
 *   WYSCIG: linie 0..207 - pole gry 8 bitplanow (256 kolorow), double-buffer
 *           linie 208..255 - HUD 48 linii, osobny ekran 5 bitplanow (32 kolory)
 *           HUD zaczyna sie na linii wiazki $2C+208 = $FC i konczy za $FF.
 *   MENU:   linie 0..255 - caly ekran 8 bitplanow, bez HUD. */
#define SCREEN_W        320
#define PLAY_H          208
#define HUD_H           48
#define PLAY_DEPTH      8
#define HUD_DEPTH       5
#define ROW_BYTES       (SCREEN_W / 8)
#define DISPLAY_VSTART  0x2C
#define HUD_VSTART      (DISPLAY_VSTART + PLAY_H)   /* 0xFC */

/* Bitplany przeplatane (interleaved): jeden blit obejmuje wszystkie plany. */
#define PLAY_STRIDE     (ROW_BYTES * PLAY_DEPTH)    /* bajty na linie ekranu */
#define PLAY_SIZE       ((ULONG)PLAY_STRIDE * PLAY_H)
#define FULL_H          256                         /* ekran menu */
#define FULL_SIZE       ((ULONG)PLAY_STRIDE * FULL_H)
#define HUD_STRIDE      (ROW_BYTES * HUD_DEPTH)
#define HUD_SIZE        ((ULONG)HUD_STRIDE * HUD_H)

typedef struct RGB24 { UBYTE r, g, b; } RGB24;

typedef struct Video {
    UBYTE *play[2];          /* bufory pola gry, FULL_H linii (Chip RAM) */
    UBYTE *background;       /* czysty tor - do wymazywania czasteczek */
    UBYTE *hud;
    UWORD *copper[2][2];     /* [tryb: 0 wyscig, 1 menu][bufor] */
    UBYTE  front, back;
    UBYTE  menu;             /* tryb wybierany przy nastepnym video_swap */

    /* wskazniki do miejsc w Copper-listach, ktore CPU latwo podmienia */
    UWORD *cop_play_pal[2][2];  /* 256 kolorow pola gry (hi+lo) */
    UWORD *cop_sprites[2][2];   /* SPR0PT..SPR7PT */
    UWORD *cop_hud_pal[2];      /* 32 kolory HUD (tylko listy wyscigowe) */
    UWORD *cop_lines[2][2];     /* kolory per linia: tlo (COLOR00) i tekst (COLOR100) */

    RGB24  pal_dry[256];
    RGB24  pal_wet[256];
    RGB24  pal_hud[32];
} Video;

extern Video g_video;

BOOL  video_init(void);
void  video_free(void);
void  video_start(void);                 /* wlacza DMA i Copper */
void  video_set_play_palette(const RGB24 *pal);
void  video_set_hud_palette(const RGB24 *pal);
void  video_set_hud_color(UBYTE index, UBYTE r, UBYTE g, UBYTE b);
void  video_blend_track_palette(UBYTE wet); /* 0 = sucho, 255 = mokro */
void  video_set_sprite(UBYTE n, const UWORD *data);
void  video_set_lines(const UWORD *bg12, const UWORD *fg12);   /* PLAY_H (wyscig) / FULL_H (menu) wartosci */
void  video_set_menu(BOOL menu);         /* tryb ekranu od nastepnej zamiany buforow */
void  video_swap(void);                  /* zamiana buforow na kolejnej ramce */
UBYTE *video_backbuffer(void);

/* sprites.c */
BOOL   sprites_init(void);
void   sprites_free(void);
extern UWORD *g_blank_sprite;
#define SPRITE_W     32          /* AGA: duszki 32 px (FMODE SPR32) - motocykl miesci sie w 32x32 */
#define SPRITE_H     32
#define SKID_FRAMES  3           /* 0 = prosto, 1-2 = coraz mocniejszy uslizg */
UWORD *sprite_frame_ptr(UBYTE gate, UBYTE frame, UBYTE skid, UBYTE half);
void   sprite_set_pos(UWORD *spr, WORD x, WORD y, UWORD height, BOOL attached);

#endif
