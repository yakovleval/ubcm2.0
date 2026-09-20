#ifndef ENCODING_H
#define ENCODING_H

#include "bitstream.h"
#include "vm.h"
#include <stdint.h>

// ===== Режимы адресации =====
#define ADDR_IMMEDIATE 0
#define ADDR_INDIRECT  1
#define ADDR_DIRECT    2
#define ADDR_FOREIGN   3

// ===== Классы регистров =====
#define REG_PROCEDURE  0
#define REG_LOCAL      1
#define REG_SUPERLOCAL 2
#define REG_GLOBAL     3

// ===== Типы значений =====
#define VALUE_INT      0
#define VALUE_FLOAT    1

typedef struct {
    uint8_t reg_class;
    uint8_t reg_num;
} RegisterSelector;

typedef struct {
    uint8_t  mode;
    uint8_t  reg_class;
    uint8_t  reg_num;
    uint64_t offset;
    uint64_t imm;
    uint64_t imm_size;   // ← размер immediate в битах
} Address;

typedef struct {
    Address  addr;
    uint64_t size_bits;  // размер диапазона или immediate-значения
} Range;

Range    read_source(BitStream *bs);
uint64_t read_range(VM *vm, Range r, size_t stream_pos);
void     write_range(VM *vm, Address dst, uint64_t value, uint64_t size_bits,
                     size_t stream_pos);
void     copy_range(VM *vm, Range src, Address dst, size_t stream_pos);

typedef struct {
    uint8_t  type;       // VALUE_INT / VALUE_FLOAT
    uint64_t i;          // для int
    double   f;          // для float
} Value;

uint64_t read_value_sized(VM *vm, Address addr, uint64_t size_bits,
                          size_t stream_pos);
void     write_value_sized(VM *vm, Address addr, uint64_t value,
                           uint64_t size_bits, size_t stream_pos);

// ===== Размеры и адреса =====
uint64_t read_variable_size(BitStream *bs);
void     write_variable_size(BitStream *bs, uint64_t value);

// ===== Адреса =====
RegisterSelector read_register_selector(BitStream *bs);
Address  read_address(BitStream *bs);
uint64_t read_by_address(VM *vm, Address addr, size_t stream_pos);
void     write_by_address(VM *vm, Address addr, uint64_t value,
                          size_t stream_pos);

// ===== Целые нефиксированного размера =====
uint64_t read_int(BitStream *bs);
void     write_int(BitStream *bs, uint64_t value);

// ===== Вещественные (мантисса + порядок) =====
double   read_float(BitStream *bs);
void     write_float(BitStream *bs, double value);

// ===== Значения (тип + значение) =====
Value    read_value(BitStream *bs);
void     write_value(BitStream *bs, Value v);

#endif
