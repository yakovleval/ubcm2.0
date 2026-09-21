#include "vm.h"
#include "addressing.h"
#include "encoding.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

VM *vm_create(void) { return calloc(1, sizeof(VM)); }

void vm_free(VM *vm) {
    for (int i = 0; i < MAX_REGISTERS; i++) {
        if (vm->registers[i]) {
            bv_free(vm->registers[i]->bits);
            free(vm->registers[i]);
        }
    }
    while (vm->current_ar) {
        ActivationRecord *p = vm->current_ar->prev;
        free(vm->current_ar);
        vm->current_ar = p;
    }
    free(vm);
}

void vm_create_register(VM *vm, int num, size_t size_bits) {
    if (num < 0 || num >= MAX_REGISTERS) {
        fprintf(stderr, "FATAL: invalid register number %d\n", num);
        exit(1);
    }
    if (vm->registers[num]) {
        bv_free(vm->registers[num]->bits);
        free(vm->registers[num]);
    }
    Register *reg = malloc(sizeof(Register));
    reg->bits = bv_create(size_bits);
    vm->registers[num] = reg;
}

void vm_delete_register(VM *vm, int num) {
    if (num < 0 || num >= MAX_REGISTERS) {
        fprintf(stderr, "FATAL: invalid register number %d\n", num);
        exit(1);
    }
    if (vm->registers[num]) {
        bv_free(vm->registers[num]->bits);
        free(vm->registers[num]);
        vm->registers[num] = NULL;
    }
}

void vm_resize_register(VM *vm, int num, size_t size_bits) {
    if (num < 0 || num >= MAX_REGISTERS) {
        fprintf(stderr, "FATAL: invalid register number %d\n", num);
        exit(1);
    }
    if (size_bits == 0) {
        vm_delete_register(vm, num);
        return;
    }

    Register *reg = vm->registers[num];
    if (!reg) {
        vm_create_register(vm, num, size_bits);
        return;
    }

    BitVector *old_bits = reg->bits;
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

    reg->bits = new_bits;
    bv_free(old_bits);
}

Register *vm_get_register(VM *vm, int num) {
    if (num < 0 || num >= MAX_REGISTERS) return NULL;
    return vm->registers[num];
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

void vm_set_uint64(VM *vm, int reg_num, uint64_t value) {
    Register *reg = vm_get_register(vm, reg_num);
    if (!reg) { vm_create_register(vm, reg_num, 64); reg = vm_get_register(vm, reg_num); }
    BitCursor cursor = bc_create(reg->bits, 0);
    bc_write_bits(&cursor, value, 64);
}

uint64_t vm_get_uint64(VM *vm, int reg_num) {
    Register *reg = vm_get_register(vm, reg_num);
    if (!reg) return 0;
    BitCursor cursor = bc_create(reg->bits, 0);
    return bc_read_bits(&cursor, 64);
}

static CommandContext command_context_create(VM *vm,
                                             ActivationRecord *read_ar,
                                             ActivationRecord *write_ar,
                                             ResolvingNetworkNode node) {
    Register *proc = vm_get_register(vm, read_ar->proc_reg);
    CommandContext context = {
        .vm = vm,
        .read_ar = read_ar,
        .write_ar = write_ar,
        .node = node,
        .operands = bc_create(proc->bits, read_ar->proc_pos),
    };
    return context;
}

static void command_context_commit_operands(CommandContext *context) {
    context->read_ar->proc_pos = context->operands.pos;
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
        context->vm, condition, 1, context->operands.pos) != 0;
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

    uint64_t v1 = read_range(context->vm, src1, context->operands.pos);
    uint64_t v2 = read_range(context->vm, src2, context->operands.pos);
    uint64_t result = 0;
    switch (opcode) {
        case 0: result = v1 + v2; break;
        case 1: result = v1 - v2; break;
        case 2: result = v1 * v2; break;
        case 3: result = v2 ? v1 / v2 : 0; break;
    }

    uint64_t rsize = src1.size_bits > src2.size_bits
                   ? src1.size_bits : src2.size_bits;
    write_range(context->vm, dst, result, rsize, context->operands.pos);

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

    copy_range(context->vm, src, dst, context->operands.pos);
    printf("[COPY] %llu bits\n", (unsigned long long)src.size_bits);
    return AR_NOP;
}

