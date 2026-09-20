CC = gcc
SRC = src/main.c src/vm.c src/bitstream.c src/encoding.c
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

all: debug

debug: $(DEBUG_DIR)/$(TARGET)

release: $(RELEASE_DIR)/$(TARGET)

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

clean:
	rm -rf $(DEBUG_DIR) $(RELEASE_DIR)
	rm -f *.ubc
	rm -f *.rs

.PHONY: all debug release clean
