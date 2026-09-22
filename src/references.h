#ifndef REFERENCES_H
#define REFERENCES_H

#include "registers.h"

uint64_t read_range(VM *vm, RegisterContext context, Range range,
                    size_t stream_pos);
void write_range(VM *vm, RegisterContext context,
                 Address destination, uint64_t value,
                 uint64_t size_bits, size_t stream_pos);
void copy_range(VM *vm, RegisterContext read_context, Range source,
                RegisterContext write_context,
                Address destination, size_t stream_pos);

uint64_t read_value_sized(VM *vm, RegisterContext context,
                          Address address, uint64_t size_bits,
                          size_t stream_pos);
void write_value_sized(VM *vm, RegisterContext context,
                       Address address, uint64_t value,
                       uint64_t size_bits, size_t stream_pos);

uint64_t read_by_address(VM *vm, RegisterContext context,
                         Address address, size_t stream_pos);
void write_by_address(VM *vm, RegisterContext context,
                      Address address, uint64_t value,
                      size_t stream_pos);

#endif
