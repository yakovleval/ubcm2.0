#include "encoding.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Range read_source(BitStream *bs) {
    Range r = {0};
    r.addr = read_address(bs);
    if (r.addr.mode != ADDR_IMMEDIATE)
        r.size_bits = read_variable_size(bs);
    return r;
}

uint64_t read_range(VM *vm, Range r) {
    if (r.addr.mode == ADDR_IMMEDIATE)
        return r.addr.imm;

    Register *reg = vm_get_register(vm, r.addr.reg_num);
    if (!reg) { fprintf(stderr, "FATAL: reg %d not found\n", r.addr.reg_num); exit(1); }
    if (r.addr.offset > reg->data->size_bits ||
        r.size_bits > reg->data->size_bits - r.addr.offset) {
        fprintf(stderr, "FATAL: read out of bounds\n"); exit(1);
    }
    bs_seek(reg->data, r.addr.offset);
    return bs_read_bits(reg->data, (int)r.size_bits);
}

void write_range(VM *vm, Address dst, uint64_t value, uint64_t size) {
    if (dst.mode == ADDR_IMMEDIATE) {
        fprintf(stderr, "FATAL: cannot write to immediate\n"); exit(1);
    }
    Register *reg = vm_get_register(vm, dst.reg_num);
    if (!reg) { fprintf(stderr, "FATAL: reg %d not found\n", dst.reg_num); exit(1); }
    if (dst.offset > reg->data->size_bits ||
        size > reg->data->size_bits - dst.offset) {
        fprintf(stderr, "FATAL: write out of bounds\n"); exit(1);
    }
    bs_seek(reg->data, dst.offset);
    bs_write_bits(reg->data, value, (int)size);
}

uint64_t read_value_sized(VM *vm, Address addr, uint64_t size_bits) {
    if (size_bits == 0) return 0;
    if (size_bits > 64) {
        fprintf(stderr, "FATAL: read size > 64 (%llu)\n",
                (unsigned long long)size_bits);
        exit(1);
    }
    if (addr.mode == ADDR_IMMEDIATE) return addr.imm;

    Register *reg = vm_get_register(vm, addr.reg_num);
    if (!reg) {
        fprintf(stderr, "FATAL: register %d not found\n", addr.reg_num);
        exit(1);
    }
    if (addr.offset > reg->data->size_bits ||
        size_bits > reg->data->size_bits - addr.offset) {
        fprintf(stderr,
            "FATAL: read out of bounds: reg%d offset=%llu size=%llu\n",
            addr.reg_num, (unsigned long long)addr.offset,
            (unsigned long long)size_bits);
        exit(1);
    }
    bs_seek(reg->data, addr.offset);
    return bs_read_bits(reg->data, (int)size_bits);
}

void write_value_sized(VM *vm, Address addr, uint64_t value, uint64_t size_bits) {
    if (size_bits == 0) return;
    if (size_bits > 64) {
        fprintf(stderr, "FATAL: write size > 64 (%llu)\n",
                (unsigned long long)size_bits);
        exit(1);
    }
    if (addr.mode == ADDR_IMMEDIATE) {
        fprintf(stderr, "FATAL: cannot write to immediate\n");
        exit(1);
    }
    Register *reg = vm_get_register(vm, addr.reg_num);
    if (!reg) {
        fprintf(stderr, "FATAL: register %d not found\n", addr.reg_num);
        exit(1);
    }
    if (addr.offset > reg->data->size_bits ||
        size_bits > reg->data->size_bits - addr.offset) {
        fprintf(stderr,
            "FATAL: write out of bounds: reg%d offset=%llu size=%llu\n",
            addr.reg_num, (unsigned long long)addr.offset,
            (unsigned long long)size_bits);
        exit(1);
    }
    bs_seek(reg->data, addr.offset);
    bs_write_bits(reg->data, value, (int)size_bits);
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

uint64_t read_by_address(VM *vm, Address addr) {
    if (addr.mode == ADDR_IMMEDIATE) return addr.imm;

    if (addr.reg_class != REG_GLOBAL) {
        fprintf(stderr, "FATAL: only global registers supported (class=%d)\n",
                addr.reg_class);
        exit(1);
    }

    Register *reg = vm_get_register(vm, addr.reg_num);
    if (!reg) {
	fprintf(stderr,
            "FATAL: register %d not found (offset %llu)\n",
            addr.reg_num, (unsigned long long)addr.offset);
        exit(1);
    }
    if (addr.offset > reg->data->size_bits ||
        64 > reg->data->size_bits - addr.offset) {
        fprintf(stderr,
            "FATAL: read out of bounds: reg%d offset=%llu size=%zu bits\n",
            addr.reg_num, (unsigned long long)addr.offset,
            reg->data->size_bits);
        exit(1);
    }
    bs_seek(reg->data, addr.offset);
    return bs_read_bits(reg->data, 64);
}

void write_by_address(VM *vm, Address addr, uint64_t value) {
    if (addr.mode == ADDR_IMMEDIATE) {
        fprintf(stderr, "FATAL: cannot write to immediate\n");
        exit(1);
    }
    if (addr.reg_class != REG_GLOBAL) {
        fprintf(stderr, "FATAL: only global registers supported (class=%d)\n",
                addr.reg_class);
        exit(1);
    }
    Register *reg = vm_get_register(vm, addr.reg_num);
    if (!reg) {
	fprintf(stderr,
            "FATAL: register %d not found (offset %llu)\n",
            addr.reg_num, (unsigned long long)addr.offset);
        exit(1);
    }
    if (addr.offset > reg->data->size_bits ||
        64 > reg->data->size_bits - addr.offset) {
        fprintf(stderr,
            "FATAL: write out of bounds: reg%d offset=%llu size=%zu bits\n",
            addr.reg_num, (unsigned long long)addr.offset,
            reg->data->size_bits);
        exit(1);
    }
    bs_seek(reg->data, addr.offset);
    bs_write_bits(reg->data, value, 64);
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
