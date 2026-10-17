/* text.c - rysowanie tekstu bajtami (x wyrownany do 8 px), osobno w kazdym bitplanie. */
#include "text.h"
#include "video.h"
#include "hw.h"

/* UTF-8 -> indeks glifu; obsluguje tez wewnetrzne kody 0x80..0x88 (nazwiska) */
static UBYTE next_glyph(const UBYTE **ps)
{
    const UBYTE *s = *ps;
    UBYTE c = *s++, idx;
    if (c < 0x80) {
        if (c >= 'a' && c <= 'z') idx = (UBYTE)(LOWER_FIRST + c - 'a');
        else idx = (c >= 32 && c < 96) ? (UBYTE)(c - 32) : 0;
    } else if (c < PL_FIRST + PL_COUNT) {
        idx = (UBYTE)(64 + c - PL_FIRST);
    } else {
        UBYTE c2 = *s ? *s++ : 0;
        UWORD u = (UWORD)(((c & 0x1F) << 6) | (c2 & 0x3F));
        switch (u) {
        case 0x104: case 0x105: idx = 64; break;   /* Ą */
        case 0x106: case 0x107: idx = 65; break;   /* Ć */
        case 0x118: case 0x119: idx = 66; break;   /* Ę */
        case 0x141: case 0x142: idx = 67; break;   /* Ł */
        case 0x143: case 0x144: idx = 68; break;   /* Ń */
        case 0xD3:  case 0xF3:  idx = 69; break;   /* Ó */
        case 0x15A: case 0x15B: idx = 70; break;   /* Ś */
        case 0x179: case 0x17A: idx = 71; break;   /* Ź */
        case 0x17B: case 0x17C: idx = 72; break;   /* Ż */
        case 0xA9:  idx = GLYPH_COPY; break;       /* (c) */
        default:    idx = 0; break;
        }
    }
    *ps = s;
    return idx;
}

WORD text_len(const char *s)
{
    const UBYTE *p = (const UBYTE *)s;
    WORD n = 0;
    while (*p) { next_glyph(&p); n++; }
    return n;
}

WORD text_center_x(const char *s, UBYTE scale)
{
    WORD w = (WORD)(text_len(s) * FONT_W * scale);
    return (WORD)(((SCREEN_W - w) / 2) & ~7);
}

/* zapis jednego bajtu maski do wszystkich planow: kolor przez bity planow */
static inline void put_byte(UBYTE *p, UBYTE mask, UBYTE color)
{
    UBYTE pl;
    for (pl = 0; pl < PLAY_DEPTH; pl++, p += ROW_BYTES, color >>= 1)
        *p = (color & 1) ? (UBYTE)(*p | mask) : (UBYTE)(*p & ~mask);
}

static void draw_pass(UBYTE *buf, WORD x, WORD y, const char *s, UBYTE color)
{
    const UBYTE *p = (const UBYTE *)s;
    hw_wait_blit();
    while (*p && x <= SCREEN_W - FONT_W) {
        const UBYTE *g = g_font[next_glyph(&p)];
        WORD r;
        for (r = 0; r < FONT_H; r++) {
            WORD yy = y + r;
            if (g[r] && yy >= 0 && yy < FULL_H) {
                UBYTE *d = buf + (LONG)yy * PLAY_STRIDE + (x >> 3);
                UBYTE sh = (UBYTE)(x & 7);                 /* x nie musi byc wielokrotnoscia 8 */
                put_byte(d, (UBYTE)(g[r] >> sh), color);
                if (sh && (x >> 3) < ROW_BYTES - 1) put_byte(d + 1, (UBYTE)(g[r] << (8 - sh)), color);
            }
        }
        x += FONT_W;
    }
}

WORD g_clip_y0 = 0, g_clip_y1 = 32767;

static BOOL clipped(WORD y, WORD h) { return (BOOL)(y + h <= g_clip_y0 || y >= g_clip_y1); }

