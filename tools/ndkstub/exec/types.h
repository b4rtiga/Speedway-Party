#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H
typedef unsigned char UBYTE; typedef signed char BYTE;
typedef unsigned short UWORD; typedef signed short WORD;
#if defined(__LP64__)            /* 64-bitowy Linux: long ma 64 bity, a na Amidze ULONG ma 32 */
typedef unsigned int ULONG; typedef signed int LONG;
#else
typedef unsigned long ULONG; typedef signed long LONG;
#endif
typedef short BOOL; typedef void *APTR; typedef unsigned char *STRPTR;
typedef const unsigned char *CONST_STRPTR; typedef LONG BPTR;
#define TRUE 1
#define FALSE 0
#define CONST const
struct Library { UWORD lib_Version; };
#endif
