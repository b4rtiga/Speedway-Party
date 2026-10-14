#ifndef PROTO_DOS_H
#define PROTO_DOS_H
#include <exec/types.h>
#include <dos/dos.h>
BPTR Open(CONST_STRPTR name, LONG accessMode);
LONG Read(BPTR file, APTR buffer, LONG length);
LONG Write(BPTR file, CONST APTR buffer, LONG length);
LONG Seek(BPTR file, LONG position, LONG offset);
BOOL Close(BPTR file);
BOOL Rename(CONST_STRPTR oldName, CONST_STRPTR newName);
BOOL DeleteFile(CONST_STRPTR name);
struct DateStamp *DateStamp(struct DateStamp *ds);
#endif
