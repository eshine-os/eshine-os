# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
CROSS   ?= aarch64-linux-gnu-
CC      := $(CROSS)gcc

# VERSION 文件 = 唯一事实源(docs/VERSIONING.md),注入到 version.h
VERSION := $(shell cat VERSION 2>/dev/null || echo unknown)

CFLAGS  := -ffreestanding -fno-builtin -fno-pie -fno-pic \
           -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables \
           -mgeneral-regs-only -march=armv8-a -std=gnu11 -O2 -g \
           -DESHINE_VERSION_STR=\"$(VERSION)\" \
           -MMD -MP \
           -Wall -Wextra -Ihypervisor
ASFLAGS := -mgeneral-regs-only -march=armv8-a
LDFLAGS := -nostdlib -Wl,--build-id=none -T linker.ld

CSRCS := $(wildcard hypervisor/kernel/*.c) $(wildcard hypervisor/drivers/*.c) \
         $(wildcard hypervisor/arch/arm64/mm/*.c)
ASRCS := $(wildcard hypervisor/arch/arm64/*.S)
OBJS  := $(CSRCS:.c=.o) $(ASRCS:.S=.o)

TARGET := eshine.elf

# guest:独立构建,链接 IPA 0x40000000(见 guest/linker.ld)
GOBJS := guest/start.o guest/main.o

all: $(TARGET) guest.bin

$(TARGET): $(OBJS) linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.S
	$(CC) $(ASFLAGS) -c -o $@ $<

# VERSION 是注入宏的来源,必须作为先决条件(否则改版本不触发重编,门禁会抓)
$(OBJS): VERSION

run: $(TARGET)
	./scripts/run_qemu.sh

verify: $(TARGET) guest.bin
	python3 tools/verify/verify_el2.py

guest.elf: $(GOBJS) guest/linker.ld
	$(CC) $(CFLAGS) -nostdlib -Wl,--build-id=none -T guest/linker.ld -o $@ $(GOBJS)

guest.bin: guest.elf
	$(CROSS)objcopy -O binary $< $@

clean:
	rm -f $(OBJS) $(TARGET) $(OBJS:.o=.d) $(GOBJS) guest/*.d guest.elf guest.bin

.PHONY: all run verify clean

# 头文件自动依赖(-MMD -MP),头文件改动触发重编
-include $(OBJS:.o=.d) $(GOBJS:.o=.d)
