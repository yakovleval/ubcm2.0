#ifndef BITSTREAM_H
#define BITSTREAM_H

#include <stdint.h>
#include <stdlib.h>

typedef struct {
    uint8_t *data;   // сырые байты
    size_t   size;   // выделенный размер в байтах
    size_t   size_bits; // логический размер в битах
} BitVector;

typedef struct {
    BitVector *vector;
    size_t pos;
} BitCursor;

BitVector *bv_create(size_t size_bits);
void       bv_free(BitVector *vector);

BitCursor  bc_create(BitVector *vector, size_t bit_pos);
uint64_t   bc_read_bits(BitCursor *cursor, int n);
void       bc_write_bits(BitCursor *cursor, uint64_t value, int n);
void       bc_seek(BitCursor *cursor, size_t bit_pos);

#endif
