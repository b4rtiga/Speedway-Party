/* system.h - przejecie i oddanie sprzetu systemowi. */
#ifndef SYSTEM_H
#define SYSTEM_H

#include <exec/types.h>

extern volatile ULONG g_frame;   /* licznik VBL (50 Hz PAL), z przerwania poziomu 3 */
extern ULONG g_lag;              /* klatki zgubione przez petle gry */

BOOL system_show_early(void);    /* plansza wczytywania: lista Coppera gry, system dziala dalej */
void system_hide_early(void);    /* blad przed przejeciem: z powrotem ekran systemu */
BOOL system_takeover(void);
void system_restore(void);
void system_wait_vbl(void);      /* czeka na nastepna ramke */

#endif
