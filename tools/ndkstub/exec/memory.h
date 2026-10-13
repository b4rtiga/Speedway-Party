#ifndef EXEC_MEMORY_H
#define EXEC_MEMORY_H
#include <exec/types.h>
#define MEMF_PUBLIC (1L<<0)
#define MEMF_ANY    (0L)
#define MEMF_CHIP   (1L<<1)
#define MEMF_FAST   (1L<<2)
#define MEMF_CLEAR  (1L<<16)
#endif
