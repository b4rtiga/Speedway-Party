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
    REG16(R_POTGO) = 0xFF00;           /* linie POT jako wyjscia w stanie wysokim: drugi przycisk joysticka */
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

/* Port 0 (i 1) w trybie myszy: emulatory (WinUAE z trybem "mysz", emulatory w przegladarce)
 * podaja pad jako liczniki ruchu. Joystick zmienia tylko bity 0, 1, 8 i 9 rejestru JOYxDAT;
 * zmiana innych bitow = mysz. Wtedy kierunek = w ktora strone licznik sie przesuwa (z krotkim
 * podtrzymaniem, bo emulator nie zawsze przesuwa go w kazdej klatce); licznik stoi = nic. */
#define MOUSE_HOLD  4                    /* klatki podtrzymania kierunku po ostatnim ruchu */
#define MOUSE_VOTES 3                    /* tyle zmian wyzszych bitow, zanim uznamy port za mysz */
static UWORD joy_prev[2];
static UBYTE joy_seen[2], joy_votes[2], joy_mouse[2], hold_x[2], hold_y[2], dir_x[2], dir_y[2];

static UBYTE read_port(UBYTE p, UWORD d)
{
    BYTE dx, dy;
    /* pierwszy odczyt tylko jako punkt odniesienia: wyzsze bity licznikow maja po starcie
     * przypadkowe wartosci (inaczej joystick od razu wygladal na mysz i strzalki "gasly") */
    if (!joy_seen[p]) { joy_seen[p] = 1; joy_prev[p] = d; }
    if (!joy_mouse[p] && ((d ^ joy_prev[p]) & ~0x0303) && ++joy_votes[p] >= MOUSE_VOTES) joy_mouse[p] = 1;
    if (!joy_mouse[p]) { joy_prev[p] = d; return decode_joy(d); }
    dx = (BYTE)((d & 0xFF) - (joy_prev[p] & 0xFF));
    dy = (BYTE)((d >> 8) - (joy_prev[p] >> 8));
    joy_prev[p] = d;
    if (dx) { dir_x[p] = (UBYTE)(dx < 0 ? BTN_LEFT : BTN_RIGHT); hold_x[p] = MOUSE_HOLD; }
    else if (hold_x[p]) hold_x[p]--;
    else dir_x[p] = 0;
    if (dy) { dir_y[p] = (UBYTE)(dy < 0 ? BTN_UP : BTN_DOWN); hold_y[p] = MOUSE_HOLD; }
    else if (hold_y[p]) hold_y[p]--;
    else dir_y[p] = 0;
    return (UBYTE)(dir_x[p] | dir_y[p]);
}

static UBYTE fire2;                      /* drugi przycisk w porcie 0 lub 1 (dziala jak ESC) */

static void poll_keyboard(void)
{
    UBYTE c;
    if (!(CIAA_ICR & 0x08)) return;    /* SP: przyszedl bajt z klawiatury */
    c = CIAA_SDR;
    CIAA_CRA |= 0x40;                  /* handshake: SP jako wyjscie (KDAT low) */
    hw_wait_lines(3);                  /* >= 85 us (3 x 64 us), takze na przelomie ramki */
    CIAA_CRA &= (UBYTE)~0x40;
    c = (UBYTE)~c;
    c = (UBYTE)((c >> 1) | (c << 7));
#ifdef SHOW_LAG
    { extern ULONG g_kb_bytes; extern UBYTE g_kb_last; g_kb_bytes++; g_kb_last = c; }
#endif
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

/* ESC z klawiatury albo drugi przycisk joysticka */
BOOL input_esc(void) { return (BOOL)(keys[KEY_ESC] || fire2); }

static UBYTE keyset(UBYTE l, UBYTE r, UBYTE f)
{
    return (UBYTE)((keys[l] ? BTN_LEFT : 0) | (keys[r] ? BTN_RIGHT : 0) | (keys[f] ? BTN_FIRE : 0));
}

void input_update(void)
{
    UBYTE pra = CIAA_PRA, par = (UBYTE)~CIAA_PRB, pb = (UBYTE)~CIAB_PRA;

    poll_keyboard();

    port_state[INPUT_JOY1] = (UBYTE)(read_port(1, REG16(R_JOY1DAT)) | ((pra & 0x80) ? 0 : BTN_FIRE));
    port_state[INPUT_JOY0] = (UBYTE)(read_port(0, REG16(R_JOY0DAT)) | ((pra & 0x40) ? 0 : BTN_FIRE));
    {
        /* drugi przycisk: linie POT (aktywne zerem) - port 0 DATLY (bit 10), port 1 DATRY (bit 14) */
        UWORD pot = REG16(R_POTGOR);
        fire2 = (UBYTE)(!(pot & 0x0400) || !(pot & 0x4000));
    }
    port_state[INPUT_JOY2_PAR] = (UBYTE)(((par & PAR_J3_LEFT) ? BTN_LEFT : 0) | ((par & PAR_J3_RIGHT) ? BTN_RIGHT : 0)
                                       | ((pb & PAR_J3_FIRE) ? BTN_FIRE : 0));
    port_state[INPUT_JOY3_PAR] = (UBYTE)(((par & PAR_J4_LEFT) ? BTN_LEFT : 0) | ((par & PAR_J4_RIGHT) ? BTN_RIGHT : 0)
                                       | ((pb & PAR_J4_FIRE) ? BTN_FIRE : 0));
    port_state[INPUT_KEYS_ZX]     = keyset(0x31, 0x32, 0x33);
    port_state[INPUT_KEYS_NM]     = keyset(0x36, 0x37, 0x38);
    port_state[INPUT_KEYS_ARROWS] = (UBYTE)(keyset(0x4F, 0x4E, 0x44)
                                  | (keys[0x4C] ? BTN_UP : 0) | (keys[0x4D] ? BTN_DOWN : 0));
    {
        /* menu: joysticki w obu portach i strzalki (mysz w porcie 0 rusza menu tylko, gdy sie nia
         * jedzie - stojaca nic nie wciska) */
        UBYTE m = (UBYTE)(port_state[INPUT_JOY1] | port_state[INPUT_JOY0] | port_state[INPUT_KEYS_ARROWS]);
        menu_edge = (UBYTE)(m & ~menu_prev);
        menu_prev = m;
    }
}

/* JOY 1 i STRZALKI to w praktyce to samo: WinUAE zamienia strzalki na joystick w porcie 1
 * (wtedy klawisze do gry nie docieraja), na Amidze strzalki to klawiatura. Kazde z tych
 * ustawien czyta oba zrodla, chyba ze drugie wybral inny gracz. */
static BOOL port_taken(InputPort p)
{
    UBYTE i;
    for (i = 0; i < g_tour.num_humans && i < MAX_RIDERS; i++)
        if (g_tour.riders[i].input == p) return TRUE;
    return FALSE;
}

#define DIR_FIRE (BTN_LEFT | BTN_RIGHT | BTN_FIRE)

UBYTE input_read(InputPort port)
{
    if (port == INPUT_JOY1 && !port_taken(INPUT_KEYS_ARROWS))
        return (UBYTE)(port_state[INPUT_JOY1] | (port_state[INPUT_KEYS_ARROWS] & DIR_FIRE));
    if (port == INPUT_KEYS_ARROWS && !port_taken(INPUT_JOY1))
        return (UBYTE)(port_state[INPUT_KEYS_ARROWS] | (port_state[INPUT_JOY1] & DIR_FIRE));
    return port < INPUT_NONE ? port_state[port] : 0;
}
