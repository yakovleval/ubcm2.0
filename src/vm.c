#include "vm.h"
#include "registers.h"
#include "encoding.h"
#include "references.h"
#include <stdio.h>
#include <stdlib.h>

VM *vm_create(void) { return calloc(1, sizeof(VM)); }

static void activation_record_free(ActivationRecord *ar) {
    for (int i = 0; i < MAX_REGISTERS; i++)
        vm_free_register_slot(&ar->local_registers[i]);
    free(ar);
}

void vm_free(VM *vm) {
    for (int i = 0; i < MAX_REGISTERS; i++) {
        if (vm->registers[i]) {
            vm_free_register_slot(&vm->registers[i]);
        }
    }
    while (vm->current_ar) {
        ActivationRecord *p = vm->current_ar->prev;
        activation_record_free(vm->current_ar);
        vm->current_ar = p;
    }
    while (vm->superlocal_storages) {
        SuperlocalStorage *storage = vm->superlocal_storages;
        vm->superlocal_storages = storage->next;
        for (int i = 0; i < MAX_REGISTERS; i++)
            vm_free_register_slot(&storage->registers[i]);
        free(storage);
    }
    free(vm);
}

ResolvingNetworkNode vm_read_resolving_network_node(
    VM *vm, ResolvingNetworkEntry entry, size_t stream_pos) {
    Register *network = vm_get_register(vm, entry.reg_num);
    if (!network) {
        fprintf(stderr,
                "FATAL: resolving network register %d not found at pos=%zu\n",
                entry.reg_num, stream_pos);
        exit(1);
    }
    return resolving_network_decode_node(network->bits, entry.node_index);
}

static void load_file_into_register(VM *vm, int reg_num, const char *filename) {
    if (!filename) {
        fprintf(stderr, "FATAL: filename is null\n");
        exit(1);
    }
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "FATAL: cannot open file: %s\n", filename);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    vm_create_register(vm, reg_num, size * 8);
    Register *reg = vm_get_register(vm, reg_num);
    if (fread(reg->bits->data, 1, size, f) != size) {
        fclose(f);
        fprintf(stderr, "FATAL: error reading file: %s\n",
                filename ? filename : "(null)");
        exit(1);
    }
    fclose(f);
}

void vm_load_procedure(VM *vm, int reg_num, const char *filename) {
    load_file_into_register(vm, reg_num, filename);
}

void vm_load_rs(VM *vm, int reg_num, const char *filename) {
    load_file_into_register(vm, reg_num, filename);
}

static CommandContext command_context_create(VM *vm,
                                             ActivationRecord *execution_ar,
                                             ActivationRecord *read_ar,
                                             ActivationRecord *write_ar,
                                             ResolvingNetworkEntry node_entry,
                                             ResolvingNetworkNode node) {
    Register *proc = vm_get_register(vm, execution_ar->proc_reg);
    CommandContext context = {
        .vm = vm,
        .execution_ar = execution_ar,
        .read_ar = read_ar,
        .write_ar = write_ar,
        .node = node,
        .node_entry = node_entry,
        .operands = bc_create(proc->bits, execution_ar->proc_pos),
    };
    return context;
}

static void command_context_commit_operands(CommandContext *context) {
    context->execution_ar->proc_pos = context->operands.pos;
}

static RegisterContext command_register_context(
    CommandContext *context, ActivationRecord *ar) {
    RegisterContext register_context = {
        .ar = ar,
        .superlocal_owner = context->node_entry,
        .superlocal_resolver = {
            .reg_num = context->node_entry.reg_num,
            .node_index = context->node.resolver,
        },
    };
    return register_context;
}

static ActivationRecord *activation_record_at_depth(VM *vm, uint64_t depth,
                                                     size_t stream_pos) {
    ActivationRecord *ar = vm->current_ar;
    for (uint64_t index = 0; index < depth; index++) {
        if (!ar || !ar->prev) {
            fprintf(stderr,
                    "FATAL: activation record depth %llu is out of bounds "
                    "at pos=%zu\n",
                    (unsigned long long)depth, stream_pos);
            exit(1);
        }
        ar = ar->prev;
    }
    return ar;
}

