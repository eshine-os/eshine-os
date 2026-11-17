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
           -Wall -Wextra -Ihypervisor -Icommon
ASFLAGS := -mgeneral-regs-only -march=armv8-a
LDFLAGS := -nostdlib -Wl,--build-id=none -T linker.ld

CSRCS := $(wildcard hypervisor/kernel/*.c) $(wildcard hypervisor/drivers/*.c) \
         $(wildcard hypervisor/arch/arm64/mm/*.c)
ASRCS := $(wildcard hypervisor/arch/arm64/*.S)
OBJS  := $(CSRCS:.c=.o) $(ASRCS:.S=.o) common/elog_core_el2.o

TARGET := eshine.elf

# guest:独立构建,同源双身份(-DGUEST_ID=0/1),链接 IPA 0x40000000(见 guest/linker.ld)
GOBJS := guest/start.o guest/vec.o guest/main1.o

all: $(TARGET) rtos.bin guest2.bin

# 站49:eshine_log 契约核心(双树单源 common/,两侧各自编为独立对象:
# 标志不同不可共物,一源两物=契约单源与构建隔离两立)。规则必须
# 在 all: 之后——首目标=默认 goal,规则先于 all 会劫持 make 裸跑
# (实测:elog_be.o 从未编译、main.o 陈旧、elf 不重链而 make 静默
# "已是最新";站 49 战雷第一枚)。
common/elog_core_el2.o: common/eshine_log_core.c common/eshine_log.h
	$(CC) $(CFLAGS) -c -o $@ $<
common/elog_core_rt.o: common/eshine_log_core.c common/eshine_log.h
	$(CC) $(RT_CFLAGS) -c -o $@ $<

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
DTC ?= $(shell command -v dtc 2>/dev/null)
tools/dtb/eshine-vm.dtb tools/dtb/eshine-linux.dtb: tools/dtb/eshine-vm.dts tools/dtb/eshine-linux.dts
	@if [ -n "$(DTC)" ]; then \
	    $(DTC) -q -I dts -O dtb -o tools/dtb/eshine-vm.dtb tools/dtb/eshine-vm.dts && \
	    $(DTC) -q -I dts -O dtb -o tools/dtb/eshine-linux.dtb tools/dtb/eshine-linux.dts; \
	else \
	    test -f tools/dtb/eshine-vm.dtb -a -f tools/dtb/eshine-linux.dtb || { \
	        echo "ERROR: dtc missing and committed dtb absent" >&2; exit 1; }; \
	    echo "WARNING: dtc missing; using committed dtb (may be stale)" >&2; \
	    touch tools/dtb/eshine-vm.dtb tools/dtb/eshine-linux.dtb; \
	fi
hypervisor/kernel/dtb.o: tools/dtb/eshine-vm.dtb tools/dtb/eshine-linux.dtb

# make run 与 make verify 同构:同一构建产物(含 guest 双镜像),
# run 走 run_qemu.sh 交互,verify 走 80 步门禁——两者验的是同一个 elf。
# 启动前确保工作根:缺工件则 --fetch,并重打 rtos.bin(同一 make 里
# 先建的 ramfs 不含 fetch 新产出的 busybox)。
run: $(TARGET) rtos.bin guest2.bin
	@if ! scripts/bootstrap.sh --check >/dev/null 2>&1; then \
	    echo "== work root incomplete; scripts/bootstrap.sh --fetch =="; \
	    scripts/bootstrap.sh --fetch || exit 1; \
	    $(MAKE) rtos.bin || exit 1; \
	fi
	./scripts/run_qemu.sh

verify: $(TARGET) rtos.bin guest2.bin
	python3 tools/verify/verify_el2.py

verify-fast: $(TARGET) rtos.bin guest2.bin
	python3 tools/verify/verify_el2.py --stop-after=36

# CI smoke tier (GitHub Actions / Gitee Go / local sanity): build + static
# checks + gates 1..18 (hypervisor boot + VM0 RTOS up). Full suite = verify.
ci-smoke: $(TARGET) rtos.bin guest2.bin
	CI_NO_BUILD=1 bash scripts/ci-smoke.sh

bootstrap:
	scripts/bootstrap.sh --all

check-isolation:
	scripts/check-host-independence.sh

container-build:
	scripts/container.sh build

container-verify:
	scripts/container.sh verify --stop-after=36

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
          rtos/rt_sig.o rtos/rt_shm.o rtos/rt_mq.o rtos/rt_pipe.o rtos/rt_blk.o \
          rtos/rt_thr.o rtos/rt_rng.o rtos/rt_procfs.o rtos/rt_slog.o rtos/rt_part.o rtos/rt_crash.o rtos/rt_xvm.o \
          rtos/rt_log_be.o rtos/rt_ham.o rtos/rt_sock.o common/elog_core_rt.o

# 站31:rootfs 构建期打包——tools/ramfs 树 -> cpio(newc),ramfs.S
# .incbin 内嵌(汇编期展开,-MMD 抓不到 -> ramfs.o 显式依赖归档)
# EL0 demo 件虽随仓提交,重建由 `make demos` 显式触发;它们不得成为
# 默认构建的传递先决条件(否则干净 checkout 会为打包 ramfs 去拉
# cjson/zlib/musl)。改了 demo 源码须 `make demos` 再 `make` 重打包。
RAMFS_DEMO_BINS := tools/ramfs/bin/jsontest tools/ramfs/bin/ztest \
                   tools/ramfs/bin/cyclictest tools/ramfs/bin/coreparse
RAMFS_INPUTS := $(filter-out $(RAMFS_DEMO_BINS),\
                  $(shell find tools/ramfs -type f 2>/dev/null))
rtos/ramfs.cpio: tools/gen_ramfs.py $(RAMFS_INPUTS)
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

# 站59:EL0 生态件 demo 链接(cJSON/zlib 静态链;先跑 tools/build-musl.sh
# +build-cjson.sh+build-zlib.sh 备工作根三件)。内核目标与 EL0 目标分立。
ESHINE_WORK ?= $(CURDIR)/.work
MUSLDIR ?= $(if $(ESHINE_MUSL_DIR),$(ESHINE_MUSL_DIR),\
            $(if $(MUSL_BUILD_DIR),$(MUSL_BUILD_DIR),$(ESHINE_WORK)/musl-build))
CJSONDIR ?= $(if $(ESHINE_CJSON_DIR),$(ESHINE_CJSON_DIR),\
             $(if $(CJSON_BUILD_DIR),$(CJSON_BUILD_DIR),$(ESHINE_WORK)/cjson-build))
ZLIBDIR ?= $(if $(ESHINE_ZLIB_DIR),$(ESHINE_ZLIB_DIR),\
            $(if $(ZLIB_BUILD_DIR),$(ZLIB_BUILD_DIR),$(ESHINE_WORK)/zlib-build))
EL0FLAGS := -static -O2 -fno-asynchronous-unwind-tables \
	-isystem $(MUSLDIR)/inst/include \
	-specs $(MUSLDIR)/inst/lib/musl-gcc.specs \
	-T tools/build-musl-link.lds -Wl,--no-warn-rwx-segments -Wl,-z,noseparate-code

.PHONY: demos
demos: $(RAMFS_DEMO_BINS)
	@rm -f rtos/ramfs.cpio    # force repack with the freshly built demos

tools/ramfs/bin/jsontest: tools/ramfs-jsontest.c $(CJSONDIR)/libcjson.a
	$(CROSS)gcc $(EL0FLAGS) -I$(CJSONDIR)/cJSON-1.7.18 -o $@ $< $(CJSONDIR)/libcjson.a -lc
tools/ramfs/bin/ztest: tools/ramfs-ztest.c $(ZLIBDIR)/libz.a
	$(CROSS)gcc $(EL0FLAGS) -I$(ZLIBDIR)/zlib-1.3.1 -o $@ $< $(ZLIBDIR)/libz.a -lc
tools/ramfs/bin/cyclictest: tools/ramfs-cyclictest.c
	$(CROSS)gcc $(EL0FLAGS) -o $@ $< -lc
tools/ramfs/bin/coreparse: tools/ramfs-coreparse.c     # 站64:ELF core 离线解析件
	$(CROSS)gcc $(EL0FLAGS) -o $@ $< -lc

.PHONY: all run verify verify-fast ci-smoke clean demos

# 头文件自动依赖(-MMD -MP),头文件改动触发重编
-include $(OBJS:.o=.d) $(GOBJS:.o=.d) $(RTOBJS:.o=.d)
