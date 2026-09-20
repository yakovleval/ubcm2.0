#include "bitstream.h"
#include <stdio.h>
#include <string.h>

BitVector *bv_create(size_t size_bits) {
    BitVector *vector = malloc(sizeof(BitVector));
    vector->size = size_bits / 8 + (size_bits % 8 != 0);
    vector->size_bits = size_bits;
    vector->data = calloc(vector->size, 1);
    return vector;
}

void bv_free(BitVector *vector) {
    free(vector->data);
    free(vector);
}

BitCursor bc_create(BitVector *vector, size_t bit_pos) {
    BitCursor cursor = {.vector = vector, .pos = 0};
    bc_seek(&cursor, bit_pos);
    return cursor;
}

uint64_t bc_read_bits(BitCursor *cursor, int n) {
    if (n < 0 || n > 64) {
        fprintf(stderr, "FATAL: read_bits: invalid n=%d\n", n);
        exit(1);
    }
    if (cursor->pos > cursor->vector->size_bits ||
        (size_t)n > cursor->vector->size_bits - cursor->pos) {
        fprintf(stderr, "FATAL: read out of bounds at pos=%zu n=%d\n",
                cursor->pos, n);
        exit(1);
    }
    uint64_t value = 0;
    for (int i = 0; i < n; i++) {
        size_t byte_idx = cursor->pos / 8;
        int bit_idx = 7 - (cursor->pos % 8); // Big-Endian внутри байта
        int bit = (cursor->vector->data[byte_idx] >> bit_idx) & 1;
        value = (value << 1) | bit;
        cursor->pos++;
    }
    return value;
}

void bc_write_bits(BitCursor *cursor, uint64_t value, int n) {
    if (n < 0 || n > 64) {
        fprintf(stderr, "FATAL: write_bits: invalid n=%d\n", n);
        exit(1);
    }
    if (cursor->pos > cursor->vector->size_bits ||
        (size_t)n > cursor->vector->size_bits - cursor->pos) {
        fprintf(stderr, "FATAL: write out of bounds at pos=%zu n=%d\n",
                cursor->pos, n);
        exit(1);
    }
    for (int i = n - 1; i >= 0; i--) {
        size_t byte_idx = cursor->pos / 8;
        int bit_idx = 7 - (cursor->pos % 8);
        int bit = (value >> i) & 1;
        if (bit) cursor->vector->data[byte_idx] |= (1 << bit_idx);
        else     cursor->vector->data[byte_idx] &= ~(1 << bit_idx);
        cursor->pos++;
    }
}

void bc_seek(BitCursor *cursor, size_t bit_pos) {
    if (bit_pos > cursor->vector->size_bits) {
        fprintf(stderr, "FATAL: seek out of bounds at pos=%zu size=%zu\n",
                bit_pos, cursor->vector->size_bits);
        exit(1);
    }
    cursor->pos = bit_pos;
}