static ArAction exec_read_prefix(CommandContext *context) {
    uint64_t depth = read_int(&context->operands);
    command_context_commit_operands(context);
    context->vm->prefix_read_ar = activation_record_at_depth(
        context->vm, depth, context->operands.pos);
    return AR_NOP;
}

static ArAction exec_write_prefix(CommandContext *context) {
    uint64_t depth = read_int(&context->operands);
    command_context_commit_operands(context);
    context->vm->prefix_write_ar = activation_record_at_depth(
        context->vm, depth, context->operands.pos);
    return AR_NOP;
}

static ArAction exec_conditional_prefix(CommandContext *context) {
    Address condition = read_address(&context->operands);
    command_context_commit_operands(context);
    context->vm->prefix_condition = read_value_sized(
        context->vm,
        command_register_context(context, context->read_ar), condition, 1,
        context->operands.pos) != 0;
    context->vm->has_prefix_condition = 1;
    return AR_NOP;
}

// Builtin: COMPUTE (0x04)
static ArAction exec_compute(CommandContext *context) {
    uint64_t opcode = bc_read_bits(&context->operands, 5);
    // TODO: не все операции принимают два операнда, доработать для 1-арных операций
    Range src1 = read_source(&context->operands);
    Range src2 = read_source(&context->operands);
    Address dst = read_address(&context->operands);
    command_context_commit_operands(context);

    RegisterContext read_context = command_register_context(
        context, context->read_ar);
    RegisterContext write_context = command_register_context(
        context, context->write_ar);
    uint64_t v1 = read_range(context->vm, read_context, src1,
                             context->operands.pos);
    uint64_t v2 = read_range(context->vm, read_context, src2,
                             context->operands.pos);
    uint64_t result = 0;
    switch (opcode) {
        case 0: result = v1 + v2; break;
        case 1: result = v1 - v2; break;
        case 2: result = v1 * v2; break;
        case 3: result = v2 ? v1 / v2 : 0; break;
    }

    uint64_t rsize = src1.size_bits > src2.size_bits
                   ? src1.size_bits : src2.size_bits;
    write_range(context->vm, write_context, dst, result, rsize,
                context->operands.pos);

    printf("[COMPUTE] op=%llu -> reg%d[%llu] = %llu (size=%llu)\n",
           (unsigned long long)opcode, dst.reg_num,
           (unsigned long long)dst.offset,
           (unsigned long long)result, (unsigned long long)rsize);
    return AR_NOP;
}

// Builtin: COPY (0x05)
static ArAction exec_copy(CommandContext *context) {
    Range src = read_source(&context->operands);
    Address dst = read_address(&context->operands);
    command_context_commit_operands(context);

    copy_range(context->vm,
               command_register_context(context, context->read_ar), src,
               command_register_context(context, context->write_ar), dst,
               context->operands.pos);
    printf("[COPY] %llu bits\n", (unsigned long long)src.size_bits);
    return AR_NOP;
}

// 0110: новая процедура, та же РС
static ArAction exec_call_new_proc(CommandContext *context) {
    ActivationRecord *caller = context->write_ar;
    Address src = read_address(&context->operands);
    command_context_commit_operands(context);
    caller->current_node.node_index = context->node.next0;

    ActivationRecord *n = calloc(1, sizeof(ActivationRecord));
    n->proc_reg    = src.reg_num;
    n->current_node = caller->current_node;
    n->local_resolver = caller->local_resolver;
    n->proc_pos    = src.offset;
    n->shares_rs   = 1;
    n->shares_proc = 0;
    n->prev        = caller;
    context->vm->current_ar = n;

    printf("[CALL 0110] proc_reg=%d\n", src.reg_num);
    return AR_CALL;
}

