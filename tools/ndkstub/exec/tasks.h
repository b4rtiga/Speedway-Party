#ifndef EXEC_TASKS_H
#define EXEC_TASKS_H
#include <exec/types.h>
struct Task { struct Task *tc_dummy; };
struct StackSwapStruct { APTR stk_Lower; ULONG stk_Upper; APTR stk_Pointer; };
#endif
