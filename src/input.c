/* input.c - joysticki (porty 0/1), adapter 4 graczy (port rownolegly), klawiatura (CIA-A). */
#include "game.h"
#include "hw.h"

#define CIAA_ICR (*(volatile UBYTE *)0xBFED01)
#define CIAA_SDR (*(volatile UBYTE *)0xBFEC01)
#define CIAA_CRA (*(volatile UBYTE *)0xBFEE01)

/* Adapter 4 graczy: linie danych portu rownoleglego, aktywne zerem.
 * Uklad bitow wg popularnego adaptera (Dyna Blaster) - zweryfikowac w WinUAE. */
#define PAR_J3_RIGHT 0x01
#define PAR_J3_LEFT  0x02
#define PAR_J4_RIGHT 0x10
#define PAR_J4_LEFT  0x20
#define PAR_J3_FIRE  0x04    /* CIAB PRA: BUSY */
#define PAR_J4_FIRE  0x01    /* CIAB PRA: POUT? sprawdzic */

static UBYTE keys[128];
static UBYTE keyq[8], keyq_head, keyq_tail;
static UBYTE menu_prev, menu_edge;
static UBYTE port_state[INPUT_NONE];

void input_init(void)
{
    CIAA_DDRB = 0x00;                  /* port rownolegly jako wejscie */
    CIAB_DDRA &= (UBYTE)~0x07;
}

static UBYTE decode_joy(UWORD d)
{
    UBYTE r = 0;
    if (d & 0x0200) r |= BTN_LEFT;
    if (d & 0x0002) r |= BTN_RIGHT;
    if (((d >> 9) ^ (d >> 8)) & 1) r |= BTN_UP;
    if (((d >> 1) ^ d) & 1) r |= BTN_DOWN;
    return r;
}

static void poll_keyboard(void)
{
    UBYTE c;
    UWORD line;
    if (!(CIAA_ICR & 0x08)) return;    /* SP: przyszedl bajt z klawiatury */
    c = CIAA_SDR;
    CIAA_CRA |= 0x40;                  /* handshake: SP jako wyjscie (KDAT low) */
    line = hw_vpos();
    while ((UWORD)(hw_vpos() - line) < 3) ;   /* >= 85 us */
    CIAA_CRA &= (UBYTE)~0x40;
    c = (UBYTE)~c;
    c = (UBYTE)((c >> 1) | (c << 7));
    keys[c & 0x7F] = (UBYTE)!(c & 0x80);
    if (!(c & 0x80) && (UBYTE)((keyq_head + 1) & 7) != keyq_tail) {
        keyq[keyq_head] = (UBYTE)(c & 0x7F);
        keyq_head = (UBYTE)((keyq_head + 1) & 7);
    }
}

UBYTE key_get(void)
{
    UBYTE k;
    if (keyq_head == keyq_tail) return 0xFF;
    k = keyq[keyq_tail];
    keyq_tail = (UBYTE)((keyq_tail + 1) & 7);
    return k;
}

UBYTE input_menu(void) { return menu_edge; }

BOOL key_down(UBYTE rawkey) { return keys[rawkey & 0x7F]; }

static UBYTE keyset(UBYTE l, UBYTE r, UBYTE f)
{
    return (UBYTE)((keys[l] ? BTN_LEFT : 0) | (keys[r] ? BTN_RIGHT : 0) | (keys[f] ? BTN_FIRE : 0));
}

void input_update(void)
{
    UBYTE pra = CIAA_PRA, par = (UBYTE)~CIAA_PRB, pb = (UBYTE)~CIAB_PRA;

    poll_keyboard();

    port_state[INPUT_JOY1] = (UBYTE)(decode_joy(REG16(R_JOY1DAT)) | ((pra & 0x80) ? 0 : BTN_FIRE));
    port_state[INPUT_JOY0] = (UBYTE)(decode_joy(REG16(R_JOY0DAT)) | ((pra & 0x40) ? 0 : BTN_FIRE));
    port_state[INPUT_JOY2_PAR] = (UBYTE)(((par & PAR_J3_LEFT) ? BTN_LEFT : 0) | ((par & PAR_J3_RIGHT) ? BTN_RIGHT : 0)
                                       | ((pb & PAR_J3_FIRE) ? BTN_FIRE : 0));
    port_state[INPUT_JOY3_PAR] = (UBYTE)(((par & PAR_J4_LEFT) ? BTN_LEFT : 0) | ((par & PAR_J4_RIGHT) ? BTN_RIGHT : 0)
                                       | ((pb & PAR_J4_FIRE) ? BTN_FIRE : 0));
    port_state[INPUT_KEYS_ZX]     = keyset(0x31, 0x32, 0x33);
    port_state[INPUT_KEYS_NM]     = keyset(0x36, 0x37, 0x38);
    port_state[INPUT_KEYS_ARROWS] = (UBYTE)(keyset(0x4F, 0x4E, 0x44)
                                  | (keys[0x4C] ? BTN_UP : 0) | (keys[0x4D] ? BTN_DOWN : 0));
    {
        /* bez JOY0: w porcie 0 zwykle siedzi mysz, a jej liczniki wygladaja jak ruchy joysticka */
        UBYTE m = (UBYTE)(port_state[INPUT_JOY1] | port_state[INPUT_KEYS_ARROWS]);
        menu_edge = (UBYTE)(m & ~menu_prev);
        menu_prev = m;
    }
}

UBYTE input_read(InputPort port)
{
    return port < INPUT_NONE ? port_state[port] : 0;
}
