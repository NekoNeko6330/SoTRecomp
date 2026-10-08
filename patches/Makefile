# OoT decomp (headers only), see lib/oot-decomp
DECOMP_PATH ?= ../lib/oot-decomp

TARGET := patches.elf

# clang and ld.lld with the MIPS target (can be set in the environment or on the command line)
ifeq ($(origin CC),default)
CC      := clang
endif
ifeq ($(origin LD),default)
LD      := ld.lld
endif

CFLAGS   := -target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -mno-odd-spreg -mno-check-zero-division \
			-fomit-frame-pointer -ffast-math -fno-unsafe-math-optimizations -fno-builtin-memset \
			-Wall -Wextra -Wno-incompatible-library-redeclaration -Wno-unused-parameter -Wno-unknown-pragmas -Wno-unused-variable -Wno-missing-braces -Wno-unsupported-floating-point-opt
CPPFLAGS := -nostdinc -D_LANGUAGE_C -DMIPS -I dummy_headers -I $(DECOMP_PATH)/include -I $(DECOMP_PATH)/include/libc -I $(DECOMP_PATH)/src -I $(DECOMP_PATH)/assets -I../lib/rt64/include
LDFLAGS  := -nostdlib -T patches.ld -T syms.ld -Map patches.map --unresolved-symbols=ignore-all --emit-relocs

C_SRCS := $(wildcard *.c)
C_OBJS := $(C_SRCS:.c=.o)
C_DEPS := $(C_SRCS:.c=.d)

$(TARGET): $(C_OBJS) patches.ld syms.ld
	$(LD) $(C_OBJS) $(LDFLAGS) -o $@

$(C_OBJS): %.o : %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -MMD -MF $(@:.o=.d) -c -o $@

clean:
	rm -rf $(C_OBJS) $(TARGET) $(C_DEPS)

-include $(C_DEPS)

.PHONY: clean
