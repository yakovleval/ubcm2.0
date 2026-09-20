#include "vm.h"
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

// Чтение узла из РС по адресу
static Node read_node(BitVector *bits, uint64_t addr) {
    BitCursor cursor = bc_create(bits, addr * 64);
    Node n;
    n.type     = bc_read_bits(&cursor, 1);
    n.data     = bc_read_bits(&cursor, 15);
    n.next0    = bc_read_bits(&cursor, 16);
    n.next1    = bc_read_bits(&cursor, 16);
    n.resolver = bc_read_bits(&cursor, 16);
    return n;
}

// Builtin: COMPUTE (0x04)
static ArAction exec_compute(VM *vm) {
    ActivationRecord *ar = vm->current_ar;
    Register *proc = vm_get_register(vm, ar->proc_reg);
    BitCursor cursor = bc_create(proc->bits, ar->proc_pos);

    uint64_t opcode = bc_read_bits(&cursor, 5);
    Range src1 = read_source(&cursor);
    Range src2 = read_source(&cursor);
    Address dst = read_address(&cursor);   // приёмник — без размера
    ar->proc_pos = cursor.pos;

    uint64_t v1 = read_range(vm, src1, ar->proc_pos);
    uint64_t v2 = read_range(vm, src2, ar->proc_pos);
    uint64_t result = 0;
    switch (opcode) {
        case 0: result = v1 + v2; break;
        case 1: result = v1 - v2; break;
        case 2: result = v1 * v2; break;
        case 3: result = v2 ? v1 / v2 : 0; break;
    }

    uint64_t rsize = src1.size_bits > src2.size_bits
                   ? src1.size_bits : src2.size_bits;
    write_range(vm, dst, result, rsize, ar->proc_pos);

    printf("[COMPUTE] op=%llu -> reg%d[%llu] = %llu (size=%llu)\n",
           (unsigned long long)opcode, dst.reg_num,
           (unsigned long long)dst.offset,
           (unsigned long long)result, (unsigned long long)rsize);
    return AR_NOP;
}

// Builtin: COPY (0x05)
static ArAction exec_copy(VM *vm) {
    ActivationRecord *ar = vm->current_ar;
    Register *proc = vm_get_register(vm, ar->proc_reg);
    BitCursor cursor = bc_create(proc->bits, ar->proc_pos);

    Range src = read_source(&cursor);
    Address dst = read_address(&cursor);
    ar->proc_pos = cursor.pos;

    copy_range(vm, src, dst, ar->proc_pos);
    printf("[COPY] %llu bits\n", (unsigned long long)src.size_bits);
    return AR_NOP;
}

// 0110: новая процедура, та же РС
static ArAction exec_call_new_proc(VM *vm, uint64_t next0) {
    ActivationRecord *ar = vm->current_ar;
    Register *proc = vm_get_register(vm, ar->proc_reg);
    BitCursor cursor = bc_create(proc->bits, ar->proc_pos);

    Address src = read_address(&cursor);
    ar->proc_pos = cursor.pos;
    ar->rs_ptr   = next0;   // caller продолжит отсюда (перезапишется, если shares_rs)

    ActivationRecord *n = calloc(1, sizeof(ActivationRecord));
    n->proc_reg    = src.reg_num;
    n->rs_reg      = ar->rs_reg;
    n->rs_ptr      = next0;   // callee стартует с того же узла
    n->proc_pos    = src.offset;
    n->shares_rs   = 1;
    n->shares_proc = 0;
    n->prev        = ar;
    vm->current_ar = n;

    printf("[CALL 0110] proc_reg=%d\n", src.reg_num);
    return AR_CALL;
}

// 0111: та же процедура, новая РС
static ArAction exec_call_new_rs(VM *vm, uint64_t next0) {
    ActivationRecord *ar = vm->current_ar;
    Register *proc = vm_get_register(vm, ar->proc_reg);
    BitCursor cursor = bc_create(proc->bits, ar->proc_pos);

    Address src = read_address(&cursor);
    uint64_t entry = read_by_address(vm, src, cursor.pos);
    ar->proc_pos = cursor.pos;
    ar->rs_ptr   = next0;

    ActivationRecord *n = calloc(1, sizeof(ActivationRecord));
    n->proc_reg    = ar->proc_reg;
    n->rs_reg      = ar->rs_reg;
    n->rs_ptr      = entry;
    n->proc_pos    = ar->proc_pos;   // продолжаем ту же процедуру
    n->shares_rs   = 0;
    n->shares_proc = 1;
    n->prev        = ar;
    vm->current_ar = n;

    printf("[CALL 0111] entry=%llu\n", (unsigned long long)entry);
    return AR_CALL;
}

// 1000: новая процедура + новая РС
static ArAction exec_call_new_both(VM *vm, uint64_t next0) {
    ActivationRecord *ar = vm->current_ar;
    Register *proc = vm_get_register(vm, ar->proc_reg);
    BitCursor cursor = bc_create(proc->bits, ar->proc_pos);

    Address src_proc = read_address(&cursor);
    Address src_rs   = read_address(&cursor);
    ar->proc_pos = cursor.pos;
    ar->rs_ptr   = next0;

    ActivationRecord *n = calloc(1, sizeof(ActivationRecord));
    n->proc_reg    = src_proc.reg_num;
    n->rs_reg      = src_rs.reg_num;
    n->rs_ptr      = src_rs.offset;
    n->proc_pos    = src_proc.offset;
    n->shares_rs   = 0;
    n->shares_proc = 0;
    n->prev        = ar;
    vm->current_ar = n;

    printf("[CALL 1000] proc=%d, rs=%d, entry=%llu\n",
           src_proc.reg_num, src_rs.reg_num,
           (unsigned long long)src_rs.offset);
    return AR_CALL;
}

