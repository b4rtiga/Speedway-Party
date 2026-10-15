/* whd.h - wersja WHDLoad (make whdload): start z slave'a, pamiec, pliki, wyjscie. */
#ifndef WHD_H
#define WHD_H
#include <exec/types.h>

#define WHD_QUITKEY 0x59                 /* F10 - wyjscie do systemu (zapis rekordow) */

/* whdstart.s: wywolania WHDLoad (resload) */
ULONG whd_getsize(const char *name);                                   /* 0 = brak pliku */
BOOL  whd_loadoffset(ULONG size, ULONG offset, const char *name, APTR dst);
BOOL  whd_savefile(ULONG size, const char *name, const void *src);
void  whd_exit(void) __attribute__((noreturn));                        /* powrot do systemu */

/* whd.c */
BOOL  whd_save_file(const char *name, const void *src, ULONG size);    /* nazwa jak w grze (PROGDIR:...) */

#endif
