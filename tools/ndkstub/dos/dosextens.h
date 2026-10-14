#ifndef DOS_DOSEXTENS_H
#define DOS_DOSEXTENS_H
#include <exec/types.h>
#include <exec/tasks.h>
/* atrapa: tylko pole uzywane przez gre (wylaczenie okien bledow DOS przy zapisie) */
struct Process { struct Task pr_Task; APTR pr_WindowPtr; };
#endif
