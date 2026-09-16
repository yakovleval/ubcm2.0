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
    uint8_t  mode;       // ADDR_*
    uint8_t  reg_class;  // REG_*
    uint8_t  reg_num;
    uint64_t offset;     // смещение в битах
    uint64_t imm;        // для immediate
} Address;

typedef struct {
    uint8_t  type;       // VALUE_INT / VALUE_FLOAT
    uint64_t i;          // для int
    double   f;          // для float
} Value;

uint64_t read_value_sized(VM *vm, Address addr, uint64_t size_bits);
void     write_value_sized(VM *vm, Address addr, uint64_t value, uint64_t size_bits);

// ===== Размеры и адреса =====
uint64_t read_variable_size(BitStream *bs);
void     write_variable_size(BitStream *bs, uint64_t value);

// ===== Адреса =====
Address  read_address(BitStream *bs);
uint64_t read_by_address(VM *vm, Address addr);
void     write_by_address(VM *vm, Address addr, uint64_t value);

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
