#include "registers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void vm_free_register_slot(Register **slot) {
    if (*slot) {
        bv_free((*slot)->bits);
        free(*slot);
        *slot = NULL;
    }
}

void vm_resize_register_slot(Register **slot, size_t size_bits) {
    if (size_bits == 0) {
        vm_free_register_slot(slot);
        return;
    }
    if (!*slot) {
        *slot = malloc(sizeof(Register));
        (*slot)->bits = bv_create(size_bits);
        return;
    }

    BitVector *old_bits = (*slot)->bits;
    BitVector *new_bits = bv_create(size_bits);
    size_t preserved_bits = old_bits->size_bits < size_bits
                          ? old_bits->size_bits : size_bits;
    size_t full_bytes = preserved_bits / 8;
    size_t remaining_bits = preserved_bits % 8;
    if (full_bytes > 0)
        memcpy(new_bits->data, old_bits->data, full_bytes);
    if (remaining_bits > 0) {
        uint8_t mask = (uint8_t)(0xFFu << (8 - remaining_bits));
        new_bits->data[full_bytes] = old_bits->data[full_bytes] & mask;
    }
    (*slot)->bits = new_bits;
    bv_free(old_bits);
}

void vm_create_register(VM *vm, int num, size_t size_bits) {
    if (num < 0 || num >= MAX_REGISTERS) {
        fprintf(stderr, "FATAL: invalid register number %d\n", num);
        exit(1);
    }
    vm_free_register_slot(&vm->registers[num]);
    Register *reg = malloc(sizeof(Register));
    reg->bits = bv_create(size_bits);
    vm->registers[num] = reg;
}

void vm_delete_register(VM *vm, int num) {
    if (num < 0 || num >= MAX_REGISTERS) {
        fprintf(stderr, "FATAL: invalid register number %d\n", num);
        exit(1);
    }
    vm_free_register_slot(&vm->registers[num]);
}

void vm_resize_register(VM *vm, int num, size_t size_bits) {
    if (num < 0 || num >= MAX_REGISTERS) {
        fprintf(stderr, "FATAL: invalid register number %d\n", num);
        exit(1);
    }
    vm_resize_register_slot(&vm->registers[num], size_bits);
}

Register *vm_get_register(VM *vm, int num) {
    if (num < 0 || num >= MAX_REGISTERS)
        return NULL;
    return vm->registers[num];
}

void vm_set_uint64(VM *vm, int reg_num, uint64_t value) {
    Register *reg = vm_get_register(vm, reg_num);
    if (!reg) {
        vm_create_register(vm, reg_num, 64);
        reg = vm_get_register(vm, reg_num);
    }
    BitCursor cursor = bc_create(reg->bits, 0);
    bc_write_bits(&cursor, value, 64);
}

uint64_t vm_get_uint64(VM *vm, int reg_num) {
    Register *reg = vm_get_register(vm, reg_num);
    if (!reg)
        return 0;
    BitCursor cursor = bc_create(reg->bits, 0);
    return bc_read_bits(&cursor, 64);
}

static uint16_t resolve_name(VM *vm, ResolvingNetworkEntry entry,
                             uint8_t name, size_t stream_pos) {
    ResolvingNetworkEntry cursor = entry;
    for (int bit_index = 4; bit_index >= 0; bit_index--) {
        ResolvingNetworkNode node = vm_read_resolving_network_node(
            vm, cursor, stream_pos);
        uint8_t bit = (uint8_t)((name >> bit_index) & 1u);
        cursor = resolving_network_next_entry(cursor, &node, bit);
    }
    ResolvingNetworkNode leaf = vm_read_resolving_network_node(
        vm, cursor, stream_pos);
    if (leaf.data >= MAX_REGISTERS) {
        fprintf(stderr,
                "FATAL: resolved register slot %u is invalid at pos=%zu\n",
                leaf.data, stream_pos);
        exit(1);
    }
    return leaf.data;
}

static int entries_equal(ResolvingNetworkEntry left,
                         ResolvingNetworkEntry right) {
    return left.reg_num == right.reg_num &&
           left.node_index == right.node_index;
}

static SuperlocalStorage *get_superlocal_storage(
    VM *vm, ResolvingNetworkEntry owner_node) {
    SuperlocalStorage *storage = vm->superlocal_storages;
    while (storage) {
        if (entries_equal(storage->owner_node, owner_node))
            return storage;
        storage = storage->next;
    }
    storage = calloc(1, sizeof(SuperlocalStorage));
    storage->owner_node = owner_node;
    storage->next = vm->superlocal_storages;
    vm->superlocal_storages = storage;
    return storage;
}

Register **resolve_register_slot(VM *vm, RegisterContext context,
                                 ParsedRegisterType reg_type,
                                 size_t stream_pos) {
    switch (reg_type.reg_class) {
        case REG_PROCEDURE:
            return &vm->registers[context.ar->proc_reg];

        case REG_LOCAL: {
            uint16_t slot = resolve_name(vm, context.ar->local_resolver,
                                         reg_type.reg_num, stream_pos);
            return &context.ar->local_registers[slot];
        }

        case REG_SUPERLOCAL: {
            uint16_t slot = resolve_name(
                vm, context.superlocal_resolver, reg_type.reg_num,
                stream_pos);
            SuperlocalStorage *storage = get_superlocal_storage(
                vm, context.superlocal_owner);
            return &storage->registers[slot];
        }

        case REG_GLOBAL:
            return &vm->registers[reg_type.reg_num];

        default:
            fprintf(stderr,
                    "FATAL: invalid register class %u at pos=%zu\n",
                    reg_type.reg_class, stream_pos);
            exit(1);
    }
}

Register *resolve_existing_register(VM *vm, RegisterContext context,
                                    ParsedRegisterType reg_type,
                                    size_t stream_pos) {
    Register **slot = resolve_register_slot(vm, context, reg_type,
                                            stream_pos);
    if (!*slot) {
        fprintf(stderr, "FATAL: register not found at pos=%zu\n",
                stream_pos);
        exit(1);
    }
    return *slot;
}
