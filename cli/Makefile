# To generate compile_commands.json for clangd: make clean && bear -- make
CC      := clang
CFLAGS  := -O3 -march=native -Wall -Wextra -Wpedantic
INCLUDE := -I..
TARGET  := rapidhash
SRC     := rapidhash.c
BUILDDIR := build

.PHONY: all clean install debug

all: $(BUILDDIR)/$(TARGET)

$(BUILDDIR)/$(TARGET): $(SRC) ../rapidhash.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $(SRC)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

debug: CFLAGS := -g -O0 -fsanitize=address,undefined -Wall -Wextra
debug: $(BUILDDIR)/$(TARGET)

clean:
	rm -rf $(BUILDDIR)

INSTALL_DIR := $(HOME)/.local/bin

install: $(BUILDDIR)/$(TARGET)
	mkdir -p $(INSTALL_DIR)
	install -m 755 $< $(INSTALL_DIR)/$(TARGET)
