#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

VM *vm_create(void) {
    VM *vm = calloc(1, sizeof(VM));
    return vm;
}

void vm_free(VM *vm) {
    for (int i = 0; i < MAX_REGISTERS; i++) {
        if (vm->registers[i]) {
            bs_free(vm->registers[i]->data);
            free(vm->registers[i]);
        }
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

// Загрузка файла в регистр (как сырые биты)
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
