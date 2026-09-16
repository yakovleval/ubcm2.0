#include "vm.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <procedure> <rs>\n", argv[0]);
        return 1;
    }
    
    VM *vm = vm_create();
    
    // Регистр №1 — процедура
    vm_load_procedure(vm, 1, argv[1]);
    
    // Регистр №2 — РС
    vm_load_rs(vm, 2, argv[2]);
    
    printf("VM initialized.\n");
    printf("Procedure: %s (register 1)\n", argv[1]);
    printf("RS:        %s (register 2)\n", argv[2]);
    
    vm_free(vm);
    return 0;
}
