#ifndef PROTO_EXEC_H
#define PROTO_EXEC_H
#include <exec/types.h>
#include <exec/execbase.h>
extern struct ExecBase *SysBase;
APTR AllocMem(ULONG byteSize, ULONG requirements);
void FreeMem(APTR memoryBlock, ULONG byteSize);
void Forbid(void); void Permit(void);
ULONG Supervisor(ULONG (*userFunction)());
struct Library *OpenLibrary(CONST_STRPTR libName, ULONG version);
void CloseLibrary(struct Library *library);
#endif