// 0111: та же процедура, новая РС
static ArAction exec_call_new_rs(CommandContext *context) {
    ActivationRecord *caller = context->write_ar;
    Address src = read_address(&context->operands);
    uint64_t entry = read_by_address(
        context->vm, command_register_context(context, context->read_ar), src,
                                     context->operands.pos);
    command_context_commit_operands(context);
    caller->current_node.node_index = context->node.next0;

    ActivationRecord *n = calloc(1, sizeof(ActivationRecord));
    n->proc_reg    = context->read_ar->proc_reg;
    n->current_node.reg_num = caller->current_node.reg_num;
    n->current_node.node_index = entry;
    n->local_resolver = caller->local_resolver;
    n->proc_pos    = context->read_ar->proc_pos;
    n->shares_rs   = 0;
    n->shares_proc = 1;
    n->prev        = caller;
    context->vm->current_ar = n;

    printf("[CALL 0111] entry=%llu\n", (unsigned long long)entry);
    return AR_CALL;
}

// 1000: новая процедура + новая РС
static ArAction exec_call_new_both(CommandContext *context) {
    ActivationRecord *caller = context->write_ar;
    Address src_proc = read_address(&context->operands);
    Address src_rs = read_address(&context->operands);
    command_context_commit_operands(context);
    caller->current_node.node_index = context->node.next0;

    ActivationRecord *n = calloc(1, sizeof(ActivationRecord));
    n->proc_reg    = src_proc.reg_num;
    n->current_node.reg_num = src_rs.reg_num;
    n->current_node.node_index = src_rs.offset;
    n->local_resolver = caller->local_resolver;
    n->proc_pos    = src_proc.offset;
    n->shares_rs   = 0;
    n->shares_proc = 0;
    n->prev        = caller;
    context->vm->current_ar = n;

    printf("[CALL 1000] proc=%d, rs=%d, entry=%llu\n",
           src_proc.reg_num, src_rs.reg_num,
           (unsigned long long)src_rs.offset);
    return AR_CALL;
}

// Builtin: RETURN RESULT (0x09)
static ArAction exec_return_result(CommandContext *context) {
    ActivationRecord *ar = context->write_ar;
    Range src = read_source(&context->operands);
    command_context_commit_operands(context);
    uint64_t val = read_range(
        context->vm, command_register_context(context, context->read_ar), src,
                              context->operands.pos);
    printf("[RETURN RESULT] val=%llu\n", (unsigned long long)val);

    if (ar->prev) {
        ar->prev->has_result = 1;
        ar->prev->result_value = val;
    } else {
        context->vm->has_result = 1;
        context->vm->result_value = val;
    }

    return AR_NOP;
}

// Builtin: JUMP (0x0A)
static ArAction exec_jump(CommandContext *context) {
    Range position_source = read_source(&context->operands);
    command_context_commit_operands(context);

    uint64_t new_position = read_range(
                                       context->vm,
                                       command_register_context(
                                           context, context->read_ar),
                                       position_source,
                                       context->operands.pos);
    size_t new_position_bits = (size_t)new_position;
    if ((uint64_t)new_position_bits != new_position) {
        fprintf(stderr,
                "FATAL: JUMP position does not fit size_t "
                "(position=%llu, pos=%zu)\n",
                (unsigned long long)new_position,
                context->operands.pos);
        exit(1);
    }

    Register *procedure = vm_get_register(context->vm,
                                          context->write_ar->proc_reg);
    if (!procedure) {
        fprintf(stderr,
                "FATAL: JUMP procedure register %d not found at pos=%zu\n",
                context->write_ar->proc_reg, context->operands.pos);
        exit(1);
    }
    if (new_position_bits > procedure->bits->size_bits) {
        fprintf(stderr,
                "FATAL: JUMP position out of bounds "
                "(position=%llu, size=%zu, pos=%zu)\n",
                (unsigned long long)new_position,
                procedure->bits->size_bits, context->operands.pos);
        exit(1);
    }

    context->write_ar->proc_pos = new_position;
    printf("[JUMP] -> %llu\n", (unsigned long long)new_position);
    return AR_NOP;
}

