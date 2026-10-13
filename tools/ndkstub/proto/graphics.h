#ifndef PROTO_GRAPHICS_H
#define PROTO_GRAPHICS_H
#include <exec/types.h>
struct View;
void LoadView(struct View *view); void WaitTOF(void);
void OwnBlitter(void); void DisownBlitter(void); void WaitBlit(void);
#endif
