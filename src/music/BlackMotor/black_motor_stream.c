#include "black_motor.h"
#include <string.h>

unsigned long black_motor_fill(unsigned char *dst, unsigned long capacity,
                              unsigned long *cursor)
{
    unsigned long first;
    if (!dst || !cursor || capacity < 2UL ||
        capacity > BLACK_MOTOR_MAX_BLOCK || (capacity & 1UL) ||
        *cursor >= BLACK_MOTOR_BYTES || (*cursor & 1UL))
        return 0UL;
    first = BLACK_MOTOR_BYTES - *cursor;
    if (first > capacity) first = capacity;
    memcpy(dst, black_motor_pcm + *cursor, (size_t)first);
    if (first < capacity)
        memcpy(dst + first, black_motor_pcm, (size_t)(capacity - first));
    *cursor = (*cursor + capacity) % BLACK_MOTOR_BYTES;
    return capacity;
}
