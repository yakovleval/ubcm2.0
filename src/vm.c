#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

VM *vm_create(void) { return calloc(1, sizeof(VM)); }

void vm_free(VM *vm) {
    for (int i = 0; i < MAX_REGISTERS; i++) {
        if (vm->registers[i]) {
            bs_free(vm->registers[i]->data);
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

int vm_create_register(VM *vm, int num, size_t size_bits) {
    if (num < 0 || num >= MAX_REGISTERS) return -1;
    if (vm->registers[num]) {
        bs_free(vm->registers[num]->data);
        free(vm->registers[num]);
    }
    Register *reg = malloc(sizeof(Register));
    reg->data = bs_create(size_bits);
    vm->registers[num] = reg;
    return 0;
}

int vm_delete_register(VM *vm, int num) {
    if (num < 0 || num >= MAX_REGISTERS) return -1;
    if (vm->registers[num]) {
        bs_free(vm->registers[num]->data);
        free(vm->registers[num]);
        vm->registers[num] = NULL;
    }
    return 0;
}

Register *vm_get_register(VM *vm, int num) {
    if (num < 0 || num >= MAX_REGISTERS) return NULL;
    return vm->registers[num];
}

static int load_file_into_register(VM *vm, int reg_num, const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    vm_create_register(vm, reg_num, size * 8);
    Register *reg = vm_get_register(vm, reg_num);
    fread(reg->data->data, 1, size, f);
    fclose(f);
    return 0;
}

int vm_load_procedure(VM *vm, int reg_num, const char *filename) {
    return load_file_into_register(vm, reg_num, filename);
}

int vm_load_rs(VM *vm, int reg_num, const char *filename) {
    return load_file_into_register(vm, reg_num, filename);
}

void vm_set_uint64(VM *vm, int reg_num, uint64_t value) {
    Register *reg = vm_get_register(vm, reg_num);
    if (!reg) { vm_create_register(vm, reg_num, 64); reg = vm_get_register(vm, reg_num); }
    bs_seek(reg->data, 0);
    bs_write_bits(reg->data, value, 64);
}

uint64_t vm_get_uint64(VM *vm, int reg_num) {
    Register *reg = vm_get_register(vm, reg_num);
    if (!reg) return 0;
    bs_seek(reg->data, 0);
    return bs_read_bits(reg->data, 64);
}

// Чтение узла из РС по адресу
static Node read_node(BitStream *bs, uint64_t addr) {
    size_t saved = bs->pos;
    bs_seek(bs, addr * 64);
    Node n;
    n.type     = bs_read_bits(bs, 1);
    n.data     = bs_read_bits(bs, 15);
    n.next0    = bs_read_bits(bs, 16);
    n.next1    = bs_read_bits(bs, 16);
    n.resolver = bs_read_bits(bs, 16);
    bs_seek(bs, saved);
    return n;
}

// Чтение источника (все режимы разрешены)
static int read_operand(BitStream *bs) {
    uint64_t mode = bs_read_bits(bs, 2);
    if (mode == 2) { // direct
        return (int)bs_read_bits(bs, 5);
    }
    return -1;
}

// Чтение приёмника (непосредственная адресация запрещена)
static int read_dst(BitStream *bs) {
    size_t mode_pos = bs->pos;
    uint64_t mode = bs_read_bits(bs, 2);
    if (mode == 0) {
        fprintf(stderr,
            "FATAL: immediate addressing not allowed for destination "
            "(bit position %zu)\n", mode_pos);
        exit(1);
    }
    if (mode == 2) { // direct
        return (int)bs_read_bits(bs, 5);
    }
    return -1;
}

// Builtin: COMPUTE (0x04)
static void exec_compute(VM *vm) {
    ActivationRecord *ar = vm->current_ar;
    Register *proc = vm_get_register(vm, ar->proc_reg);
    BitStream *bs = proc->data;

    uint64_t opcode = bs_read_bits(bs, 5);
    int src1 = read_operand(bs);
    int src2 = read_operand(bs);
    int dst  = read_dst(bs);   // ← используем read_dst

    uint64_t v1 = vm_get_uint64(vm, src1);
    uint64_t v2 = vm_get_uint64(vm, src2);
    uint64_t result = 0;

    switch (opcode) {
        case 0: result = v1 + v2; break;
        case 1: result = v1 - v2; break;
        case 2: result = v1 * v2; break;
        case 3: result = v2 ? v1 / v2 : 0; break;
        default: result = 0;
    }

    vm_set_uint64(vm, dst, result);
    printf("[COMPUTE] op=%llu r%d = r%d + r%d = %llu\n",
           (unsigned long long)opcode, dst, src1, src2,
           (unsigned long long)result);
}

// Builtin: RETURN (0x09)
static void exec_return(VM *vm) {
    ActivationRecord *ar = vm->current_ar;
    Register *proc = vm_get_register(vm, ar->proc_reg);
    int src = read_operand(proc->data);
    uint64_t val = vm_get_uint64(vm, src);
    printf("[RETURN] r%d = %llu\n", src, (unsigned long long)val);
    
    if (ar->prev) {
        vm->current_ar = ar->prev;
        free(ar);
    } else {
        vm->halted = 1;
    }
}

// Builtin: EXIT (0x0B)
static void exec_exit(VM *vm) {
    printf("[EXIT] Halted.\n");
    vm->halted = 1;
}

static void exec_builtin(VM *vm, uint16_t cmd) {
    switch (cmd) {
        case 0x00: break;          // NOP
        case 0x04: exec_compute(vm); break;
        case 0x09: exec_return(vm);  break;
        case 0x0B: exec_exit(vm);    break;
        default:
            fprintf(stderr, "[FATAL] Unknown builtin: 0x%X\n", cmd);
            vm->halted = 1;
    }
}

void vm_start(VM *vm, int proc_reg, int rs_reg) {
    ActivationRecord *ar = calloc(1, sizeof(ActivationRecord));
    ar->proc_reg = proc_reg;
    ar->rs_reg = rs_reg;
    ar->rs_ptr = 0;
    ar->prev = NULL;
    vm->current_ar = ar;
    vm->halted = 0;
}

void vm_step(VM *vm) {
    if (vm->halted) return;
    ActivationRecord *ar = vm->current_ar;
    if (!ar) { vm->halted = 1; return; }
    
    Register *rs = vm_get_register(vm, ar->rs_reg);
    Node node = read_node(rs->data, ar->rs_ptr);
    
    if (node.type == 0) {
        if (node.data == 0x03) {
            // CHOICE: читаем 1 бит из процедуры
            Register *proc = vm_get_register(vm, ar->proc_reg);
            uint64_t bit = bs_read_bits(proc->data, 1);
            ar->rs_ptr = bit ? node.next1 : node.next0;
        } else {
            exec_builtin(vm, node.data);
            if (!vm->halted) ar->rs_ptr = node.next0;
        }
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
