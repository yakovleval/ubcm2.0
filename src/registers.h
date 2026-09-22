#ifndef REGISTERS_H
#define REGISTERS_H

#include "encoding.h"
#include "vm.h"

typedef struct {
    ActivationRecord *ar;
    ResolvingNetworkEntry superlocal_owner;
    ResolvingNetworkEntry superlocal_resolver;
} RegisterContext;

Register **resolve_register_slot(VM *vm, RegisterContext context,
                                 RegisterSelector selector,
                                 size_t stream_pos);
Register *resolve_existing_register(VM *vm, RegisterContext context,
                                    RegisterSelector selector,
                                    size_t stream_pos);

#endif
