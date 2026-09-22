#ifndef ENCODING_H
#define ENCODING_H

#include "bitstream.h"
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
} SimpleAddress;

typedef struct {
    SimpleAddress depth_address;
    SimpleAddress working_address;
} ForeignAddress;

typedef struct {
    uint8_t  mode;
    uint8_t  reg_class;
    uint8_t  reg_num;
    uint64_t offset;
    uint64_t imm;
    uint64_t imm_size;
    ForeignAddress foreign;
} Address;

typedef struct {
    Address  addr;
    uint64_t size_bits;  // размер диапазона или immediate-значения
} Range;

Range    read_source(BitCursor *cursor);

typedef struct {
    uint8_t  type;       // VALUE_INT / VALUE_FLOAT
    uint64_t i;          // для int
    double   f;          // для float
} Value;

// ===== Размеры и адреса =====
uint64_t read_variable_size(BitCursor *cursor);
void     write_variable_size(BitCursor *cursor, uint64_t value);

// ===== Адреса =====
RegisterSelector read_register_selector(BitCursor *cursor);
SimpleAddress read_simple_address(BitCursor *cursor);
Address  read_address(BitCursor *cursor);

// ===== Целые нефиксированного размера =====
uint64_t read_int(BitCursor *cursor);
void     write_int(BitCursor *cursor, uint64_t value);

// ===== Вещественные (мантисса + порядок) =====
double   read_float(BitCursor *cursor);
void     write_float(BitCursor *cursor, double value);

// ===== Значения (тип + значение) =====
Value    read_value(BitCursor *cursor);
void     write_value(BitCursor *cursor, Value v);

#endif
