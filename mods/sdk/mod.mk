# Common makefile for Sands of Time: Recompiled mods, included by each mod's Makefile.
#
# Needs clang and ld.lld (with the MIPS target), RecompModTool (built from lib/N64ModernRuntime/N64Recomp) and the
# game's symbol files (sot.syms.toml and sot.datasyms.toml at the root of the repository, generated from the ROM by
# tools/generate.py). `make` builds build/<mod_filename>.nrm.

SDK_DIR     := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
ROOT        := $(SDK_DIR)/../..
DECOMP_PATH ?= $(ROOT)/lib/oot-decomp
MOD_TOOL    ?= $(ROOT)/lib/N64ModernRuntime/N64Recomp/build/RecompModTool

ifeq ($(origin CC),default)
CC := clang
endif
ifeq ($(origin LD),default)
LD := ld.lld
endif

BUILD_DIR := build
TARGET    := $(BUILD_DIR)/mod.elf

CFLAGS   := -target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -mno-odd-spreg -mno-check-zero-division \
            -fomit-frame-pointer -ffast-math -fno-unsafe-math-optimizations -fno-builtin-memset -ffunction-sections \
            -Wall -Wextra -Wno-incompatible-library-redeclaration -Wno-unused-parameter -Wno-unknown-pragmas \
            -Wno-unused-variable -Wno-missing-braces -Wno-unsupported-floating-point-opt -Werror=section \
            -Werror=implicit-function-declaration
CPPFLAGS := -nostdinc -D_LANGUAGE_C -DMIPS -DOOT_DEBUG=1 -I $(SDK_DIR) -I $(ROOT)/patches/dummy_headers -I $(DECOMP_PATH)/include \
            -I $(DECOMP_PATH)/include/libc -I $(DECOMP_PATH)/src -I $(DECOMP_PATH)/assets -I src
LDFLAGS  := -nostdlib -T $(SDK_DIR)/mod.ld -Map $(BUILD_DIR)/mod.map --unresolved-symbols=ignore-all --emit-relocs -e 0 \
            --no-nmagic -gc-sections

C_SRCS := $(wildcard src/*.c)
C_OBJS := $(addprefix $(BUILD_DIR)/, $(C_SRCS:.c=.o))
C_DEPS := $(C_OBJS:.o=.d)

all: $(TARGET)
	$(MOD_TOOL) mod.toml $(BUILD_DIR)

$(TARGET): $(C_OBJS) $(SDK_DIR)/mod.ld
	$(LD) $(C_OBJS) $(LDFLAGS) -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -MMD -MF $(@:.o=.d) -c -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(C_DEPS)

.PHONY: all clean
