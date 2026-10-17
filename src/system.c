/* system.c - wylaczenie OS (Forbid, LoadView(NULL), DMA/INTENA), wlasny VBL.
 *
 * Kolejnosc przejecia:
 *   1. LoadView(NULL) + 2x WaitTOF  -> graphics przestaje sterowac Copperem
 *   2. OwnBlitter + WaitBlit        -> Blitter nalezy do nas
 *   3. Forbid                       -> brak przelaczania taskow
 *   4. zapis DMACON/INTENA, wylaczenie wszystkiego
 *   5. wlasny wektor poziomu 3 (VBR+$6C; 68020 ma VBR, wiec odczytujemy go przez Supervisor)
 * Przywracanie w odwrotnej kolejnosci.
 *
 * Wczesniej (system_show_early) samo LoadView(NULL): plansza wczytywania z listy Coppera gry,
 * a system dziala dalej i DOS doczytuje dane z dyskietki. */
#include <exec/execbase.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/graphics.h>

#include "hw.h"
#include "system.h"

volatile ULONG g_frame;

#ifndef WHDLOAD
struct GfxBase *GfxBase;

static struct View *old_view;
static UWORD old_dmacon, old_intena;
static APTR  old_level3;
static volatile APTR *vbr;
static BOOL  shown_early, taken;          /* lista Coppera gry na ekranie / sprzet przejety */
static ULONG old_cache;                    /* stan pamieci podrecznej sprzed gry */

/* movec vbr,d0 ; rte - wykonywane w trybie nadzorcy */
static const UWORD getvbr_code[] __attribute__((aligned(4))) = { 0x4E7A, 0x0801, 0x4E73 };

static APTR get_vbr(void)
{
    if (SysBase->AttnFlags & AFF_68010)
        return (APTR)Supervisor((ULONG (*)())getvbr_code);
    return 0;
}

#endif

/* GCC m68k: atrybut bez argumentu (zapis rejestrow + RTE). Clang (tools/check.sh)
 * tylko sprawdza skladnie, wiec tam atrybut pomijamy. */
#if defined(__clang__)
#define ISR_ATTR
#else
#define ISR_ATTR __attribute__((interrupt))
#endif

void input_vbl(void);                   /* input.c: zatrzask joystickow */

static void ISR_ATTR level3_isr(void)
{
    UWORD req = REG16(R_INTREQR);
    if (req & INTF_VERTB) {
        g_frame++;
        input_vbl();
    }
    /* podwojny zapis: na szybkich CPU pierwszy moze nie zdazyc skasowac zadania */
    REG16(R_INTREQ) = INTF_VERTB | INTF_COPER;
    REG16(R_INTREQ) = INTF_VERTB | INTF_COPER;
}

#ifdef WHDLOAD
/* WHDLoad: system jest juz wylaczony, przerwania i DMA naleza do gry; wektor poziomu 3 pod $6C
 * (WHDLoad przekierowuje wektory na tablice od adresu 0, takze gdy przenosi VBR). */
BOOL system_show_early(void) { return TRUE; }
void system_hide_early(void) { }

BOOL system_takeover(void)
{
    REG16(R_INTENA) = INTF_ALL;
    REG16(R_INTREQ) = INTF_ALL;
    REG16(R_INTREQ) = INTF_ALL;
    REG16(R_DMACON) = DMAF_ALL;
    *(volatile APTR *)0x6C = (APTR)level3_isr;
    REG16(R_INTENA) = INTF_SETCLR | INTF_INTEN | INTF_VERTB;
    return TRUE;
}

void system_restore(void)
{
    REG16(R_INTENA) = INTF_ALL;
    REG16(R_DMACON) = DMAF_ALL;
    hw_wait_blit();
}
#else
static BOOL open_gfx(void)
{
    if (!GfxBase)
        GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    return (BOOL)(GfxBase != 0);              /* wymagany Kickstart 3.0+ (AGA) */
}

/* AGA: rejestry, ktorych OS nie zawsze ustawia sam, i lista Coppera systemu */
static void reset_display(void)
{
    REG16(R_FMODE)   = 0;
    REG16(R_BPLCON3) = 0x0C00;
    REG16(R_BPLCON4) = 0x0011;
    REG32(R_COP1LC) = (ULONG)GfxBase->copinit;
    REG16(R_COPJMP1) = 0;
}

/* Plansza wczytywania: graphics przestaje sterowac Copperem (LoadView(NULL)), liste gry
 * wpisuje potem video_start. Przerwania, DMA stacji dyskietek i przelaczanie taskow dzialaja
 * dalej, wiec DOS doczytuje pozostale dane. */
