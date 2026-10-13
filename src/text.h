/* text.h - tekst na ekranie 8 bitplanow: font 8x10 z polskimi literami, skala x1 i x2.
 * Napisy w kodzie sa w UTF-8 (Ą Ć Ę Ł Ń Ó Ś Ź Ż), nazwiska trzymamy w kodach 0x80..0x88.
 * Male litery a-z maja wlasne glify; male polskie znaki rysowane sa jako wielkie. */
#ifndef TEXT_H
#define TEXT_H

#include <exec/types.h>

#define FONT_W       8
#define FONT_H       10
#define FONT_GLYPHS  100         /* ASCII 32..95, polskie (9), male litery a-z (26), (c) */
#define LOWER_FIRST  73
#define GLYPH_COPY   99          /* znak (c), w napisach UTF-8 */
#define PL_FIRST     0x80          /* Ą Ć Ę Ł Ń Ó Ś Ź Ż = 0x80..0x88 */
#define PL_COUNT     9

extern const UBYTE g_font[FONT_GLYPHS][FONT_H];

/* x musi byc wielokrotnoscia 8. shadow != 0 -> cien o 1 linie nizej w kolorze shadow. */
void  text_draw(UBYTE *buf, WORD x, WORD y, const char *s, UBYTE color, UBYTE shadow);
void  text_draw_big(UBYTE *buf, WORD x, WORD y, const char *s, UBYTE color, UBYTE shadow);
WORD  text_len(const char *s);                     /* liczba znakow (UTF-8) */
WORD  text_center_x(const char *s, UBYTE scale);
void  text_draw_center(UBYTE *buf, WORD y, const char *s, UBYTE color, UBYTE shadow);
void  text_draw_big_center(UBYTE *buf, WORD y, const char *s, UBYTE color, UBYTE shadow);

/* liczba -> tekst (bez sprintf), zwraca wskaznik na koniec */
char *text_uint(char *dst, ULONG v, UBYTE min_digits);
char *text_cat(char *dst, const char *src);

#endif
