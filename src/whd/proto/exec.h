/* proto/exec.h dla wersji WHDLoad (make whdload): pod WHDLoad nie ma systemu - pamiec
 * przydziela src/whd.c z obszaru, ktory daje WHDLoad (Chip: BaseMem, Fast: ExpMem). */
#ifndef WHD_PROTO_EXEC_H
#define WHD_PROTO_EXEC_H
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/tasks.h>

APTR  AllocMem(ULONG size, ULONG flags);
void  FreeMem(APTR mem, ULONG size);
ULONG AvailMem(ULONG flags);

#endif