BOOL system_show_early(void)
{
    if (!open_gfx()) return FALSE;
    old_view = GfxBase->ActiView;
    LoadView(0);
    WaitTOF();
    WaitTOF();
    shown_early = TRUE;
    return TRUE;
}

/* blad przy wczytywaniu (sprzet nieprzejety): z powrotem ekran systemu */
void system_hide_early(void)
{
    if (!shown_early || taken) return;
    reset_display();
    LoadView(old_view);
    WaitTOF();
    WaitTOF();
    CloseLibrary((struct Library *)GfxBase);
    GfxBase = 0;
    shown_early = FALSE;
}

BOOL system_takeover(void)
{
    if (!open_gfx()) return FALSE;
    if (!shown_early) old_view = GfxBase->ActiView;    /* przy planszy wczytywania juz zapamietany */
    LoadView(0);
    WaitTOF();
    WaitTOF();

    OwnBlitter();
    WaitBlit();
    /* pamiec podreczna instrukcji 68020+ wlaczona: bez Fast RAM kazda instrukcja spoza niej
     * idzie przez szyne Chip RAM, ktora dzieli z obrazem AGA, Blitterem i dzwiekiem */
    old_cache = CacheControl(CACRF_EnableI, CACRF_EnableI);
    Forbid();

    old_dmacon = REG16(R_DMACONR);
    old_intena = REG16(R_INTENAR);

    REG16(R_INTENA) = INTF_ALL;
    REG16(R_INTREQ) = INTF_ALL;
    REG16(R_INTREQ) = INTF_ALL;
    REG16(R_DMACON) = DMAF_ALL;

    vbr = (volatile APTR *)get_vbr();
    old_level3 = vbr[0x6C / 4];
    vbr[0x6C / 4] = (APTR)level3_isr;

    REG16(R_INTENA) = INTF_SETCLR | INTF_INTEN | INTF_VERTB;
    taken = TRUE;
    return TRUE;
}

void system_restore(void)
{
    REG16(R_INTENA) = INTF_ALL;
    REG16(R_INTREQ) = INTF_ALL;
    REG16(R_DMACON) = DMAF_ALL;
    hw_wait_blit();

    vbr[0x6C / 4] = old_level3;

    reset_display();

    REG16(R_DMACON) = DMAF_SETCLR | (old_dmacon & DMAF_ALL);
    REG16(R_INTENA) = INTF_SETCLR | (old_intena & INTF_ALL);

    Permit();
    CacheControl(old_cache, CACRF_EnableI);
    DisownBlitter();
    LoadView(old_view);
    WaitTOF();
    WaitTOF();
    CloseLibrary((struct Library *)GfxBase);
    GfxBase = 0;
    shown_early = taken = FALSE;
}
#endif

ULONG g_lag;                    /* zgubione klatki (diagnostyka, make EXTRA=-DSHOW_LAG) */

#if !defined(WHDLOAD) && !defined(__clang__)
/* Czekanie na klatke instrukcja STOP zamiast pustej petli: procesor spi do przerwania VBL.
 * Na Amidze bez roznicy, ale emulator (WinUAE, Amiberry w THEA500 Mini) nie musi wykonywac
 * petli instrukcja po instrukcji - w menu, gdzie gra wiekszosc klatki czeka, to wiekszosc jego
 * pracy (przycinala sie muzyka). Bez wyscigu: przerwania zamaskowane przed porownaniem, STOP
 * odmaskowuje je atomowo, a VBL z miedzyczasu czeka w INTREQ i budzi od razu.
 * Wywolanie przez exec Supervisor() (gra dziala w trybie uzytkownika), powrot przez RTE. */
ULONG g_wait_last;
void wait_stop(void);
__asm__(
"    .text\n"
"    .even\n"
"_wait_stop:\n"
"    move.w  #0x2700,sr\n"
"    move.l  _g_frame,d0\n"
"    cmp.l   _g_wait_last,d0\n"
"    bne.s   1f\n"
"    stop    #0x2000\n"
"1:  rte\n");
#endif

void system_wait_vbl(void)
{
    static ULONG last;
#if !defined(WHDLOAD) && !defined(__clang__)
    g_wait_last = last;
    while (g_frame == last)
        Supervisor((ULONG (*)())wait_stop);
#else
    while (g_frame == last)
        ;
#endif
    if (last && g_frame - last > 1) g_lag += g_frame - last - 1;
    last = g_frame;
}
