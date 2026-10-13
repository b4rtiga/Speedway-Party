/* Night Grid: C89-compatible asset interface. RAW bytes are signed PCM bits. */
#ifndef NIGHT_GRID_H
#define NIGHT_GRID_H
#define NIGHT_GRID_BYTES 438226UL
#define NIGHT_GRID_PAL_PERIOD 222U
#define NIGHT_GRID_NTSC_PERIOD 224U
#define NIGHT_GRID_START_VOLUME 48U
#define NIGHT_GRID_MAX_BLOCK 65534UL
extern const unsigned char night_grid_pcm[NIGHT_GRID_BYTES];
/* Copy an even block into a caller-owned buffer, wrapping at the loop end.
   capacity must be even, 2..65534 bytes; *cursor starts at 0.
   This function does not allocate, submit audio, or manage DMA. */
unsigned long night_grid_fill(unsigned char *dst, unsigned long capacity,
                              unsigned long *cursor);
#endif
