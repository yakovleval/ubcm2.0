#ifndef VM_H
#define VM_H

#include "bitstream.h"
#include <stdint.h>

#define MAX_REGISTERS 256

typedef struct {
    BitStream *data;
} Register;

typedef enum {
    AR_NOP,      // AR не меняется (COMPUTE, EXIT)
    AR_CALL,     // создана новая AR (0110, 0111, 1000)
    AR_RETURN    // AR снята (1001)
} ArAction;

typedef struct ActivationRecord {
    int proc_reg;              // регистр с процедурой
    int rs_reg;                // регистр с РС
    uint64_t rs_ptr;           // текущий узел в РС
    uint64_t proc_pos;         // позиция в процедуре (в битах)
    int shares_rs;    // 0110: делим rs_ptr с caller
    int shares_proc;  // 0111: делим proc_pos с caller
    struct ActivationRecord *prev;
} ActivationRecord;

typedef struct {
    Register *registers[MAX_REGISTERS];
    ActivationRecord *current_ar;
    int halted;
} VM;

typedef struct {
    uint8_t  type;    // 0 = builtin, 1 = procedure call
    uint16_t data;    // command code
    uint16_t next0;
    uint16_t next1;
    uint16_t resolver;
} Node;

VM  *vm_create(void);
void vm_free(VM *vm);

int  vm_create_register(VM *vm, int num, size_t size_bits);
int  vm_delete_register(VM *vm, int num);
Register *vm_get_register(VM *vm, int num);

int  vm_load_procedure(VM *vm, int reg_num, const char *filename);
int  vm_load_rs(VM *vm, int reg_num, const char *filename);

// Запуск
void vm_start(VM *vm, int proc_reg, int rs_reg);
void vm_step(VM *vm);
void vm_run(VM *vm);

// Утилиты
void     vm_set_uint64(VM *vm, int reg_num, uint64_t value);
uint64_t vm_get_uint64(VM *vm, int reg_num);

#endif
