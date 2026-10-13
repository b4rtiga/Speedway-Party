#ifndef EXEC_EXECBASE_H
#define EXEC_EXECBASE_H
#include <exec/types.h>
struct ExecBase { struct Library LibNode; UWORD AttnFlags; };
#define AFF_68010 (1<<0)
#define AFF_68020 (1<<1)
#endif