// Builtin: RESIZE (0x0C)
static ArAction exec_resize(CommandContext *context) {
    ParsedRegisterType target = read_register_selector(&context->operands);
    Range size_source = read_source(&context->operands);
    command_context_commit_operands(context);

    uint64_t new_size = read_range(
                                   context->vm,
                                   command_register_context(
                                       context, context->read_ar),
                                   size_source,
                                   context->operands.pos);
    size_t new_size_bits = (size_t)new_size;
    if ((uint64_t)new_size_bits != new_size) {
        fprintf(stderr,
                "FATAL: register size does not fit size_t "
                "(size=%llu, pos=%zu)\n",
                (unsigned long long)new_size,
                context->operands.pos);
        exit(1);
    }

    Register **target_slot = resolve_register_slot(
        context->vm,
        command_register_context(context, context->write_ar), target,
        context->operands.pos);
    vm_resize_register_slot(target_slot, new_size_bits);
    printf("[RESIZE] reg%u -> %llu bits\n",
           target.reg_num, (unsigned long long)new_size);
    return AR_NOP;
}

// Builtin: GET SIZE (0x0D)
static ArAction exec_get_size(CommandContext *context) {
    ParsedRegisterType source = read_register_selector(&context->operands);
    Address destination = read_address(&context->operands);
    command_context_commit_operands(context);

    Register **source_slot = resolve_register_slot(
        context->vm,
        command_register_context(context, context->read_ar), source,
        context->operands.pos);
    Register *reg = *source_slot;
    size_t host_size = reg ? reg->bits->size_bits : 0;
    uint64_t size_bits = (uint64_t)host_size;
    if ((size_t)size_bits != host_size) {
        fprintf(stderr,
                "FATAL: register size does not fit uint64_t "
                "(pos=%zu)\n",
                context->operands.pos);
        exit(1);
    }

    write_by_address(context->vm,
                     command_register_context(context, context->write_ar),
                     destination, size_bits,
                     context->operands.pos);
    printf("[GET SIZE] reg%u -> %llu bits\n",
           source.reg_num, (unsigned long long)size_bits);
    return AR_NOP;
}

// Builtin: END CALL (0x0B)
static ArAction exec_end_call(CommandContext *context) {
    ActivationRecord *ar = context->write_ar;

    if (!ar->prev) {
        context->vm->current_ar = NULL;
        context->vm->halted = 1;
        printf("[EXIT] Halted.\n");
        return AR_POP;
    }

    ActivationRecord *caller = ar->prev;
    if (ar->shares_rs)
        caller->current_node.node_index = context->node.next0;
    if (ar->shares_proc)
        caller->proc_pos = ar->proc_pos;

    context->vm->current_ar = caller;
    return AR_POP;
}

static ArAction exec_builtin(CommandContext *context) {
    switch (context->node.data) {
        case 0x00: return exec_read_prefix(context);
        case 0x01: return exec_write_prefix(context);
        case 0x02: return exec_conditional_prefix(context);
        case 0x04: return exec_compute(context);
        case 0x05: return exec_copy(context);
        case 0x06: return exec_call_new_proc(context);
        case 0x07: return exec_call_new_rs(context);
        case 0x08: return exec_call_new_both(context);
        case 0x09: return exec_return_result(context);
        case 0x0A: return exec_jump(context);
        case 0x0B: return exec_end_call(context);
        case 0x0C: return exec_resize(context);
        case 0x0D: return exec_get_size(context);
        default:
            fprintf(stderr, "FATAL: unknown builtin 0x%X\n",
                    context->node.data);
            context->vm->halted = 1;
            return AR_NOP;
    }
}

