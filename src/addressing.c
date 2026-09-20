#include "addressing.h"

#include <stdio.h>
#include <stdlib.h>

typedef enum {
    RESOLVED_IMMEDIATE,
    RESOLVED_VECTOR
} ResolvedRangeKind;

typedef struct {
    ResolvedRangeKind kind;
    BitVector *vector;
    size_t offset;
    size_t size_bits;
    uint64_t immediate;
} ResolvedRange;

static size_t checked_size(uint64_t value, const char *what,
                           size_t stream_pos) {
    size_t result = (size_t)value;
    if ((uint64_t)result != value) {
        fprintf(stderr, "FATAL: %s does not fit size_t at pos=%zu\n",
                what, stream_pos);
        exit(1);
    }
    return result;
}

static Register *resolve_register(VM *vm, uint8_t reg_class,
                                  uint8_t reg_num, size_t stream_pos) {
    switch (reg_class) {
        case REG_GLOBAL: {
            Register *reg = vm_get_register(vm, reg_num);
            if (!reg) {
                fprintf(stderr,
                        "FATAL: register %u not found at pos=%zu\n",
                        reg_num, stream_pos);
                exit(1);
            }
            return reg;
        }

        case REG_PROCEDURE:
        case REG_LOCAL:
        case REG_SUPERLOCAL:
            fprintf(stderr,
                    "FATAL: register class %u not implemented at pos=%zu\n",
                    reg_class, stream_pos);
            exit(1);

        default:
            fprintf(stderr,
                    "FATAL: invalid register class %u at pos=%zu\n",
                    reg_class, stream_pos);
            exit(1);
    }
}

static ResolvedRange resolve_address(VM *vm, Address address,
                                     uint64_t size_bits, int writable,
                                     size_t stream_pos) {
    ResolvedRange resolved = {0};
    resolved.size_bits = checked_size(size_bits, "range size", stream_pos);

    switch (address.mode) {
        case ADDR_IMMEDIATE:
            if (writable) {
                fprintf(stderr,
                        "FATAL: cannot write to immediate at pos=%zu\n",
                        stream_pos);
                exit(1);
            }
            if (resolved.size_bits > 64) {
                fprintf(stderr,
                        "FATAL: immediate exceeds 64 bits at pos=%zu\n",
                        stream_pos);
                exit(1);
            }
            resolved.kind = RESOLVED_IMMEDIATE;
            resolved.immediate = address.imm;
            return resolved;

        case ADDR_DIRECT: {
            Register *reg = resolve_register(vm, address.reg_class,
                                             address.reg_num, stream_pos);
            resolved.kind = RESOLVED_VECTOR;
            resolved.vector = reg->bits;
            resolved.offset = checked_size(address.offset, "range offset",
                                           stream_pos);
            if (resolved.offset > resolved.vector->size_bits ||
                resolved.size_bits >
                    resolved.vector->size_bits - resolved.offset) {
                fprintf(stderr,
                        "FATAL: range out of bounds at pos=%zu\n",
                        stream_pos);
                exit(1);
            }
            return resolved;
        }

        case ADDR_INDIRECT:
            fprintf(stderr,
                    "FATAL: indirect addressing not implemented at pos=%zu\n",
                    stream_pos);
            exit(1);

        case ADDR_FOREIGN:
            fprintf(stderr,
                    "FATAL: foreign addressing not implemented at pos=%zu\n",
                    stream_pos);
            exit(1);

        default:
            fprintf(stderr,
                    "FATAL: invalid addressing mode %u at pos=%zu\n",
                    address.mode, stream_pos);
            exit(1);
    }
}

uint64_t read_range(VM *vm, Range range, size_t stream_pos) {
    ResolvedRange resolved = resolve_address(vm, range.addr, range.size_bits,
                                             0, stream_pos);
    if (resolved.size_bits > 64) {
        fprintf(stderr, "FATAL: read size exceeds 64 bits at pos=%zu\n",
                stream_pos);
        exit(1);
    }
    if (resolved.kind == RESOLVED_IMMEDIATE)
        return resolved.immediate;

    BitCursor cursor = bc_create(resolved.vector, resolved.offset);
    return bc_read_bits(&cursor, (int)resolved.size_bits);
}

void write_range(VM *vm, Address destination, uint64_t value,
                 uint64_t size_bits, size_t stream_pos) {
    ResolvedRange resolved = resolve_address(vm, destination, size_bits, 1,
                                             stream_pos);
    if (resolved.size_bits > 64) {
        fprintf(stderr, "FATAL: write size exceeds 64 bits at pos=%zu\n",
                stream_pos);
        exit(1);
    }
    BitCursor cursor = bc_create(resolved.vector, resolved.offset);
    bc_write_bits(&cursor, value, (int)resolved.size_bits);
}

static void copy_cursors(BitCursor *source, BitCursor *destination,
                         size_t size_bits) {
    while (size_bits > 0) {
        int chunk = size_bits > 64 ? 64 : (int)size_bits;
        uint64_t value = bc_read_bits(source, chunk);
        bc_write_bits(destination, value, chunk);
        size_bits -= (size_t)chunk;
    }
}

void copy_range(VM *vm, Range source, Address destination,
                size_t stream_pos) {
    ResolvedRange resolved_source = resolve_address(
        vm, source.addr, source.size_bits, 0, stream_pos);
    ResolvedRange resolved_destination = resolve_address(
        vm, destination, source.size_bits, 1, stream_pos);
    BitVector *temporary = bv_create(resolved_source.size_bits);
    BitCursor temporary_writer = bc_create(temporary, 0);

    if (resolved_source.kind == RESOLVED_IMMEDIATE) {
        bc_write_bits(&temporary_writer, resolved_source.immediate,
                      (int)resolved_source.size_bits);
    } else {
        BitCursor source_cursor = bc_create(resolved_source.vector,
                                            resolved_source.offset);
        copy_cursors(&source_cursor, &temporary_writer,
                     resolved_source.size_bits);
    }

    BitCursor temporary_reader = bc_create(temporary, 0);
    BitCursor destination_cursor = bc_create(resolved_destination.vector,
                                             resolved_destination.offset);
    copy_cursors(&temporary_reader, &destination_cursor,
                 resolved_source.size_bits);
    bv_free(temporary);
}

uint64_t read_value_sized(VM *vm, Address address, uint64_t size_bits,
                          size_t stream_pos) {
    Range range = {.addr = address, .size_bits = size_bits};
    return read_range(vm, range, stream_pos);
}

void write_value_sized(VM *vm, Address address, uint64_t value,
                       uint64_t size_bits, size_t stream_pos) {
    write_range(vm, address, value, size_bits, stream_pos);
}

uint64_t read_by_address(VM *vm, Address address, size_t stream_pos) {
    return read_value_sized(vm, address, 64, stream_pos);
}

void write_by_address(VM *vm, Address address, uint64_t value,
                      size_t stream_pos) {
    write_value_sized(vm, address, value, 64, stream_pos);
}
