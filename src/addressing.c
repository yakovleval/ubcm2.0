#include "addressing.h"

#include <stdio.h>
#include <stdlib.h>

typedef enum {
    RESOLVED_IMMEDIATE,
    RESOLVED_VECTOR
} ResolvedRangeKind;

typedef struct {
    ResolvedRangeKind kind;
    BitVector *vector;
    size_t offset;
    size_t size_bits;
    uint64_t immediate;
} ResolvedRange;

static size_t checked_size(uint64_t value, const char *what,
                           size_t stream_pos) {
    size_t result = (size_t)value;
    if ((uint64_t)result != value) {
        fprintf(stderr, "FATAL: %s does not fit size_t at pos=%zu\n",
                what, stream_pos);
        exit(1);
    }
    return result;
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

Register **resolve_register_slot(VM *vm, RegisterResolutionContext context,
                                 RegisterSelector selector,
                                 size_t stream_pos) {
    switch (selector.reg_class) {
        case REG_PROCEDURE:
            return &vm->registers[context.ar->proc_reg];

        case REG_LOCAL: {
            uint16_t slot = resolve_name(vm, context.ar->local_resolver,
                                         selector.reg_num, stream_pos);
            return &context.ar->local_registers[slot];
        }

        case REG_SUPERLOCAL: {
            uint16_t slot = resolve_name(
                vm, context.superlocal_resolver, selector.reg_num,
                stream_pos);
            SuperlocalStorage *storage = get_superlocal_storage(
                vm, context.superlocal_owner);
            return &storage->registers[slot];
        }

        case REG_GLOBAL:
            return &vm->registers[selector.reg_num];

        default:
            fprintf(stderr,
                    "FATAL: invalid register class %u at pos=%zu\n",
                    selector.reg_class, stream_pos);
            exit(1);
    }
}

static Register *resolve_existing_register(
                                           VM *vm,
                                           RegisterResolutionContext context,
                                           uint8_t reg_class,
                                           uint8_t reg_num,
                                           size_t stream_pos) {
    RegisterSelector selector = {
        .reg_class = reg_class,
        .reg_num = reg_num,
    };
    Register **slot = resolve_register_slot(vm, context, selector,
                                            stream_pos);
    if (!*slot) {
        fprintf(stderr, "FATAL: register not found at pos=%zu\n",
                stream_pos);
        exit(1);
    }
    return *slot;
}

static ResolvedRange resolve_address(VM *vm,
                                     RegisterResolutionContext context,
                                     Address address,
                                     uint64_t size_bits, int writable,
                                     size_t stream_pos) {
    ResolvedRange resolved = {0};
    resolved.size_bits = checked_size(size_bits, "range size", stream_pos);

    switch (address.mode) {
        case ADDR_IMMEDIATE:
            if (writable) {
                fprintf(stderr,
                        "FATAL: cannot write to immediate at pos=%zu\n",
                        stream_pos);
                exit(1);
            }
            if (resolved.size_bits > 64) {
                fprintf(stderr,
                        "FATAL: immediate exceeds 64 bits at pos=%zu\n",
                        stream_pos);
                exit(1);
            }
            resolved.kind = RESOLVED_IMMEDIATE;
            resolved.immediate = address.imm;
            return resolved;

        case ADDR_DIRECT: {
            Register *reg = resolve_existing_register(
                vm, context, address.reg_class, address.reg_num, stream_pos);
            resolved.kind = RESOLVED_VECTOR;
            resolved.vector = reg->bits;
            resolved.offset = checked_size(address.offset, "range offset",
                                           stream_pos);
            if (resolved.offset > resolved.vector->size_bits ||
                resolved.size_bits >
                    resolved.vector->size_bits - resolved.offset) {
                fprintf(stderr,
                        "FATAL: range out of bounds at pos=%zu\n",
                        stream_pos);
                exit(1);
            }
            return resolved;
        }

        case ADDR_INDIRECT: {
            Register *pointer_register = resolve_existing_register(
                vm, context, address.reg_class, address.reg_num,
                stream_pos);
            size_t pointer_offset = checked_size(
                address.offset, "indirect address offset", stream_pos);
            if (pointer_offset >= pointer_register->bits->size_bits) {
                fprintf(stderr,
                        "FATAL: indirect address out of bounds at pos=%zu\n",
                        stream_pos);
                exit(1);
            }

            BitCursor cursor = bc_create(pointer_register->bits,
                                         pointer_offset);
            Address target = read_address(&cursor);
            if (target.mode != ADDR_DIRECT) {
                fprintf(stderr,
                        "FATAL: indirect address must point to a direct "
                        "reference at pos=%zu\n",
                        stream_pos);
                exit(1);
            }
            return resolve_address(vm, context, target, size_bits,
                                   writable, stream_pos);
        }

        case ADDR_FOREIGN:
            fprintf(stderr,
                    "FATAL: foreign addressing not implemented at pos=%zu\n",
                    stream_pos);
            exit(1);

        default:
            fprintf(stderr,
                    "FATAL: invalid addressing mode %u at pos=%zu\n",
                    address.mode, stream_pos);
            exit(1);
    }
}

uint64_t read_range(VM *vm, RegisterResolutionContext context, Range range,
                    size_t stream_pos) {
    ResolvedRange resolved = resolve_address(vm, context, range.addr,
                                             range.size_bits, 0, stream_pos);
    if (resolved.size_bits > 64) {
        fprintf(stderr, "FATAL: read size exceeds 64 bits at pos=%zu\n",
                stream_pos);
        exit(1);
    }
    if (resolved.kind == RESOLVED_IMMEDIATE)
        return resolved.immediate;

    BitCursor cursor = bc_create(resolved.vector, resolved.offset);
    return bc_read_bits(&cursor, (int)resolved.size_bits);
}

void write_range(VM *vm, RegisterResolutionContext context,
                 Address destination,
                 uint64_t value,
                 uint64_t size_bits, size_t stream_pos) {
    ResolvedRange resolved = resolve_address(vm, context, destination,
                                             size_bits, 1, stream_pos);
    if (resolved.size_bits > 64) {
        fprintf(stderr, "FATAL: write size exceeds 64 bits at pos=%zu\n",
                stream_pos);
        exit(1);
    }
    BitCursor cursor = bc_create(resolved.vector, resolved.offset);
    bc_write_bits(&cursor, value, (int)resolved.size_bits);
}

static void copy_cursors(BitCursor *source, BitCursor *destination,
                         size_t size_bits) {
    while (size_bits > 0) {
        int chunk = size_bits > 64 ? 64 : (int)size_bits;
        uint64_t value = bc_read_bits(source, chunk);
        bc_write_bits(destination, value, chunk);
        size_bits -= (size_t)chunk;
    }
}

void copy_range(VM *vm, RegisterResolutionContext read_context, Range source,
                RegisterResolutionContext write_context,
                Address destination,
                size_t stream_pos) {
    ResolvedRange resolved_source = resolve_address(
        vm, read_context, source.addr, source.size_bits, 0, stream_pos);
    ResolvedRange resolved_destination = resolve_address(
        vm, write_context, destination, source.size_bits, 1, stream_pos);
    BitVector *temporary = bv_create(resolved_source.size_bits);
    BitCursor temporary_writer = bc_create(temporary, 0);

    if (resolved_source.kind == RESOLVED_IMMEDIATE) {
        bc_write_bits(&temporary_writer, resolved_source.immediate,
                      (int)resolved_source.size_bits);
    } else {
        BitCursor source_cursor = bc_create(resolved_source.vector,
                                            resolved_source.offset);
        copy_cursors(&source_cursor, &temporary_writer,
                     resolved_source.size_bits);
    }

    BitCursor temporary_reader = bc_create(temporary, 0);
    BitCursor destination_cursor = bc_create(resolved_destination.vector,
                                             resolved_destination.offset);
    copy_cursors(&temporary_reader, &destination_cursor,
                 resolved_source.size_bits);
    bv_free(temporary);
}

uint64_t read_value_sized(VM *vm, RegisterResolutionContext context,
                          Address address, uint64_t size_bits,
                          size_t stream_pos) {
    Range range = {.addr = address, .size_bits = size_bits};
    return read_range(vm, context, range, stream_pos);
}

void write_value_sized(VM *vm, RegisterResolutionContext context,
                       Address address, uint64_t value,
                       uint64_t size_bits, size_t stream_pos) {
    write_range(vm, context, address, value, size_bits, stream_pos);
}

uint64_t read_by_address(VM *vm, RegisterResolutionContext context,
                         Address address, size_t stream_pos) {
    return read_value_sized(vm, context, address, 64, stream_pos);
}

void write_by_address(VM *vm, RegisterResolutionContext context,
                      Address address, uint64_t value,
                      size_t stream_pos) {
    write_value_sized(vm, context, address, value, 64, stream_pos);
}
