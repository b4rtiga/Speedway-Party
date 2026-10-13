/* hw.h - bezposredni dostep do Custom Chips (AGA) i CIA.
 * Offsety rejestrow sa wspolne dla zapisu z CPU i instrukcji MOVE Coppera. */
#ifndef HW_H
#define HW_H

#include <exec/types.h>

#ifdef HOST_SIM
/* narzedzia na PC (tools/shots): rejestry w zwyklej tablicy, nic nie trafia do sprzetu */
extern volatile UWORD g_fake_custom[0x100];
#define REG16(off)  (g_fake_custom[(off) >> 1])
#define REG32(off)  (*(volatile ULONG *)&g_fake_custom[(off) >> 1])
#else
#define CUSTOM_BASE 0xDFF000UL
#define REG16(off)  (*(volatile UWORD *)(CUSTOM_BASE + (off)))
#define REG32(off)  (*(volatile ULONG *)(CUSTOM_BASE + (off)))
#endif

/* --- offsety rejestrow --- */
#define R_DMACONR  0x002
#define R_VPOSR    0x004
#define R_VHPOSR   0x006
#define R_JOY0DAT  0x00A
#define R_JOY1DAT  0x00C
#define R_POTGOR   0x016
#define R_INTENAR  0x01C
#define R_INTREQR  0x01E
#define R_COP1LC   0x080
#define R_COPJMP1  0x088
#define R_DIWSTRT  0x08E
#define R_DIWSTOP  0x090
#define R_DDFSTRT  0x092
#define R_DDFSTOP  0x094
#define R_DMACON   0x096
#define R_INTENA   0x09A
#define R_INTREQ   0x09C
#define R_BPL1PT   0x0E0      /* BPLxPTH = 0x0E0 + 4*x, BPLxPTL = +2 */
#define R_BPLCON0  0x100
#define R_BPLCON1  0x102
#define R_BPLCON2  0x104
#define R_BPLCON3  0x106
#define R_BPL1MOD  0x108
#define R_BPL2MOD  0x10A
#define R_BPLCON4  0x10C
#define R_SPR0PT   0x120      /* SPRxPTH = 0x120 + 4*x */
#define R_COLOR00  0x180
#define R_DIWHIGH  0x1E4
#define R_FMODE    0x1FC

/* --- DMACON --- */
#define DMAF_SETCLR  0x8000
#define DMAF_BLTPRI  0x0400
#define DMAF_DMAEN   0x0200
#define DMAF_BPLEN   0x0100
#define DMAF_COPEN   0x0080
#define DMAF_BLTEN   0x0040
#define DMAF_SPREN   0x0020
#define DMAF_ALL     0x01FF

/* --- INTENA/INTREQ --- */
#define INTF_SETCLR  0x8000
#define INTF_INTEN   0x4000
#define INTF_VERTB   0x0020
#define INTF_COPER   0x0010
#define INTF_PORTS   0x0008
#define INTF_ALL     0x7FFF

/* --- BPLCON0 --- */
#define BPLCON0_COLOR  0x0200
#define BPLCON0_BPU3   0x0010   /* AGA: BPU=000 + BPU3=1 -> 8 bitplanow */
#define BPLCON0_ECSENA 0x0001   /* wlacza bity BPLCON3 */
#define BPLCON0_PLANES(n) ((((n) & 7) << 12) | (((n) & 8) ? BPLCON0_BPU3 : 0))

/* --- BPLCON3 (AGA) --- */
#define BPLCON3_BRDRBLNK 0x0020            /* ramka poza oknem obrazu wygaszona (czarna) */
/* bank palety 0..7 (po 32 kolory); kazdy zapis BPLCON3 w grze wygasza tez ramke -
 * emulatory (autoskalowanie) wykrywaja wtedy dokladnie obszar 320x256 */
#define BPLCON3_BANK(b) ((((b) & 7) << 13) | BPLCON3_BRDRBLNK)
#define BPLCON3_LOCT    0x0200             /* zapis dolnych 4 bitow skladowych */
#define BPLCON3_SPRES_LORES 0x0000

/* --- FMODE (AGA) --- */
#define FMODE_BPL_4X   0x0003   /* 64-bit fetch bitplanow */
#define FMODE_SPR_4X   0x000C   /* SPR32|SPAGEM: duszki 64 px */
#define FMODE_SPR_2X   0x0004   /* SPR32: duszki 32 px */

/* --- CIA --- */
#define CIAA_PRA  (*(volatile UBYTE *)0xBFE001)   /* /FIR1 bit7, /FIR0 bit6 */
#define CIAA_PRB  (*(volatile UBYTE *)0xBFE101)   /* port rownolegly (adapter 4 graczy) */
#define CIAA_DDRB (*(volatile UBYTE *)0xBFE301)
#define CIAB_PRA  (*(volatile UBYTE *)0xBFD000)   /* BUSY bit0, POUT bit1 */
#define CIAB_DDRA (*(volatile UBYTE *)0xBFD200)

static inline void hw_wait_blit(void)
{
    /* dummy read (bug Agnusa A1000) + test BBUSY w DMACONR */
    (void)REG16(R_DMACONR);
    while (REG16(R_DMACONR) & 0x4000) ;
}

static inline UWORD hw_vpos(void)
{
    return (UWORD)((REG32(R_VPOSR) >> 8) & 0x1FF);
}

/* czeka n zmian linii wiazki (64 us kazda) - poprawnie takze przez koniec ramki (311 -> 0) */
static inline void hw_wait_lines(UWORD n)
{
    UWORD last = hw_vpos();
    while (n) {
        UWORD v = hw_vpos();
        if (v != last) { last = v; n--; }
    }
}

#endif
