#include "night_grid.h"
#include <string.h>

unsigned long night_grid_fill(unsigned char *dst, unsigned long capacity,
                              unsigned long *cursor)
{
    unsigned long first;
    if (!dst || !cursor || capacity < 2UL ||
        capacity > NIGHT_GRID_MAX_BLOCK || (capacity & 1UL) ||
        *cursor >= NIGHT_GRID_BYTES || (*cursor & 1UL))
        return 0UL;
    first = NIGHT_GRID_BYTES - *cursor;
    if (first > capacity) first = capacity;
    memcpy(dst, night_grid_pcm + *cursor, (size_t)first);
    if (first < capacity)
        memcpy(dst + first, night_grid_pcm, (size_t)(capacity - first));
    *cursor = (*cursor + capacity) % NIGHT_GRID_BYTES;
    return capacity;
}
