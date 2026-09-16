CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g
SRC = src/main.c src/vm.c src/bitstream.c src/encoding.c
TARGET = ubcm

BUILD_DIR = build
OBJ = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))

all: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
