#ifndef GRAPHICS_GFXBASE_H
#define GRAPHICS_GFXBASE_H
#include <exec/types.h>
struct View; struct copinit;
struct GfxBase { struct Library LibNode; struct View *ActiView; struct copinit *copinit; };
#endif
