#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
# 站四十:musl-static busybox(1.36.1)构建配方——vm0/vm2 双世界同字节。
#
# 用法:tools/build-busybox.sh           # 产物落 tools/ramfs/bin/busybox
#
# 机制(0 patch 目标):
# 1) 工具链=站 33 musl 安装(/tmp/musl-build/inst;不存在则先跑
#    tools/build-musl.sh)。CC=交叉 gcc+-isystem musl 头(站 33 同律:
#    musl 头压默认 sysroot,linux/ uapi 头仍由交叉 sysroot 供给——
#    musl 安装不含内核头)。
# 2) GPL-2.0 隔离照 vm2 先例(站 15):busybox 源与产物皆仓外/
#    gitignore,配方与判据入仓。
# 3) 链接窗基 0x40C00000(站 33 AT_PHDR 律:头入段。-z
#    noseparate-code 单 PT_LOAD+FILEHDR,同 hello 形;真内核/vm2
#    按 p_vaddr 落 0x40C00000 不冲突)。
# 4) 栈预算:图像 ~1MB + 栈@0x40D80000 + malloc 缺省落位(游标恒越
#    hint 段尾,rt_as.c 契约)= 2MB 窗内账。
# 5) applet 集=allnoconfig+精选表(defconfig 1.3MB 驻 rtos 内核像
#    0x40200000 两区式硬界下塞不下——两区式 stage-1 块 0=2MB=内核
#    像预算,ramfs+内核文本/数据+bss 合账;精选集 ~600KB 留余量)。
#    旗舰判据不损:echo/cat/ls/date/md5sum 皆在集内。
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${BUSYBOX_BUILD_DIR:-/tmp/busybox-build}"
MUSL="${MUSL_BUILD_DIR:-/tmp/musl-build}"
CROSS=aarch64-linux-gnu-
VER=1.36.1
TARBALLS=("$HOME/projects/eshine-linux/busybox-${VER}.tar.bz2")
BB_URL="https://busybox.net/downloads/busybox-${VER}.tar.bz2"

if [ ! -x "$MUSL/inst/bin/musl-gcc" ] || [ ! -f "$MUSL/inst/lib/musl-gcc.specs" ]; then
    echo "== musl 安装缺失,先跑 tools/build-musl.sh =="
    "$REPO/tools/build-musl.sh"
fi
SPECS="$MUSL/inst/lib/musl-gcc.specs"
# musl specs 自带 -nostdinc(站 40 实测):默认 sysroot 整个被摘,
# busybox 要的 <linux/*.h> uapi 失供。外科修法:只拷交叉 sysroot
# 的 uapi 三目录作独立 -isystem(零 glibc 头混入,musl 头恒先)。
SYSROOT=/usr/aarch64-linux-gnu/include
mkdir -p "$WORK/uapi"
for d in linux asm asm-generic mtd scsi sound video rdma xen; do
    [ -d "$SYSROOT/$d" ] || continue
    rm -rf "$WORK/uapi/$d"
    cp -r "$SYSROOT/$d" "$WORK/uapi/$d"
done
CCFLAGS="$CROSS"gcc" -isystem $MUSL/inst/include -isystem $WORK/uapi -specs $SPECS"
LDFLAGS_WIN="-Wl,-z,noseparate-code -Wl,-Ttext-segment=0x40C00000"

mkdir -p "$WORK"
cd "$WORK"
if [ ! -d "busybox-${VER}" ]; then
    if [ ! -f "busybox-${VER}.tar.bz2" ]; then
        found=""
        for t in "${TARBALLS[@]}"; do
            [ -f "$t" ] && found="$t" && break
        done
        if [ -z "$found" ]; then
            echo "== 下载 busybox ${VER}(直连先试,退 代理)=="
            CURL="curl -fL --retry 3"
            curl -sI --max-time 8 "$BB_URL" 2>/dev/null | head -1 | \
                grep -q " 200" || CURL="$CURL -x $ESHINE_PROXY"
            $CURL -o "busybox-${VER}.tar.bz2" "$BB_URL"
        else
            cp "$found" "busybox-${VER}.tar.bz2"
        fi
    fi
    tar xf "busybox-${VER}.tar.bz2"
fi
cd "busybox-${VER}"

echo "== allnoconfig + STATIC + 精选 applet 表(站 40 精选集)=="
make ARCH=arm64 CROSS_COMPILE="$CROSS" allnoconfig >/dev/null
APPLETS="STATIC ECHO CAT LS DATE UNAME MD5SUM SLEEP TRUE FALSE PWD \
 WC HEAD TAIL TOUCH MKDIR RM CP MV LN STAT HEXDUMP DU DF \
 READLINK BASENAME DIRNAME ID WHOAMI TEST CLEAR SYNC \
 ASH FEATURE_EDITING FEATURE_EDITING_FANCY_PROMPT FEATURE_SH_STANDALONE \
 MOUNT UMOUNT \
 IFCONFIG PING FEATURE_FANCY_PING"
# 站58:IFCONFIG+PING 入集(eth0 起 link+ARP TX 触发=帧收发设备级判据;
# FANCY=-c/-W 参数面(实测:无 FANCY 则 "bad address '-c'" 拒参);
# 参考实现全 applet 面=后弧,精选集纪律不变)
sed -i "s/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/" .config
for a in $APPLETS; do
    sed -i "s/^# CONFIG_$a is not set/CONFIG_$a=y/" .config
