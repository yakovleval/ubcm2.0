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

static void check_get_size(VM *vm, const char *case_name) {
    Register *reg = vm_get_register(vm, 21);
    if (!reg)
        fail(case_name, "destination register 21 was not created");
    if (reg->bits->size_bits != 128)
        fail(case_name, "destination register has an unexpected bit size");

    BitCursor cursor = bc_create(reg->bits, 0);
    if (bc_read_bits(&cursor, 64) != 13)
        fail(case_name, "GET SIZE returned the wrong existing-register size");
    if (bc_read_bits(&cursor, 64) != 0)
        fail(case_name, "GET SIZE returned a nonzero missing-register size");
}

static void check_integer_compute(VM *vm, const char *case_name) {
    static const uint64_t arithmetic[] = {
        16, 10, 39, 4, 1, 149, 1, 15,
    };
    static const uint64_t boolean[] = {
        0, 1, 1, 1, 0, 0, 1, 1,
    };
    Register *reg = vm_get_register(vm, 20);
    if (!reg || reg->bits->size_bits != 81)
        fail(case_name, "COMPUTE result register is invalid");

    BitCursor cursor = bc_create(reg->bits, 0);
    for (size_t index = 0;
         index < sizeof(arithmetic) / sizeof(arithmetic[0]); index++) {
        if (bc_read_bits(&cursor, 8) != arithmetic[index])
            fail(case_name, "arithmetic COMPUTE result is invalid");
    }
    for (size_t index = 0;
         index < sizeof(boolean) / sizeof(boolean[0]); index++) {
        if (bc_read_bits(&cursor, 1) != boolean[index])
            fail(case_name, "boolean COMPUTE result is invalid");
    }
    if (bc_read_bits(&cursor, 1) != 1)
        fail(case_name, "logical NOT result is invalid");
    if (bc_read_bits(&cursor, 8) != 242)
        fail(case_name, "bitwise NOT result is invalid");
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
    } else if (strcmp(case_name, "get_register_size_1101") == 0) {
        if (result_count != 0)
            fail(case_name, "GET SIZE unexpectedly returned a CALL result");
        check_get_size(vm, case_name);
    } else if (strcmp(case_name, "jump_to_position_1010") == 0) {
        if (result_count != 0)
            fail(case_name, "JUMP unexpectedly returned a CALL result");
        if (vm_get_register(vm, 20) != NULL)
            fail(case_name, "JUMP did not skip the RESIZE command");
    } else if (strcmp(case_name, "return_result_1001") == 0) {
        if (result_count != 0)
            fail(case_name, "root result appeared as a CALL result");
        if (!vm->has_result || vm->result_value != 42)
            fail(case_name, "root procedure returned an unexpected result");
    } else if (strcmp(case_name, "copy_value_0101") == 0) {
        if (result_count != 0)
            fail(case_name, "COPY unexpectedly returned a CALL result");
        check_copy(vm, case_name);
    } else if (strcmp(case_name, "integer_compute_0100") == 0) {
        if (result_count != 0)
            fail(case_name, "COMPUTE unexpectedly returned a result");
        check_integer_compute(vm, case_name);
    } else if (strcmp(case_name, "indirect_addressing_01") == 0) {
        if (result_count != 0)
            fail(case_name, "indirect COPY unexpectedly returned a result");
        Register *reg22 = vm_get_register(vm, 22);
        if (!reg22 || reg22->bits->size_bits != 26)
            fail(case_name, "indirect COPY destination is missing");
        BitCursor cursor = bc_create(reg22->bits, 0);
        if (bc_read_bits(&cursor, 13) != UINT64_C(0x1696))
            fail(case_name, "indirect source returned unexpected bits");
        if (bc_read_bits(&cursor, 13) != UINT64_C(0x1969))
            fail(case_name, "indirect destination contains unexpected bits");
    } else if (strcmp(case_name, "foreign_addressing_11") == 0) {
        if (result_count != 0)
            fail(case_name, "foreign COPY unexpectedly returned a result");
        Register *reg20 = vm_get_register(vm, 20);
        if (!reg20 || reg20->bits->size_bits != 3)
            fail(case_name, "foreign COPY result register is missing");
        BitCursor cursor = bc_create(reg20->bits, 0);
        if (bc_read_bits(&cursor, 3) != 5)
            fail(case_name, "foreign COPY returned unexpected bits");
    } else if (strcmp(case_name, "procedure_register_class_00") == 0) {
        if (result_count != 0)
            fail(case_name, "procedure-register COPY returned a result");
        Register *reg20 = vm_get_register(vm, 20);
        if (!reg20 || reg20->bits->size_bits != 13)
            fail(case_name, "procedure-register destination is missing");
        BitCursor cursor = bc_create(reg20->bits, 0);
        if (bc_read_bits(&cursor, 13) != UINT64_C(0x1696))
            fail(case_name, "procedure-register source returned wrong bits");
    } else if (strcmp(case_name,
                      "accumulated_prefixes_compute_0000_0001") == 0) {
        if (result_count != 0)
            fail(case_name, "COMPUTE unexpectedly returned a result");
        Register *reg20 = vm_get_register(vm, 20);
        if (!reg20)
            fail(case_name, "COMPUTE destination register is missing");
        BitCursor cursor = bc_create(reg20->bits, 0);
        if (bc_read_bits(&cursor, 3) != 7)
            fail(case_name, "prefixed COMPUTE returned an unexpected value");
    } else if (strcmp(case_name, "superlocal_registers") == 0) {
        if (result_count != 0)
            fail(case_name, "superlocal commands returned a result");
        Register *reg20 = vm_get_register(vm, 20);
        if (!reg20 || reg20->bits->size_bits != 64)
            fail(case_name, "superlocal result register is missing");
        BitCursor cursor = bc_create(reg20->bits, 0);
        if (bc_read_bits(&cursor, 64) != 0)
            fail(case_name, "different nodes shared superlocal storage");
        size_t storage_count = 0;
        for (SuperlocalStorage *storage = vm->superlocal_storages;
             storage; storage = storage->next)
            storage_count++;
        if (storage_count != 2)
            fail(case_name,
                 "superlocal storages are not owned by nodes");
    } else if (strcmp(case_name, "write_activation_record_0001") == 0) {
        if (result_count != 0)
            fail(case_name, "write prefix unexpectedly returned a result");
        if (vm_get_register(vm, 20) != NULL)
            fail(case_name, "write prefix did not redirect JUMP to caller");
    } else if (strcmp(case_name, "conditional_execution_0010") == 0) {
        if (result_count != 0)
            fail(case_name, "conditional prefix unexpectedly returned a result");
        if (vm_get_register(vm, 21) != NULL)
            fail(case_name, "false conditional prefix executed command");
        Register *reg22 = vm_get_register(vm, 22);
        if (!reg22 || reg22->bits->size_bits != 9)
            fail(case_name, "true conditional prefix skipped command");
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
        else if (strcmp(case_name, "procedure_node_type_1") == 0)
            expected = 37;
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
