#ifndef VM_H
#define VM_H

#include "bitstream.h"
#include "resolving_network.h"
#include <stdint.h>

#define MAX_REGISTERS 256
#define DEFAULT_LOCAL_RESOLVER_NODE 128

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
    ResolvingNetworkEntry current_node;
    ResolvingNetworkEntry local_resolver;
    Register *local_registers[MAX_REGISTERS];
    uint64_t proc_pos;         // позиция в процедуре (в битах)
    int shares_rs;    // 0110: делим rs_ptr с caller
    int shares_proc;  // 0111: делим proc_pos с caller
    struct ActivationRecord *prev;
} ActivationRecord;

typedef struct SuperlocalStorage {
    ResolvingNetworkEntry owner_node;
    Register *registers[MAX_REGISTERS];
    struct SuperlocalStorage *next;
} SuperlocalStorage;

typedef struct {
    Register *registers[MAX_REGISTERS];
    SuperlocalStorage *superlocal_storages;
    ActivationRecord *current_ar;
    ActivationRecord *prefix_read_ar;
    ActivationRecord *prefix_write_ar;
    int prefix_condition;
    int has_prefix_condition;
    uint64_t result_value;
    int has_result;
    int halted;
} VM;

typedef struct {
    VM *vm;
    ActivationRecord *execution_ar;
    ActivationRecord *read_ar;
    ActivationRecord *write_ar;
    ResolvingNetworkNode node;
    ResolvingNetworkEntry node_entry;
    BitCursor operands;
} CommandContext;

VM  *vm_create(void);
void vm_free(VM *vm);

void vm_create_register(VM *vm, int num, size_t size_bits);
void vm_delete_register(VM *vm, int num);
void vm_resize_register(VM *vm, int num, size_t size_bits);
void vm_resize_register_slot(Register **slot, size_t size_bits);
void vm_free_register_slot(Register **slot);
Register *vm_get_register(VM *vm, int num);
ResolvingNetworkNode vm_read_resolving_network_node(
    VM *vm, ResolvingNetworkEntry entry, size_t stream_pos);

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
