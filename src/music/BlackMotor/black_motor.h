/* Black Motor: C89-compatible asset interface. RAW bytes are signed PCM bits. */
#ifndef BLACK_MOTOR_H
#define BLACK_MOTOR_H
#define BLACK_MOTOR_BYTES 213026UL
#define BLACK_MOTOR_PAL_PERIOD 222U
#define BLACK_MOTOR_NTSC_PERIOD 224U
#define BLACK_MOTOR_START_VOLUME 48U
#define BLACK_MOTOR_MAX_BLOCK 65534UL
extern const unsigned char black_motor_pcm[BLACK_MOTOR_BYTES];
/* Copy an even block into a caller-owned buffer, wrapping at the loop end.
   capacity must be even, 2..65534 bytes; *cursor starts at 0.
   This function does not allocate, submit audio, or manage DMA. */
unsigned long black_motor_fill(unsigned char *dst, unsigned long capacity,
                              unsigned long *cursor);
#endif
