#include "bitstream.h"
#include <stdio.h>
#include <string.h>

BitStream *bs_create(size_t size_bits) {
    BitStream *bs = malloc(sizeof(BitStream));
    bs->size = size_bits / 8 + (size_bits % 8 != 0);
    bs->size_bits = size_bits;
    bs->data = calloc(bs->size, 1);
    bs->pos = 0;
    return bs;
}

void bs_free(BitStream *bs) {
    free(bs->data);
    free(bs);
}

uint64_t bs_read_bits(BitStream *bs, int n) {
    if (n < 0 || n > 64) {
        fprintf(stderr, "FATAL: read_bits: invalid n=%d\n", n);
        exit(1);
    }
    if (bs->pos > bs->size_bits || (size_t)n > bs->size_bits - bs->pos) {
        fprintf(stderr, "FATAL: read out of bounds at pos=%zu n=%d\n", bs->pos, n);
        exit(1);
    }
    uint64_t value = 0;
    for (int i = 0; i < n; i++) {
        size_t byte_idx = bs->pos / 8;
        int bit_idx = 7 - (bs->pos % 8); // Big-Endian внутри байта
        int bit = (bs->data[byte_idx] >> bit_idx) & 1;
        value = (value << 1) | bit;
        bs->pos++;
    }
    return value;
}

void bs_write_bits(BitStream *bs, uint64_t value, int n) {
    if (n < 0 || n > 64) {
        fprintf(stderr, "FATAL: write_bits: invalid n=%d\n", n);
        exit(1);
    }
    if (bs->pos > bs->size_bits || (size_t)n > bs->size_bits - bs->pos) {
        fprintf(stderr, "FATAL: write out of bounds at pos=%zu n=%d\n", bs->pos, n);
        exit(1);
    }
    for (int i = n - 1; i >= 0; i--) {
        size_t byte_idx = bs->pos / 8;
        int bit_idx = 7 - (bs->pos % 8);
        int bit = (value >> i) & 1;
        if (bit) bs->data[byte_idx] |= (1 << bit_idx);
        else     bs->data[byte_idx] &= ~(1 << bit_idx);
        bs->pos++;
    }
}

void bs_seek(BitStream *bs, size_t bit_pos) {
    if (bit_pos > bs->size_bits) {
        fprintf(stderr, "FATAL: seek out of bounds at pos=%zu size=%zu\n",
                bit_pos, bs->size_bits);
        exit(1);
    }
    bs->pos = bit_pos;
}
