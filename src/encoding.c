#include "encoding.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    RESOLVED_IMMEDIATE,
    RESOLVED_STREAM
} ResolvedRangeKind;

typedef struct {
    ResolvedRangeKind kind;
    BitStream *stream;
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

static ResolvedRange resolve_address(VM *vm, Address addr,
                                     uint64_t size_bits, int writable,
                                     size_t stream_pos) {
    ResolvedRange resolved = {0};
    resolved.size_bits = checked_size(size_bits, "range size", stream_pos);

    switch (addr.mode) {
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
            resolved.immediate = addr.imm;
            return resolved;

        case ADDR_DIRECT: {
            Register *reg = resolve_register(vm, addr.reg_class,
                                             addr.reg_num, stream_pos);
            resolved.kind = RESOLVED_STREAM;
            resolved.stream = reg->data;
            resolved.offset = checked_size(addr.offset, "range offset",
                                           stream_pos);
            if (resolved.offset > resolved.stream->size_bits ||
                resolved.size_bits >
                    resolved.stream->size_bits - resolved.offset) {
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
                    addr.mode, stream_pos);
            exit(1);
    }
}

Range read_source(BitStream *bs) {
    Range r = {0};
    r.addr = read_address(bs);
    if (r.addr.mode == ADDR_IMMEDIATE)
        r.size_bits = r.addr.imm_size;
    else
        r.size_bits = read_variable_size(bs);
    return r;
}

uint64_t read_range(VM *vm, Range r, size_t stream_pos) {
    ResolvedRange resolved = resolve_address(vm, r.addr, r.size_bits, 0,
                                             stream_pos);
    if (resolved.size_bits > 64) {
        fprintf(stderr, "FATAL: read size exceeds 64 bits at pos=%zu\n",
                stream_pos);
        exit(1);
    }
    if (resolved.kind == RESOLVED_IMMEDIATE)
        return resolved.immediate;

    bs_seek(resolved.stream, resolved.offset);
    return bs_read_bits(resolved.stream, (int)resolved.size_bits);
}

void write_range(VM *vm, Address dst, uint64_t value, uint64_t size_bits,
                 size_t stream_pos) {
    ResolvedRange resolved = resolve_address(vm, dst, size_bits, 1,
                                             stream_pos);
    if (resolved.size_bits > 64) {
        fprintf(stderr, "FATAL: write size exceeds 64 bits at pos=%zu\n",
                stream_pos);
        exit(1);
    }
    bs_seek(resolved.stream, resolved.offset);
    bs_write_bits(resolved.stream, value, (int)resolved.size_bits);
}

static void copy_bitstreams(BitStream *src, BitStream *dst, size_t size_bits) {
    while (size_bits > 0) {
        int chunk = size_bits > 64 ? 64 : (int)size_bits;
        uint64_t value = bs_read_bits(src, chunk);
        bs_write_bits(dst, value, chunk);
        size_bits -= (size_t)chunk;
    }
}

void copy_range(VM *vm, Range src, Address dst, size_t stream_pos) {
    ResolvedRange source = resolve_address(vm, src.addr, src.size_bits, 0,
                                           stream_pos);
    ResolvedRange destination = resolve_address(vm, dst, src.size_bits, 1,
                                                stream_pos);
    BitStream *temporary = bs_create(source.size_bits);

    if (source.kind == RESOLVED_IMMEDIATE) {
        bs_write_bits(temporary, source.immediate, (int)source.size_bits);
    } else {
        bs_seek(source.stream, source.offset);
        copy_bitstreams(source.stream, temporary, source.size_bits);
    }

    bs_seek(temporary, 0);
    bs_seek(destination.stream, destination.offset);
    copy_bitstreams(temporary, destination.stream, source.size_bits);
    bs_free(temporary);
}

uint64_t read_value_sized(VM *vm, Address addr, uint64_t size_bits,
                          size_t stream_pos) {
    Range range = {.addr = addr, .size_bits = size_bits};
    return read_range(vm, range, stream_pos);
}

void write_value_sized(VM *vm, Address addr, uint64_t value,
                       uint64_t size_bits, size_t stream_pos) {
    write_range(vm, addr, value, size_bits, stream_pos);
}

// ============================================================
// Размеры и адреса
// ============================================================

uint64_t read_variable_size(BitStream *bs) {
    uint64_t header = bs_read_bits(bs, 3);
    uint64_t num_bytes = header + 1;  // 1..8
    uint64_t value = 0;
    for (uint64_t i = 0; i < num_bytes; i++)
        value = (value << 8) | bs_read_bits(bs, 8);
    return value;
}

void write_variable_size(BitStream *bs, uint64_t value) {
    int bytes = 1;
    uint64_t tmp = value;
    while (tmp > 0xFF && bytes < 8) { tmp >>= 8; bytes++; }
    bs_write_bits(bs, bytes - 1, 3);
    for (int i = bytes - 1; i >= 0; i--)
        bs_write_bits(bs, (value >> (i * 8)) & 0xFF, 8);
}

// ============================================================
// Адреса
// ============================================================

RegisterSelector read_register_selector(BitStream *bs) {
    RegisterSelector selector;
    selector.reg_class = bs_read_bits(bs, 2);
    selector.reg_num = bs_read_bits(bs, 5);
    return selector;
}

Address read_address(BitStream *bs) {
    Address addr = {0};
    addr.mode = bs_read_bits(bs, 2);

    switch (addr.mode) {
        case ADDR_IMMEDIATE:
            addr.imm_size = read_variable_size(bs);
	    addr.imm      = bs_read_bits(bs, (int)addr.imm_size);
            break;

        case ADDR_DIRECT: {
            RegisterSelector selector = read_register_selector(bs);
            addr.reg_class = selector.reg_class;
            addr.reg_num   = selector.reg_num;
            addr.offset    = read_variable_size(bs);
            break;
        }

        case ADDR_INDIRECT:
            // Пока не реализовано
            fprintf(stderr, "FATAL: indirect addressing not implemented\n");
            exit(1);

        case ADDR_FOREIGN:
            // Пока не реализовано
            fprintf(stderr, "FATAL: foreign addressing not implemented\n");
            exit(1);
    }
    return addr;
}

uint64_t read_by_address(VM *vm, Address addr, size_t stream_pos) {
    return read_value_sized(vm, addr, 64, stream_pos);
}

void write_by_address(VM *vm, Address addr, uint64_t value,
                      size_t stream_pos) {
    write_value_sized(vm, addr, value, 64, stream_pos);
}

// ============================================================
// Целые нефиксированного размера
// ============================================================

uint64_t read_int(BitStream *bs) {
    uint64_t size_bits = read_variable_size(bs);
    if (size_bits > 64) {
	fprintf(stderr,
            "FATAL: integer > 64 bits not supported (%llu bits)\n",
            (unsigned long long)size_bits);
        exit(1);
    }
    if (size_bits == 0) return 0;
    return bs_read_bits(bs, (int)size_bits);
}

void write_int(BitStream *bs, uint64_t value) {
    // Считаем минимальное число бит
    int bits = 1;
    uint64_t tmp = value;
    while (tmp > 1) { tmp >>= 1; bits++; }
    write_variable_size(bs, bits);
    bs_write_bits(bs, value, bits);
}

// ============================================================
// Вещественные (манntissa + порядок)
// ============================================================

// Храним как: [int: мантисса] [int: порядок]
// value = mantissa * 2^exponent
// Для простоты: используем double и разбираем его как IEEE 754
double read_float(BitStream *bs) {
    uint64_t mantissa = read_int(bs);
    uint64_t exponent = read_int(bs);

    // Простейшая интерпретация: value = mantissa * 2^(exponent - 1023)
    double value = (double)mantissa;
    int exp = (int)exponent - 1023;
    while (exp > 0) { value *= 2.0; exp--; }
    while (exp < 0) { value /= 2.0; exp++; }
    return value;
}

void write_float(BitStream *bs, double value) {
    // Разбираем double как мантиссу и порядок
    uint64_t bits;
    memcpy(&bits, &value, sizeof(double));

    uint64_t mantissa = bits & 0x000FFFFFFFFFFFFFULL;
    uint64_t exponent = (bits >> 52) & 0x7FFULL;

    // Добавляем неявную единицу (нормализация)
    mantissa |= 0x0010000000000000ULL;

    write_int(bs, mantissa);
    write_int(bs, exponent);
}

// ============================================================
// Значения (тип + значение)
// ============================================================

Value read_value(BitStream *bs) {
    Value v = {0};
    v.type = bs_read_bits(bs, 1);
    if (v.type == VALUE_INT) {
        v.i = read_int(bs);
    } else {
        v.f = read_float(bs);
    }
    return v;
}

void write_value(BitStream *bs, Value v) {
    bs_write_bits(bs, v.type, 1);
    if (v.type == VALUE_INT) {
        write_int(bs, v.i);
    } else {
        write_float(bs, v.f);
    }
}
