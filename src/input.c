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
static UBYTE menu_prev[4], menu_edge;    /* osobno: port 1, port 0, strzalki, port 0 jako mysz */
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

/* "JOY 0 MYSZ": port 0 podany przez emulator jako mysz (pad w emulatorze w przegladarce,
 * WinUAE z portem 0 w trybie myszy) - kierunek = w ktora strone przesuwa sie licznik, z krotkim
 * podtrzymaniem (emulator nie zawsze przesuwa go w kazdej klatce); licznik stoi = nic.
 * Wybierane w ustawieniach - samych danych z portu nie da sie pewnie odroznic od joysticka
 * (WinUAE podaje w wyzszych bitach JOYxDAT liczniki myszy Windows). */
#define MOUSE_HOLD 4                     /* klatki podtrzymania kierunku po ostatnim ruchu */
static UWORD mouse_prev;
static UBYTE mouse_seen, hold_x, hold_y, dir_x, dir_y;

static UBYTE read_mouse0(UWORD d)
{
    BYTE dx, dy;
    if (!mouse_seen) { mouse_seen = 1; mouse_prev = d; }
    dx = (BYTE)((d & 0xFF) - (mouse_prev & 0xFF));
    dy = (BYTE)((d >> 8) - (mouse_prev >> 8));
    mouse_prev = d;
    if (dx) { dir_x = (UBYTE)(dx < 0 ? BTN_LEFT : BTN_RIGHT); hold_x = MOUSE_HOLD; }
    else if (hold_x) hold_x--;
    else dir_x = 0;
    if (dy) { dir_y = (UBYTE)(dy < 0 ? BTN_UP : BTN_DOWN); hold_y = MOUSE_HOLD; }
    else if (hold_y) hold_y--;
    else dir_y = 0;
    return (UBYTE)(dir_x | dir_y);
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

    {
        UWORD d0 = REG16(R_JOY0DAT);
        port_state[INPUT_JOY1] = (UBYTE)(decode_joy(REG16(R_JOY1DAT)) | ((pra & 0x80) ? 0 : BTN_FIRE));
        port_state[INPUT_JOY0] = (UBYTE)(decode_joy(d0) | ((pra & 0x40) ? 0 : BTN_FIRE));
        port_state[INPUT_MOUSE0] = (UBYTE)(read_mouse0(d0) | ((pra & 0x40) ? 0 : BTN_FIRE));
    }
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
        /* kazde zrodlo ma wlasne wykrywanie wcisniec: stale "wcisniete" bity z portu 0 (mysz,
         * pusty port) nie blokuja tych samych kierunkow ze strzalek i portu 1 */
        static const UBYTE src[4] = { INPUT_JOY1, INPUT_JOY0, INPUT_KEYS_ARROWS, INPUT_MOUSE0 };
        UBYTE k;
        menu_edge = 0;
        BOOL mouse_menu = FALSE;                 /* port 0 jako mysz tylko, gdy ktos to wybral */
        for (k = 0; k < MAX_RIDERS; k++)
            if (g_tour.riders[k].input == INPUT_MOUSE0) mouse_menu = TRUE;
        for (k = 0; k < 4; k++) {
            UBYTE m = (src[k] == INPUT_MOUSE0 && !mouse_menu) ? 0 : port_state[src[k]];
            menu_edge |= (UBYTE)(m & ~menu_prev[k]);
            menu_prev[k] = m;
        }
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
