/* proto/dos.h dla wersji WHDLoad: odczyt plikow przez WHDLoad (src/whd.c), nazwy wzgledem
 * katalogu danych instalacji (PROGDIR:data/x -> x). Zapis rekordow - whd_save_file (whd.h). */
#ifndef WHD_PROTO_DOS_H
#define WHD_PROTO_DOS_H
#include <exec/types.h>
#include <dos/dos.h>

BPTR Open(CONST_STRPTR name, LONG mode);
LONG Read(BPTR file, APTR buffer, LONG length);
LONG Seek(BPTR file, LONG position, LONG mode);
LONG Close(BPTR file);

#endif
