# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu

# 站三十四:eshine-rtos 部署配方——从源归档构建到三镜像部署。
# 机制镜像参考实现(git archive 单仓进 WORKDIR)而零补丁:Makefile
# 的 CROSS ?= 环境驱动,宿主工具链一行声明(v0 政策,spec §2);
# 判据=部署产物真实可靴(verify [93]),非 parse-only。

SUMMARY = "eshine-rtos: from-scratch ARM64 type-1 hypervisor + RTOS"
DESCRIPTION = "From-scratch clean-room ARM64 Type-1 hypervisor with a \
QNX-flavored micro RTOS guest (EL0 userland: shell, per-process address \
spaces, three-phase IPC, resmgr servers, musl userland). Deploy-only \
recipe: eshine.elf (EL2 hypervisor) + rtos.bin (VM0) + guest2.bin (bare-metal guest)."
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=c4f0a156f847eac5ee2c220cb016e034"

SECTION = "base"
PV = "27.04"

# tarball 自带前缀 eshine-os-${PV}/=即 S(勿再 subdir=双目录
# 嵌套,LICENSE 落 S/eshine-os-27.04/ 层 QA 炸,实测)
SRC_URI = "file://eshine-os-${PV}.tar.gz"
S = "${WORKDIR}/eshine-os-${PV}"

# v0 工具链政策:不拉 gcc-cross(参考实现须 60 补丁切换的面),宿主
# aarch64-linux-gnu- 四件经 layer.conf HOSTTOOLS 白名单直入;裸机
# freestanding 构建(gcc-cross 内建=后弧)。
INHIBIT_DEFAULT_DEPS = "1"
DEPENDS = ""

EXTRA_OEMAKE = "CROSS=aarch64-linux-gnu-"

do_compile () {
    # 构建吃提交树(archive-source.sh 的 git archive HEAD),非工作树
    make ${EXTRA_OEMAKE}
}

inherit deploy nopackages

do_deploy () {
    install -m 0644 eshine.elf ${DEPLOYDIR}/eshine.elf
    install -m 0644 rtos.bin    ${DEPLOYDIR}/rtos.bin
    install -m 0644 guest2.bin  ${DEPLOYDIR}/guest2.bin
    install -m 0644 rtos/ramfs.cpio ${DEPLOYDIR}/eshine-ramfs.cpio
}
addtask deploy after do_compile before do_build

# 站34 判据面:do_test 部署产物可靴(verify [93] 走 sstate 快路径
# 后以部署物起 QEMU 跑 musl hello;机器面=外部 poky/build 目录,
# 与 eshine-linux 同位的仓外工件纪律)。
do_test[depends] = "eshine-rtos:do_deploy"
