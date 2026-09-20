#include "vm.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <procedure> <rs>\n", argv[0]);
        return 1;
    }

    VM *vm = vm_create();

    vm_load_procedure(vm, 1, argv[1]);
    vm_load_rs(vm, 2, argv[2]);

    vm_start(vm, 1, 2);
    vm_run(vm);

    vm_free(vm);
    return 0;
}
