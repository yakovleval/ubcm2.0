#ifndef BITSTREAM_H
#define BITSTREAM_H

#include <stdint.h>
#include <stdlib.h>

typedef struct {
    uint8_t *data;   // сырые байты
    size_t   size;   // выделенный размер в байтах
    size_t   size_bits; // логический размер в битах
    size_t   pos;    // позиция в битах
} BitStream;

BitStream *bs_create(size_t size_bits);
void       bs_free(BitStream *bs);
uint64_t   bs_read_bits(BitStream *bs, int n);
void       bs_write_bits(BitStream *bs, uint64_t value, int n);
void       bs_seek(BitStream *bs, size_t bit_pos);

#endif
