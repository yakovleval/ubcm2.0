#ifndef VM_H
#define VM_H

#include "bitstream.h"
#include "resolving_network.h"
#include <stdint.h>

#define MAX_REGISTERS 256

typedef struct {
    BitVector *bits;
} Register;

typedef enum {
    AR_NOP,      // AR не меняется
    AR_CALL,     // создана новая AR (0110, 0111, 1000)
    AR_POP       // AR снята (1011)
} ArAction;

typedef struct ActivationRecord {
    uint64_t result_value;
    int has_result;
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
    uint64_t result_value;
    int has_result;
    int halted;
} VM;

typedef struct {
    VM *vm;
    ActivationRecord *read_ar;
    ActivationRecord *write_ar;
    ResolvingNetworkNode node;
    BitCursor operands;
} CommandContext;

VM  *vm_create(void);
void vm_free(VM *vm);

void vm_create_register(VM *vm, int num, size_t size_bits);
void vm_delete_register(VM *vm, int num);
void vm_resize_register(VM *vm, int num, size_t size_bits);
Register *vm_get_register(VM *vm, int num);

void vm_load_procedure(VM *vm, int reg_num, const char *filename);
void vm_load_rs(VM *vm, int reg_num, const char *filename);

// Запуск
void vm_start(VM *vm, int proc_reg, int rs_reg);
void vm_step(VM *vm);
void vm_run(VM *vm);

// Утилиты
void     vm_set_uint64(VM *vm, int reg_num, uint64_t value);
uint64_t vm_get_uint64(VM *vm, int reg_num);

#endif
