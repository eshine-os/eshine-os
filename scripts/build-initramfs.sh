#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
# 第十五站:busybox initramfs 构建配方(可复现)。busybox 与内核 Image
# 同为外部工件(GPL-2.0 隔离,仓内零字节);产物放仓外 $LINUX_DIR。
# 槽位契约:恰好 8MB(与 tools/dtb/eshine-linux.dts 的 linux,initrd-end
# 0x44800000 绑死,改动须同 commit 同步)。
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
. "$REPO/scripts/env.sh"
VER=1.36.1
DIR="$ESHINE_LINUX_DIR"
SLOT=$((8 * 1024 * 1024))
BB_URL="https://busybox.net/downloads/busybox-${VER}.tar.bz2"
CURL="curl -fL --retry 3"
# busybox.net 直连多半不通,先试直连;失败时经 ESHINE_PROXY(可选)。
# 仅在实际需要下载时探测网络;缓存归档存在时全离线。
curl_head_ok() { curl -sI --max-time 8 "$1" 2>/dev/null | head -1 | grep -q " 200"; }

mkdir -p "$DIR"
cd "$DIR"
if [ ! -d "busybox-${VER}" ]; then
    if [ ! -f "busybox-${VER}.tar.bz2" ]; then
        for cand in "$ESHINE_DOWNLOAD_DIR/busybox-${VER}.tar.bz2" \
                    "$ESHINE_LINUX_DIR/busybox-${VER}.tar.bz2"; do
            [ -f "$cand" ] || continue
            cp --update=none "$cand" "busybox-${VER}.tar.bz2" 2>/dev/null || \
                cp -n "$cand" "busybox-${VER}.tar.bz2"
            break
        done
    fi
    if [ ! -f "busybox-${VER}.tar.bz2" ]; then
        if ! curl_head_ok "$BB_URL"; then
            if [ -n "${ESHINE_PROXY:-}" ]; then
                CURL="$CURL -x $ESHINE_PROXY"
            elif ! wget -q -T 20 --spider "$BB_URL"; then
                echo "ERROR: 无法直连 $BB_URL(curl/wget 均不通;可设 ESHINE_PROXY)" >&2
                exit 1
            fi
        fi
        echo "== 下载 busybox ${VER} =="
        if ! $CURL -o "busybox-${VER}.tar.bz2" "$BB_URL"; then
            rm -f "busybox-${VER}.tar.bz2"
            wget -q -T 120 -O "busybox-${VER}.tar.bz2" "$BB_URL"
        fi
    fi
    BB_SHA=$(python3 -c "import json;print(json.load(open('$REPO/tools/artifacts.json'))['busybox']['sha256'])")
    echo "$BB_SHA  busybox-${VER}.tar.bz2" | sha256sum -c -
    echo "== 解压 =="
    tar xf "busybox-${VER}.tar.bz2"
fi

# 站40 双世界面:build-busybox.sh 的 musl 精选集总是先就位
# (bootstrap --fetch 顺序),它会被原样装进 initramfs。那份 glibc
# 自建只在 musl busybox 缺席时才是退路;存在时跳过整个编译——省
# ~1-2 分钟,也免掉容器镜像对 target glibc 头(libc6-dev-arm64-cross)
# 的依赖。容器冷构建实测:无此头时 glibc busybox 编译在
# applets/applets.c 撞 <bits/libc-header-start.h>。
if [ ! -f "$REPO/tools/ramfs/bin/busybox" ]; then
    cd "busybox-${VER}"
    echo "== defconfig + STATIC(aarch64 交叉) =="
    make ARCH=arm64 CROSS_COMPILE="$CROSS" defconfig
    sed -i 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
    # tc applet 撞新内核头(CBQ 定义自 linux-libc-dev 移除,1.36.1 时代坑),
    # initramfs 用不上;Ubuntu 24.04 头实测
    sed -i 's/^CONFIG_TC=y/# CONFIG_TC is not set/' .config
    make ARCH=arm64 CROSS_COMPILE="$CROSS" -j"$(nproc)"
    # 退路分支的 cp busybox 取相对路径:cwd 保持在 busybox 源目录
else
    echo "== busybox: vm0 musl 精选集在位,跳过 glibc 自建 =="
fi

ROOT="$DIR/initramfs-root"
echo "== 组装 rootfs =="
rm -rf "$ROOT"
mkdir -p "$ROOT/bin" "$ROOT/dev" "$ROOT/proc" "$ROOT/sys" "$ROOT/tmp" \
         "$ROOT/mnt" "$ROOT/root" "$ROOT/etc"
# 站40:busybox 双世界右翼——优先用 vm0 同一 musl busybox 字节
# (tools/ramfs/bin/busybox=build-busybox.sh 精选集产物,GPL 仓外);
# 缺席则退 glibc 自建(站 15 原配方)。
if [ -f "$REPO/tools/ramfs/bin/busybox" ]; then
    cp "$REPO/tools/ramfs/bin/busybox" "$ROOT/bin/busybox"
    echo "== busybox: vm0 同字节 musl 精选集(双世界判据面) =="
else
    cp busybox "$ROOT/bin/busybox"
    echo "== busybox: 本地 glibc 构建(站 15 原配方) =="
fi
# 站40:/etc/motd 双世界共用(md5sum 同哈希判据的公共输入)
mkdir -p "$ROOT/etc"
cp "$REPO/tools/ramfs/etc/motd" "$ROOT/etc/motd"
# 站33:双 musl 世界右翼——与 vm0 ramfs 同一 ELF 字节(tools/
# ramfs/bin/hello,build-musl.sh 产物入仓);48KB vs 8MB 槽余量足
cp "$REPO/tools/ramfs/bin/hello" "$ROOT/bin/hello"
# 内核 initramfs 默认 init=/init:挂 proc/sys(/proc/cpuinfo 自证判据的
# 前提)后 exec sh,PID 1 平滑过渡给 shell
cat > "$ROOT/init" <<'EOF_INIT'
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
exec /bin/sh
EOF_INIT
chmod 755 "$ROOT/init"
# cat/echo/mount 等为门禁与手玩备好
for cmd in sh cat echo ls uname mount umount ps free sleep ifconfig ping; do
    ln -sf busybox "$ROOT/bin/$cmd"
done

echo "== 打包 newc(定长 ${SLOT} 槽) =="
python3 "$REPO/tools/mkinitcpio.py" "$ROOT" --size "$SLOT" \
        --dev console:5:1 --dev null:1:3 \
        --dev dev/mem:1:1 > "$DIR/initramfs.cpio"   # 站58:mem=内核文本客侧取证

size=$(stat -c%s "$DIR/initramfs.cpio")
echo "== 产物: $DIR/initramfs.cpio ($size 字节;硬门 <= ${SLOT}) =="
if [ "$size" -ne "$SLOT" ]; then
    echo "FAIL: 定长槽契约破坏(须恰好 ${SLOT})"
    exit 1
fi