static void skip_builtin(CommandContext *context) {
    switch (context->node.data) {
        case 0x04:
            (void)bc_read_bits(&context->operands, 5);
            (void)read_source(&context->operands);
            (void)read_source(&context->operands);
            (void)read_address(&context->operands);
            break;
        case 0x05:
            (void)read_source(&context->operands);
            (void)read_address(&context->operands);
            break;
        case 0x06:
        case 0x07:
            (void)read_address(&context->operands);
            break;
        case 0x08:
            (void)read_address(&context->operands);
            (void)read_address(&context->operands);
            break;
        case 0x09:
        case 0x0A:
            (void)read_source(&context->operands);
            break;
        case 0x0B:
            break;
        case 0x0C:
            (void)read_register_selector(&context->operands);
            (void)read_source(&context->operands);
            break;
        case 0x0D:
            (void)read_register_selector(&context->operands);
            (void)read_address(&context->operands);
            break;
        default:
            fprintf(stderr, "FATAL: cannot skip builtin 0x%X at pos=%zu\n",
                    context->node.data, context->operands.pos);
            exit(1);
    }
    command_context_commit_operands(context);
}

void vm_start(VM *vm, int proc_reg, int rs_reg) {
    ActivationRecord *ar = calloc(1, sizeof(ActivationRecord));
    ar->proc_reg = proc_reg;
    ar->current_node.reg_num = rs_reg;
    ar->current_node.node_index = 0;
    ar->local_resolver.reg_num = rs_reg;
    ar->local_resolver.node_index = DEFAULT_LOCAL_RESOLVER_NODE;
    ar->proc_pos = 0;
    ar->prev = NULL;
    vm->current_ar = ar;
    vm->prefix_read_ar = NULL;
    vm->prefix_write_ar = NULL;
    vm->prefix_condition = 0;
    vm->has_prefix_condition = 0;
    vm->has_result = 0;
    vm->result_value = 0;
    vm->halted = 0;
}

void vm_step(VM *vm) {
    if (vm->halted) return;
    ActivationRecord *ar = vm->current_ar;
    ActivationRecord *old_ar = vm->current_ar;
    if (!ar) { vm->halted = 1; return; }

    ResolvingNetworkEntry node_entry = ar->current_node;
    ResolvingNetworkNode node = vm_read_resolving_network_node(
        vm, node_entry, ar->proc_pos);

    if (node.type != 0) {
        fprintf(stderr, "FATAL: node type %d not implemented\n", node.type);
        vm->halted = 1;
        return;
    }

    ArAction action;
    CommandContext context;
    switch (node.data) {
        case 0:
        case 1:
        case 2:
            context = command_context_create(vm, ar, ar, ar, node_entry,
                                             node);
            action = exec_builtin(&context);
        break;
        case 3:
            Register *proc = vm_get_register(vm, ar->proc_reg);
            BitCursor cursor = bc_create(proc->bits, ar->proc_pos);
            uint64_t bit = bc_read_bits(&cursor, 1);
            ar->proc_pos = cursor.pos;
            ar->current_node = resolving_network_next_entry(
                node_entry, &node, (uint8_t)bit);
            return;
        default:
            int should_skip = vm->has_prefix_condition && !vm->prefix_condition;
            if (should_skip) {
                context = command_context_create(vm, ar, ar, ar, node_entry,
                                                 node);
                skip_builtin(&context);
                action = AR_NOP;
            } else {
                ActivationRecord *prefix_read_ar = vm->prefix_read_ar ? vm->prefix_read_ar : ar;
                ActivationRecord *prefix_write_ar = vm->prefix_write_ar ? vm->prefix_write_ar : ar;
                context = command_context_create(
                    vm, ar, prefix_read_ar, prefix_write_ar, node_entry,
                    node);
                action = exec_builtin(&context);
            }
            vm->prefix_read_ar = NULL;
            vm->prefix_write_ar = NULL;
            vm->prefix_condition = 0;
            vm->has_prefix_condition = 0;
        break;
    }
    switch (action) {
        case AR_NOP:
                ar->current_node.node_index = node.next0;
            break;
        case AR_CALL:
            // CALL: caller->rs_ptr и callee уже настроены внутри
            break;
        case AR_POP:
            // END CALL: caller уже переключён внутри
            activation_record_free(old_ar);
            break;
    }
}

void vm_run(VM *vm) {
    int steps = 0;
    while (!vm->halted && steps < 1000) {
        vm_step(vm);
        steps++;
    }
    if (steps >= 1000) fprintf(stderr, "[FATAL] Step limit reached.\n");
}
