#include "vm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *case_name, const char *message) {
    fprintf(stderr, "TEST FAILED [%s]: %s\n", case_name, message);
    exit(1);
}

static void check_resize(VM *vm, const char *case_name) {
    if (vm_get_register(vm, 20) != NULL)
        fail(case_name, "register 20 was not deleted");

    Register *reg21 = vm_get_register(vm, 21);
    if (!reg21)
        fail(case_name, "register 21 was not created");
    if (reg21->bits->size_bits != 9)
        fail(case_name, "register 21 has an unexpected bit size");
    for (size_t index = 0; index < reg21->bits->size; index++) {
        if (reg21->bits->data[index] != 0)
            fail(case_name, "new register bits are not zero-initialized");
    }
}

static void check_copy(VM *vm, const char *case_name) {
    Register *reg = vm_get_register(vm, 20);
    if (!reg)
        fail(case_name, "destination register 20 was not created");
    if (reg->bits->size_bits != 20)
        fail(case_name, "destination register has an unexpected bit size");

    BitCursor cursor = bc_create(reg->bits, 0);
    if (bc_read_bits(&cursor, 20) != UINT64_C(0xB5696))
        fail(case_name, "COPY produced unexpected destination bits");
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr,
                "TEST FAILED: usage: test_integration <case> <procedure> <rs>\n");
        return 1;
    }

    const char *case_name = argv[1];
    VM *vm = vm_create();
    vm_load_procedure(vm, 1, argv[2]);
    vm_load_rs(vm, 2, argv[3]);
    vm_start(vm, 1, 2);

    uint64_t results[2] = {0};
    size_t result_count = 0;
    size_t steps = 0;

    while (!vm->halted && steps < 1000) {
        vm_step(vm);
        steps++;

        ActivationRecord *current = vm->current_ar;
        if (current && current->has_result) {
            if (result_count >= sizeof(results) / sizeof(results[0]))
                fail(case_name, "received too many CALL results");
            results[result_count] = current->result_value;
            result_count++;
            current->has_result = 0;
        }
    }

    if (!vm->halted)
        fail(case_name, "VM did not halt");

    if (strcmp(case_name, "resize_register_1100") == 0) {
        if (result_count != 0)
            fail(case_name, "RESIZE unexpectedly returned a CALL result");
        check_resize(vm, case_name);
    } else if (strcmp(case_name, "copy_value_0101") == 0) {
        if (result_count != 0)
            fail(case_name, "COPY unexpectedly returned a CALL result");
        check_copy(vm, case_name);
    } else if (strcmp(case_name, "call_new_procedure_and_network_1000") == 0) {
        if (result_count != 2)
            fail(case_name, "CALL 1000 did not produce exactly two results");
        if (results[0] != 17 || results[1] != 23)
            fail(case_name, "the two networks returned unexpected values");
    } else {
        uint64_t expected = 0;
        if (strcmp(case_name, "call_new_procedure_0110") == 0)
            expected = 11;
        else if (strcmp(case_name, "call_new_network_0111") == 0)
            expected = 17;
        else
            fail(case_name, "unknown test case");

        if (result_count != 1)
            fail(case_name, "CALL did not return exactly one result");
        if (results[0] != expected)
            fail(case_name, "CALL returned an unexpected value");
    }

    vm_free(vm);
    printf("Integration test passed: %s\n", case_name);
    return 0;
}
