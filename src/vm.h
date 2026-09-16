#ifndef VM_H
#define VM_H

#include "bitstream.h"
#include <stdint.h>

#define MAX_REGISTERS 256

typedef struct {
    BitStream *data;   // содержимое регистра
} Register;

typedef struct {
    Register *registers[MAX_REGISTERS];
    int       count;
} VM;

VM  *vm_create(void);
void vm_free(VM *vm);

int  vm_create_register(VM *vm, int num, size_t size_bits);
int  vm_delete_register(VM *vm, int num);
Register *vm_get_register(VM *vm, int num);

int  vm_load_procedure(VM *vm, int reg_num, const char *filename);
int  vm_load_rs(VM *vm, int reg_num, const char *filename);

#endif
