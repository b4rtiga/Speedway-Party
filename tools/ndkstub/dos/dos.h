#ifndef DOS_DOS_H
#define DOS_DOS_H
#include <exec/types.h>
#define MODE_OLDFILE 1005
#define MODE_NEWFILE 1006
#define OFFSET_BEGINNING -1
#define OFFSET_CURRENT 0
#define OFFSET_END 1
struct DateStamp { LONG ds_Days, ds_Minute, ds_Tick; };
#endif
