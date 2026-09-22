#ifndef ADDRESSING_H
#define ADDRESSING_H

#include "encoding.h"
#include "vm.h"

typedef struct {
    ActivationRecord *ar;
    ResolvingNetworkEntry superlocal_owner;
    ResolvingNetworkEntry superlocal_resolver;
} RegisterResolutionContext;

Register **resolve_register_slot(VM *vm, RegisterResolutionContext context,
                                 RegisterSelector selector,
                                 size_t stream_pos);
uint64_t read_range(VM *vm, RegisterResolutionContext context, Range range,
                    size_t stream_pos);
void write_range(VM *vm, RegisterResolutionContext context,
                 Address destination,
                 uint64_t value,
                 uint64_t size_bits, size_t stream_pos);
void copy_range(VM *vm, RegisterResolutionContext read_context, Range source,
                RegisterResolutionContext write_context,
                Address destination,
                size_t stream_pos);

uint64_t read_value_sized(VM *vm, RegisterResolutionContext context,
                          Address address, uint64_t size_bits,
                          size_t stream_pos);
void write_value_sized(VM *vm, RegisterResolutionContext context,
                       Address address, uint64_t value,
                       uint64_t size_bits, size_t stream_pos);

uint64_t read_by_address(VM *vm, RegisterResolutionContext context,
                         Address address, size_t stream_pos);
void write_by_address(VM *vm, RegisterResolutionContext context,
                      Address address, uint64_t value,
                      size_t stream_pos);

#endif
