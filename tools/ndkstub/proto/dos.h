#ifndef PROTO_DOS_H
#define PROTO_DOS_H
#include <exec/types.h>
BPTR Open(CONST_STRPTR name, LONG accessMode);
LONG Read(BPTR file, APTR buffer, LONG length);
LONG Seek(BPTR file, LONG position, LONG offset);
BOOL Close(BPTR file);
#endif