// Builtin: RETURN (0x09)
static ArAction exec_return(VM *vm, uint64_t next0) {
    ActivationRecord *ar = vm->current_ar;
    Register *proc = vm_get_register(vm, ar->proc_reg);
    BitCursor cursor = bc_create(proc->bits, ar->proc_pos);

    Range src = read_source(&cursor);
    ar->proc_pos = cursor.pos;
    uint64_t val = read_range(vm, src, ar->proc_pos);
    printf("[RETURN] val=%llu\n", (unsigned long long)val);

    if (!ar->prev) { vm->halted = 1; return AR_RETURN; }

    ActivationRecord *caller = ar->prev;
    caller->has_result = 1;
    caller->result_value = val;

    if (ar->shares_rs)   caller->rs_ptr   = next0;
    if (ar->shares_proc) caller->proc_pos = ar->proc_pos;

    vm->current_ar = caller;
    return AR_RETURN;
}

// Builtin: RESIZE (0x0C)
static ArAction exec_resize(VM *vm) {
    ActivationRecord *ar = vm->current_ar;
    Register *proc = vm_get_register(vm, ar->proc_reg);
    BitCursor cursor = bc_create(proc->bits, ar->proc_pos);

    RegisterSelector target = read_register_selector(&cursor);
    Range size_source = read_source(&cursor);
    ar->proc_pos = cursor.pos;

    if (target.reg_class != REG_GLOBAL) {
        fprintf(stderr,
                "FATAL: RESIZE supports only global registers "
                "(class=%u, pos=%llu)\n",
                target.reg_class, (unsigned long long)ar->proc_pos);
        exit(1);
    }

    uint64_t new_size = read_range(vm, size_source, ar->proc_pos);
    size_t new_size_bits = (size_t)new_size;
    if ((uint64_t)new_size_bits != new_size) {
        fprintf(stderr,
                "FATAL: register size does not fit size_t "
                "(size=%llu, pos=%llu)\n",
                (unsigned long long)new_size,
                (unsigned long long)ar->proc_pos);
        exit(1);
    }

    vm_resize_register(vm, target.reg_num, new_size_bits);
    printf("[RESIZE] reg%u -> %llu bits\n",
           target.reg_num, (unsigned long long)new_size);
    return AR_NOP;
}

// Builtin: EXIT (0x0B)
static ArAction exec_exit(VM *vm) {
    printf("[EXIT] Halted.\n");
    vm->halted = 1;
    return AR_NOP;
}

static ArAction exec_builtin(VM *vm, uint16_t cmd, uint64_t next0) {
    switch (cmd) {
        case 0x04: return exec_compute(vm);
        case 0x05: return exec_copy(vm);
        case 0x06: return exec_call_new_proc(vm, next0);
        case 0x07: return exec_call_new_rs(vm, next0);
        case 0x08: return exec_call_new_both(vm, next0);
        case 0x09: return exec_return(vm, next0);
        case 0x0B: return exec_exit(vm);
        case 0x0C: return exec_resize(vm);
        default:
            fprintf(stderr, "FATAL: unknown builtin 0x%X\n", cmd);
            vm->halted = 1;
            return AR_NOP;
    }
}

void vm_start(VM *vm, int proc_reg, int rs_reg) {
    ActivationRecord *ar = calloc(1, sizeof(ActivationRecord));
    ar->proc_reg = proc_reg;
    ar->rs_reg = rs_reg;
    ar->rs_ptr = 0;
    ar->proc_pos = 0;
    ar->prev = NULL;
    vm->current_ar = ar;
    vm->halted = 0;
}

void vm_step(VM *vm) {
    if (vm->halted) return;
    ActivationRecord *ar = vm->current_ar;
    if (!ar) { vm->halted = 1; return; }

    Register *rs = vm_get_register(vm, ar->rs_reg);
    Node node = read_node(rs->bits, ar->rs_ptr);

    if (node.type != 0) {
        fprintf(stderr, "FATAL: node type %d not implemented\n", node.type);
        vm->halted = 1;
        return;
    }

    // CHOICE — обрабатываем отдельно (не в exec_builtin)
    if (node.data == 0x03) {
        Register *proc = vm_get_register(vm, ar->proc_reg);
        BitCursor cursor = bc_create(proc->bits, ar->proc_pos);
        uint64_t bit = bc_read_bits(&cursor, 1);
        ar->proc_pos = cursor.pos;
        ar->rs_ptr = bit ? node.next1 : node.next0;
        return;
    }

    ActivationRecord *old_ar = ar;
    ArAction action = exec_builtin(vm, node.data, node.next0);
    if (vm->halted) return;

    switch (action) {
        case AR_NOP:
            // COMPUTE / EXIT: просто двигаем узел
            old_ar->rs_ptr = node.next0;
            break;
        case AR_CALL:
            // CALL: caller->rs_ptr и callee уже настроены внутри
            break;
        case AR_RETURN:
            // RETURN: caller уже переключён внутри
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