done
# 站68F:applet 类翻转(REGULAR→NOEXEC)——精选集里上游标 APPLET() 的
# 普通类(cat/sleep/wc/tail/du/mount/ifconfig/ping)在 ash 的 tryexec 里
# 走 execve(busybox 自身路径/路径表安装位),而本内核 exec=用户态驱动
# (站29 红线:exec_setup 帧改写,无 execve syscall)→ ENOSYS,ash 报
# "sh: <cmd>: Function not implemented" + exit 126(实测 cat/tail/wc 三
# 灭,md5sum/ls/head=NOEXEC 类全活:ash.c tryexec 的 APPLET_IS_NOEXEC
# 分支=run_noexec_applet_and_exit 内建分发零 execve)。修=类翻 NOEXEC。
# 真源=.c 文件头的 //applet: 注解(include/applets.h 是其生成物,配置步
# 再生会冲掉对生成物的 sed——首版教训:flip on 生成物=永不入二进制)。
# 上游标志是行为选择非接口契约;逐条断言生效(静默替换雷=哑包)。
flip_src() { # flip_src <file> <旧行> <新行> —— 幂等(源目录跨跑驻留)
    grep -q -F "$3" "$2" && return 0
    sed -i "s@^$1\$@$3@" "$2"
    grep -q -F "$3" "$2" || { echo "FATAL: applet 类翻转未生效: $3"; exit 1; }
}
flip_src '//applet:IF_CAT(APPLET(cat, BB_DIR_BIN, BB_SUID_DROP))'\
         coreutils/cat.c\
         '//applet:IF_CAT(APPLET_NOEXEC(cat, cat, BB_DIR_BIN, BB_SUID_DROP, cat))'
flip_src '//applet:IF_SLEEP(APPLET(sleep, BB_DIR_BIN, BB_SUID_DROP))'\
         coreutils/sleep.c\
         '//applet:IF_SLEEP(APPLET_NOEXEC(sleep, sleep, BB_DIR_BIN, BB_SUID_DROP, sleep))'
flip_src '//applet:IF_WC(APPLET(wc, BB_DIR_USR_BIN, BB_SUID_DROP))'\
         coreutils/wc.c\
         '//applet:IF_WC(APPLET_NOEXEC(wc, wc, BB_DIR_USR_BIN, BB_SUID_DROP, wc))'
flip_src '//applet:IF_TAIL(APPLET(tail, BB_DIR_USR_BIN, BB_SUID_DROP))'\
         coreutils/tail.c\
         '//applet:IF_TAIL(APPLET_NOEXEC(tail, tail, BB_DIR_USR_BIN, BB_SUID_DROP, tail))'
flip_src '//applet:IF_DU(APPLET(du, BB_DIR_USR_BIN, BB_SUID_DROP))'\
         coreutils/du.c\
         '//applet:IF_DU(APPLET_NOEXEC(du, du, BB_DIR_USR_BIN, BB_SUID_DROP, du))'
flip_src '//applet:IF_IFCONFIG(APPLET(ifconfig, BB_DIR_SBIN, BB_SUID_DROP))'\
         networking/ifconfig.c\
         '//applet:IF_IFCONFIG(APPLET_NOEXEC(ifconfig, ifconfig, BB_DIR_SBIN, BB_SUID_DROP, ifconfig))'
flip_src '//applet:IF_PING(APPLET(ping, BB_DIR_BIN, BB_SUID_MAYBE))'\
         networking/ping.c\
         '//applet:IF_PING(APPLET_NOEXEC(ping, ping, BB_DIR_BIN, BB_SUID_MAYBE, ping))'
flip_src '//applet:IF_MOUNT(APPLET(mount, BB_DIR_BIN, IF_DESKTOP(BB_SUID_MAYBE) IF_NOT_DESKTOP(BB_SUID_DROP)))'\
         util-linux/mount.c\
         '//applet:IF_MOUNT(APPLET_NOEXEC(mount, mount, BB_DIR_BIN, BB_SUID_DROP, mount))'
# 强制生成物再生(增量构建陈旧表防呆):applets.h 与 applet_tables.h
# 皆生成物,删旧逼重建——翻转是否真入二进制由末尾旗标位断言把关。
rm -f include/applets.h include/applet_tables.h include/NUM_APPLETS.h applets/applet_tables
yes "" | # oldconfig 对新符号交互提问(无 tty 即悬空)——yes 喂默认
    yes '' | make ARCH=arm64 CROSS_COMPILE="$CROSS" oldconfig >/dev/null

echo "== 构建(CC 携 musl 头+specs;LDFLAGS 窗基 0x40C00000)=="
make ARCH=arm64 CROSS_COMPILE="$CROSS" CC="$CCFLAGS" \
     EXTRA_LDFLAGS="$LDFLAGS_WIN" -j"$(nproc)" busybox_unstripped 2>&1 | tail -5

cp busybox_unstripped busybox
${CROSS}strip busybox
LC_ALL=C aarch64-linux-gnu-readelf -lW busybox | grep -E "LOAD|Entry" | head -8
sz=$(stat -c %s busybox)
echo "size: $sz bytes"
if [ "$sz" -gt 1500000 ]; then
    echo "WARN: 图像 >1.5MB,窗内账吃紧(0x40C00000+1.5MB=0x40D68000,栈 0x40D80000)"
fi
mkdir -p "$REPO/tools/ramfs/bin"
cp busybox "$REPO/tools/ramfs/bin/busybox"
echo "== 产物: tools/ramfs/bin/busybox(GPL:勿入 git;vm0 ramfs+vm2 initramfs 共用字节)=="
