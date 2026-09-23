CC = gcc
SRC = src/main.c src/vm.c src/bitstream.c src/encoding.c src/registers.c \
	src/references.c src/resolving_network.c
TARGET = ubcm

COMMON_CFLAGS = -Wall -Wextra -Werror -std=c11
DEBUG_CFLAGS = $(COMMON_CFLAGS) -O0 -g3 -fno-omit-frame-pointer \
	-fsanitize=address,undefined
DEBUG_LDFLAGS = -fsanitize=address,undefined
RELEASE_CFLAGS = $(COMMON_CFLAGS) -DNDEBUG

DEBUG_DIR = debug
RELEASE_DIR = release
DEBUG_OBJ = $(patsubst src/%.c,$(DEBUG_DIR)/%.o,$(SRC))
RELEASE_OBJ = $(patsubst src/%.c,$(RELEASE_DIR)/%.o,$(SRC))
TEST_CASES = call_new_procedure_0110 call_new_network_0111 \
	call_new_procedure_and_network_1000 resize_register_1100 \
	get_register_size_1101 jump_to_position_1010 return_result_1001 \
	copy_value_0101 indirect_addressing_01 procedure_register_class_00 \
	foreign_addressing_11 \
	accumulated_prefixes_compute_0000_0001 superlocal_registers \
	write_activation_record_0001 conditional_execution_0010 \
	procedure_node_type_1
TEST_FIXTURES = $(foreach case,$(TEST_CASES),\
	tests/$(case).ubc tests/$(case).rn)
TEST_OBJ = $(DEBUG_DIR)/test_integration.o
TEST_TARGET = $(DEBUG_DIR)/test_integration

all: debug

debug: $(DEBUG_DIR)/$(TARGET)

release: $(RELEASE_DIR)/$(TARGET)

fixtures: $(TEST_FIXTURES)

test: debug fixtures $(TEST_TARGET)
	@for case in $(TEST_CASES); do \
		./$(TEST_TARGET) $$case \
			tests/$$case.ubc tests/$$case.rn || exit 1; \
	done

$(DEBUG_DIR)/$(TARGET): $(DEBUG_OBJ)
	$(CC) $(DEBUG_LDFLAGS) -o $@ $^

$(RELEASE_DIR)/$(TARGET): $(RELEASE_OBJ)
	$(CC) -o $@ $^

$(DEBUG_DIR)/%.o: src/%.c
	@mkdir -p $(DEBUG_DIR)
	$(CC) $(DEBUG_CFLAGS) -c $< -o $@

$(RELEASE_DIR)/%.o: src/%.c
	@mkdir -p $(RELEASE_DIR)
	$(CC) $(RELEASE_CFLAGS) -c $< -o $@

$(TEST_FIXTURES): tests/generate_integration.py
	python3 $<

$(TEST_OBJ): tests/test_integration.c
	@mkdir -p $(DEBUG_DIR)
	$(CC) $(DEBUG_CFLAGS) -Isrc -c $< -o $@

$(TEST_TARGET): $(TEST_OBJ) $(filter-out $(DEBUG_DIR)/main.o,$(DEBUG_OBJ))
	$(CC) $(DEBUG_LDFLAGS) -o $@ $^

clean:
	rm -rf $(DEBUG_DIR) $(RELEASE_DIR)
	rm -f tests/*.ubc tests/*.rn
	rm -f *.ubc
	rm -f *.rs *.rn

.PHONY: all debug release fixtures test clean
