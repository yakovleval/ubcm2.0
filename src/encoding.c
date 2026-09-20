#include "encoding.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Range read_source(BitCursor *cursor) {
    Range r = {0};
    r.addr = read_address(cursor);
    if (r.addr.mode == ADDR_IMMEDIATE)
        r.size_bits = r.addr.imm_size;
    else
        r.size_bits = read_variable_size(cursor);
    return r;
}

// ============================================================
// Размеры и адреса
// ============================================================

uint64_t read_variable_size(BitCursor *cursor) {
    uint64_t header = bc_read_bits(cursor, 3);
    uint64_t num_bytes = header + 1;  // 1..8
    uint64_t value = 0;
    for (uint64_t i = 0; i < num_bytes; i++)
        value = (value << 8) | bc_read_bits(cursor, 8);
    return value;
}

void write_variable_size(BitCursor *cursor, uint64_t value) {
    int bytes = 1;
    uint64_t tmp = value;
    while (tmp > 0xFF && bytes < 8) { tmp >>= 8; bytes++; }
    bc_write_bits(cursor, bytes - 1, 3);
    for (int i = bytes - 1; i >= 0; i--)
        bc_write_bits(cursor, (value >> (i * 8)) & 0xFF, 8);
}

// ============================================================
// Адреса
// ============================================================

RegisterSelector read_register_selector(BitCursor *cursor) {
    RegisterSelector selector;
    selector.reg_class = bc_read_bits(cursor, 2);
    selector.reg_num = bc_read_bits(cursor, 5);
    return selector;
}

Address read_address(BitCursor *cursor) {
    Address addr = {0};
    addr.mode = bc_read_bits(cursor, 2);

    switch (addr.mode) {
        case ADDR_IMMEDIATE:
            addr.imm_size = read_variable_size(cursor);
            addr.imm = bc_read_bits(cursor, (int)addr.imm_size);
            break;

        case ADDR_DIRECT: {
            RegisterSelector selector = read_register_selector(cursor);
            addr.reg_class = selector.reg_class;
            addr.reg_num   = selector.reg_num;
            addr.offset    = read_variable_size(cursor);
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

// ============================================================
// Целые нефиксированного размера
// ============================================================

uint64_t read_int(BitCursor *cursor) {
    uint64_t size_bits = read_variable_size(cursor);
    if (size_bits > 64) {
	fprintf(stderr,
            "FATAL: integer > 64 bits not supported (%llu bits)\n",
            (unsigned long long)size_bits);
        exit(1);
    }
    if (size_bits == 0) return 0;
    return bc_read_bits(cursor, (int)size_bits);
}

void write_int(BitCursor *cursor, uint64_t value) {
    // Считаем минимальное число бит
    int bits = 1;
    uint64_t tmp = value;
    while (tmp > 1) { tmp >>= 1; bits++; }
    write_variable_size(cursor, bits);
    bc_write_bits(cursor, value, bits);
}

// ============================================================
// Вещественные (манntissa + порядок)
// ============================================================

// Храним как: [int: мантисса] [int: порядок]
// value = mantissa * 2^exponent
// Для простоты: используем double и разбираем его как IEEE 754
double read_float(BitCursor *cursor) {
    uint64_t mantissa = read_int(cursor);
    uint64_t exponent = read_int(cursor);

    // Простейшая интерпретация: value = mantissa * 2^(exponent - 1023)
    double value = (double)mantissa;
    int exp = (int)exponent - 1023;
    while (exp > 0) { value *= 2.0; exp--; }
    while (exp < 0) { value /= 2.0; exp++; }
    return value;
}

void write_float(BitCursor *cursor, double value) {
    // Разбираем double как мантиссу и порядок
    uint64_t bits;
    memcpy(&bits, &value, sizeof(double));

    uint64_t mantissa = bits & 0x000FFFFFFFFFFFFFULL;
    uint64_t exponent = (bits >> 52) & 0x7FFULL;

    // Добавляем неявную единицу (нормализация)
    mantissa |= 0x0010000000000000ULL;

    write_int(cursor, mantissa);
    write_int(cursor, exponent);
}

// ============================================================
// Значения (тип + значение)
// ============================================================

Value read_value(BitCursor *cursor) {
    Value v = {0};
    v.type = bc_read_bits(cursor, 1);
    if (v.type == VALUE_INT) {
        v.i = read_int(cursor);
    } else {
        v.f = read_float(cursor);
    }
    return v;
}

void write_value(BitCursor *cursor, Value v) {
    bc_write_bits(cursor, v.type, 1);
    if (v.type == VALUE_INT) {
        write_int(cursor, v.i);
    } else {
        write_float(cursor, v.f);
    }
}