// 0110: новая процедура, та же РС
static ArAction exec_call_new_proc(CommandContext *context) {
    ActivationRecord *caller = context->write_ar;
    Address src = read_address(&context->operands);
    command_context_commit_operands(context);
    caller->rs_ptr = context->node.next0;

    ActivationRecord *n = calloc(1, sizeof(ActivationRecord));
    n->proc_reg    = src.reg_num;
    n->rs_reg      = caller->rs_reg;
    n->rs_ptr      = context->node.next0;
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
    uint64_t entry = read_by_address(context->vm, src,
                                     context->operands.pos);
    command_context_commit_operands(context);
    caller->rs_ptr = context->node.next0;

    ActivationRecord *n = calloc(1, sizeof(ActivationRecord));
    n->proc_reg    = context->read_ar->proc_reg;
    n->rs_reg      = caller->rs_reg;
    n->rs_ptr      = entry;
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
    caller->rs_ptr = context->node.next0;

    ActivationRecord *n = calloc(1, sizeof(ActivationRecord));
    n->proc_reg    = src_proc.reg_num;
    n->rs_reg      = src_rs.reg_num;
    n->rs_ptr      = src_rs.offset;
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
    uint64_t val = read_range(context->vm, src, context->operands.pos);
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

    uint64_t new_position = read_range(context->vm, position_source,
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
    RegisterSelector target = read_register_selector(&context->operands);
    Range size_source = read_source(&context->operands);
    command_context_commit_operands(context);

    if (target.reg_class != REG_GLOBAL) {
        fprintf(stderr,
                "FATAL: RESIZE supports only global registers "
                "(class=%u, pos=%zu)\n",
                target.reg_class, context->operands.pos);
        exit(1);
    }

    uint64_t new_size = read_range(context->vm, size_source,
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

    vm_resize_register(context->vm, target.reg_num, new_size_bits);
    printf("[RESIZE] reg%u -> %llu bits\n",
           target.reg_num, (unsigned long long)new_size);
    return AR_NOP;
}

// Builtin: GET SIZE (0x0D)
static ArAction exec_get_size(CommandContext *context) {
    RegisterSelector source = read_register_selector(&context->operands);
    Address destination = read_address(&context->operands);
    command_context_commit_operands(context);

    if (source.reg_class != REG_GLOBAL) {
        fprintf(stderr,
                "FATAL: GET SIZE supports only global registers "
                "(class=%u, pos=%zu)\n",
                source.reg_class, context->operands.pos);
        exit(1);
    }

    Register *reg = vm_get_register(context->vm, source.reg_num);
    size_t host_size = reg ? reg->bits->size_bits : 0;
    uint64_t size_bits = (uint64_t)host_size;
    if ((size_t)size_bits != host_size) {
        fprintf(stderr,
                "FATAL: register size does not fit uint64_t "
                "(pos=%zu)\n",
                context->operands.pos);
        exit(1);
    }

    write_by_address(context->vm, destination, size_bits,
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
        caller->rs_ptr = context->node.next0;
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
    ar->rs_reg = rs_reg;
    ar->rs_ptr = 0;
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
    if (!ar) { vm->halted = 1; return; }

    ActivationRecord *read_ar = vm->prefix_read_ar
                              ? vm->prefix_read_ar : ar;
    Register *rs = vm_get_register(vm, ar->rs_reg);
    ResolvingNetworkNode node = resolving_network_read_node(rs->bits,
                                                            ar->rs_ptr);

    if (node.type != 0) {
        fprintf(stderr, "FATAL: node type %d not implemented\n", node.type);
        vm->halted = 1;
        return;
    }

    // CHOICE — обрабатываем отдельно (не в exec_builtin)
    if (node.data == 0x03) {
        Register *proc = vm_get_register(vm, read_ar->proc_reg);
        BitCursor cursor = bc_create(proc->bits, read_ar->proc_pos);
        uint64_t bit = bc_read_bits(&cursor, 1);
        read_ar->proc_pos = cursor.pos;
        ar->rs_ptr = resolving_network_next_node(&node, (uint8_t)bit);
        return;
    }

    int is_prefix = node.data <= 0x02;
    ActivationRecord *write_ar = vm->prefix_write_ar
                               ? vm->prefix_write_ar : ar;
    CommandContext context = command_context_create(
        vm, read_ar, is_prefix ? ar : write_ar, node);
    ActivationRecord *old_ar = context.write_ar;
    ArAction action;
    if (!is_prefix && vm->has_prefix_condition && !vm->prefix_condition) {
        skip_builtin(&context);
        action = AR_NOP;
    } else {
        action = exec_builtin(&context);
    }

    if (!is_prefix) {
        vm->prefix_read_ar = NULL;
        vm->prefix_write_ar = NULL;
        vm->prefix_condition = 0;
        vm->has_prefix_condition = 0;
    }

    switch (action) {
        case AR_NOP:
            if (!vm->halted) {
                old_ar->rs_ptr = node.next0;
                if (ar != old_ar)
                    ar->rs_ptr = node.next0;
            }
            break;
        case AR_CALL:
            // CALL: caller->rs_ptr и callee уже настроены внутри
            break;
        case AR_POP:
            // END CALL: caller уже переключён внутри
            free(old_ar);
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
