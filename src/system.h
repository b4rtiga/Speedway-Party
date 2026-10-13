/* system.h - przejecie i oddanie sprzetu systemowi. */
#ifndef SYSTEM_H
#define SYSTEM_H

#include <exec/types.h>

extern volatile ULONG g_frame;   /* licznik VBL (50 Hz PAL), z przerwania poziomu 3 */

BOOL system_takeover(void);
void system_restore(void);
void system_wait_vbl(void);      /* czeka na nastepna ramke */

#endif
