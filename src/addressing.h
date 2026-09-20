#ifndef ADDRESSING_H
#define ADDRESSING_H

#include "encoding.h"
#include "vm.h"

uint64_t read_range(VM *vm, Range range, size_t stream_pos);
void write_range(VM *vm, Address destination, uint64_t value,
                 uint64_t size_bits, size_t stream_pos);
void copy_range(VM *vm, Range source, Address destination,
                size_t stream_pos);

uint64_t read_value_sized(VM *vm, Address address, uint64_t size_bits,
                          size_t stream_pos);
void write_value_sized(VM *vm, Address address, uint64_t value,
                       uint64_t size_bits, size_t stream_pos);

uint64_t read_by_address(VM *vm, Address address, size_t stream_pos);
void write_by_address(VM *vm, Address address, uint64_t value,
                      size_t stream_pos);

#endif
