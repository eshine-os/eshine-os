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

# guest:独立构建,同源双身份(-DGUEST_ID=0/1),链接 IPA 0x40000000(见 guest/linker.ld)
GOBJS := guest/start.o guest/vec.o guest/main1.o

all: $(TARGET) rtos.bin guest2.bin

$(TARGET): $(OBJS) linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.S
	$(CC) $(ASFLAGS) -c -o $@ $<

# VERSION 是注入宏的来源,必须作为先决条件(否则改版本不触发重编,门禁会抓)
$(OBJS): VERSION

# 第十一站:设备树。blob 由 dts 编译(dtc;无 dtc 时用随仓提交的 .dtb),
# incbin 在汇编期展开,-MMD 抓不到 -> dtb.o 显式依赖 blob
tools/dtb/eshine-vm.dtb tools/dtb/eshine-linux.dtb: tools/dtb/eshine-vm.dts tools/dtb/eshine-linux.dts
	dtc -q -I dts -O dtb -o tools/dtb/eshine-vm.dtb tools/dtb/eshine-vm.dts
	dtc -q -I dts -O dtb -o tools/dtb/eshine-linux.dtb tools/dtb/eshine-linux.dts
hypervisor/kernel/dtb.o: tools/dtb/eshine-vm.dtb tools/dtb/eshine-linux.dtb

# make run 与 make verify 同构:同一构建产物(含 guest 双镜像),
# run 走 run_qemu.sh 交互,verify 走 80 步门禁——两者验的是同一个 elf
run: $(TARGET) rtos.bin guest2.bin
	./scripts/run_qemu.sh

verify: $(TARGET) rtos.bin guest2.bin
	python3 tools/verify/verify_el2.py

guest/main1.o: guest/main.c guest/hvc.h guest/miniuart.h guest/gic.h guest/timer.h guest/virtio.h guest/fdt.h
	$(CC) $(CFLAGS) -DGUEST_ID=1 -c -o $@ $<

guest2.elf: guest/start.o guest/vec.o guest/main1.o guest/linker.ld
	$(CC) $(CFLAGS) -nostdlib -Wl,--build-id=none -T guest/linker.ld -o $@ guest/start.o guest/vec.o guest/main1.o

guest2.bin: guest2.elf
	$(CROSS)objcopy -O binary $< $@

# rtos:站十六 VM0 客(参考RTOS 味最小任务内核,共享 guest/ ABI 头)。
# fp 自证走手写 asm,全程 -mgeneral-regs-only 不破。
RT_CFLAGS := $(CFLAGS) -Iguest
RTOBJS := rtos/start.o rtos/vec.o rtos/rt_sched.o rtos/rt_task.o \
	rtos/rt_mm0.o rtos/rt_syscall.o \
          rtos/rt_uart.o rtos/rt_shell.o rtos/rt_main.o rtos/rt_fp.o \
          rtos/rt_sync.o rtos/rt_ipc.o rtos/rt_demo.o rtos/rt_timer.o \
          rtos/rt_as.o rtos/rt_msg.o rtos/rt_fsrv.o rtos/ramfs.o \
          rtos/rt_sig.o

# 站31:rootfs 构建期打包——tools/ramfs 树 -> cpio(newc),ramfs.S
# .incbin 内嵌(汇编期展开,-MMD 抓不到 -> ramfs.o 显式依赖归档)
rtos/ramfs.cpio: tools/gen_ramfs.py $(wildcard tools/ramfs/*)
	python3 tools/gen_ramfs.py tools/ramfs rtos/ramfs.cpio

rtos/start.o: rtos/start.S
	$(CC) $(ASFLAGS) -c -o $@ $<
rtos/vec.o: rtos/vec.S
	$(CC) $(ASFLAGS) -c -o $@ $<
rtos/ramfs.o: rtos/ramfs.S rtos/ramfs.cpio
	$(CC) $(ASFLAGS) -c -o $@ $<
rtos/%.o: rtos/%.c rtos/rtos.h
	$(CC) $(RT_CFLAGS) -c -o $@ $<
rtos.elf: $(RTOBJS) rtos/linker.ld
	$(CC) $(RT_CFLAGS) -nostdlib -Wl,--build-id=none,--no-warn-rwx-segments -T rtos/linker.ld -o $@ $(RTOBJS)
rtos.bin: rtos.elf
	$(CROSS)objcopy -O binary $< $@

clean:
	rm -f $(OBJS) $(TARGET) $(OBJS:.o=.d) $(GOBJS) guest/*.d guest1.elf guest2.elf guest1.bin guest2.bin \
	       $(RTOBJS) rtos/*.d rtos.elf rtos.bin rtos/ramfs.cpio

.PHONY: all run verify clean

# 头文件自动依赖(-MMD -MP),头文件改动触发重编
-include $(OBJS:.o=.d) $(GOBJS:.o=.d) $(RTOBJS:.o=.d)
