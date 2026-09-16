#include "vm.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <procedure> <rs>\n", argv[0]);
        return 1;
    }
    
    VM *vm = vm_create();

	vm_load_procedure(vm, 1, "program.ubc");   // main
	vm_load_rs(vm, 2, "rs1.bin");              // РС для main

	vm_load_procedure(vm, 5, "subproc.ubc");   // subproc
	vm_load_rs(vm, 3, "rs2.bin");              // РС для subproc

	vm_set_uint64(vm, 10, 5);
	vm_set_uint64(vm, 11, 7);
	vm_create_register(vm, 12, 64);

	vm_start(vm, 1, 2);
	vm_run(vm); 
    printf("Result r12 = %llu\n", (unsigned long long)vm_get_uint64(vm, 12));
    
    vm_free(vm);
    return 0;
}
