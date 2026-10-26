#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
# 第十五站:busybox initramfs 构建配方(可复现)。busybox 与内核 Image
# 同为外部工件(GPL-2.0 隔离,仓内零字节);产物放仓外 $LINUX_DIR。
# 槽位契约:恰好 8MB(与 tools/dtb/eshine-linux.dts 的 linux,initrd-end
# 0x44800000 绑死,改动须同 commit 同步)。
set -e
VER=1.36.1
DIR="${LINUX_DIR:-$HOME/projects/eshine-linux}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SLOT=$((8 * 1024 * 1024))
BB_URL="https://busybox.net/downloads/busybox-${VER}.tar.bz2"
CURL="curl -fL --retry 3"
# busybox.net 直连多半不通,先试直连再退代理(与站 14 内核取源同法)
curl_head_ok() { curl -sI --max-time 8 "$1" 2>/dev/null | head -1 | grep -q " 200"; }
if ! curl_head_ok "$BB_URL"; then
    CURL="$CURL -x $ESHINE_PROXY"
fi

mkdir -p "$DIR"
cd "$DIR"
if [ ! -d "busybox-${VER}" ]; then
    if [ ! -f "busybox-${VER}.tar.bz2" ]; then
        echo "== 下载 busybox ${VER} =="
        $CURL -o "busybox-${VER}.tar.bz2" "$BB_URL"
    fi
    echo "== 解压 =="
    tar xf "busybox-${VER}.tar.bz2"
fi

cd "busybox-${VER}"
echo "== defconfig + STATIC(aarch64 交叉) =="
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
sed -i 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
# tc applet 撞新内核头(CBQ 定义自 linux-libc-dev 移除,1.36.1 时代坑),
# initramfs 用不上;Ubuntu 24.04 头实测
sed -i 's/^CONFIG_TC=y/# CONFIG_TC is not set/' .config
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j"$(nproc)"

ROOT="$DIR/initramfs-root"
echo "== 组装 rootfs =="
rm -rf "$ROOT"
mkdir -p "$ROOT/bin" "$ROOT/dev" "$ROOT/proc" "$ROOT/sys" "$ROOT/tmp" \
         "$ROOT/mnt" "$ROOT/root" "$ROOT/etc"
cp busybox "$ROOT/bin/busybox"
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
for cmd in sh cat echo ls uname mount umount ps free sleep; do
    ln -sf busybox "$ROOT/bin/$cmd"
done

echo "== 打包 newc(定长 ${SLOT} 槽) =="
python3 "$REPO/tools/mkinitcpio.py" "$ROOT" --size "$SLOT" \
        --dev console:5:1 --dev null:1:3 > "$DIR/initramfs.cpio"

size=$(stat -c%s "$DIR/initramfs.cpio")
echo "== 产物: $DIR/initramfs.cpio ($size 字节;硬门 <= ${SLOT}) =="
if [ "$size" -ne "$SLOT" ]; then
    echo "FAIL: 定长槽契约破坏(须恰好 ${SLOT})"
    exit 1
fi