void text_draw(UBYTE *buf, WORD x, WORD y, const char *s, UBYTE color, UBYTE shadow)
{
    if (clipped(y, FONT_H + 1)) return;
    if (shadow) draw_pass(buf, x, y + 1, s, shadow);
    draw_pass(buf, x, y, s, color);
}

/* x2: kazdy bit -> 2 bity, kazdy wiersz -> 2 wiersze */
static UWORD expand(UBYTE b)
{
    UWORD r = 0, i;
    for (i = 0; i < 8; i++)
        if (b & (0x80 >> i)) r |= (UWORD)(0xC000 >> (i * 2));
    return r;
}

static void draw_big_pass(UBYTE *buf, WORD x, WORD y, const char *s, UBYTE color)
{
    const UBYTE *p = (const UBYTE *)s;
    hw_wait_blit();
    while (*p && x <= SCREEN_W - FONT_W * 2) {
        const UBYTE *g = g_font[next_glyph(&p)];
        WORD r, k;
        for (r = 0; r < FONT_H; r++) {
            UWORD w = expand(g[r]);
            if (!w) continue;
            for (k = 0; k < 2; k++) {
                WORD yy = y + r * 2 + k;
                UBYTE *d;
                if (yy < 0 || yy >= FULL_H) continue;
                d = buf + (LONG)yy * PLAY_STRIDE + (x >> 3);
                put_byte(d, (UBYTE)(w >> 8), color);
                put_byte(d + 1, (UBYTE)w, color);
            }
        }
        x += FONT_W * 2;
    }
}

void text_draw_big(UBYTE *buf, WORD x, WORD y, const char *s, UBYTE color, UBYTE shadow)
{
    if (clipped(y, FONT_H * 2 + 2)) return;
    if (shadow) draw_big_pass(buf, x, y + 2, s, shadow);
    draw_big_pass(buf, x, y, s, color);
}

/* maly font (5 px na znak): podpowiedzi o klawiszach na dole ekranow */
void text_draw_small(UBYTE *buf, WORD x, WORD y, const char *s, UBYTE color)
{
    const UBYTE *p = (const UBYTE *)s;
    if (clipped(y, FONT_SMALL_H)) return;
    hw_wait_blit();
    while (*p && x <= SCREEN_W - FONT_SMALL_W) {
        const UBYTE *g = g_font_small[next_glyph(&p)];
        WORD r;
        for (r = 0; r < FONT_SMALL_H; r++) {
            WORD yy = y + r;
            if (g[r] && yy >= 0 && yy < FULL_H) {
                UBYTE *d = buf + (LONG)yy * PLAY_STRIDE + (x >> 3);
                UBYTE sh = (UBYTE)(x & 7);
                put_byte(d, (UBYTE)(g[r] >> sh), color);
                if (sh > 4 && (x >> 3) < ROW_BYTES - 1) put_byte(d + 1, (UBYTE)(g[r] << (8 - sh)), color);
            }
        }
        x += FONT_SMALL_W;
    }
}

void text_draw_small_center(UBYTE *buf, WORD y, const char *s, UBYTE color)
{
    text_draw_small(buf, (WORD)((SCREEN_W - text_len(s) * FONT_SMALL_W) / 2), y, s, color);
}

void text_draw_center(UBYTE *buf, WORD y, const char *s, UBYTE color, UBYTE shadow)
{
    text_draw(buf, text_center_x(s, 1), y, s, color, shadow);
}

void text_draw_big_center(UBYTE *buf, WORD y, const char *s, UBYTE color, UBYTE shadow)
{
    text_draw_big(buf, text_center_x(s, 2), y, s, color, shadow);
}

char *text_uint(char *dst, ULONG v, UBYTE min_digits)
{
    char tmp[11];
    UBYTE n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n < min_digits) tmp[n++] = '0';
    while (n) *dst++ = tmp[--n];
    *dst = 0;
    return dst;
}

char *text_cat(char *dst, const char *src)
{
    while (*src) *dst++ = *src++;
    *dst = 0;
    return dst;
}
