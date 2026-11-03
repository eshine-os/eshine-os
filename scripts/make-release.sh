#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
#
# 站六十三:发布镜像 tar——站 61 部署四件 + run 入口 + 工件账 + 合规面
# (LICENSE/NOTICE),组装 release/eshine-os-<ver>/ 打成
# release/eshine-os-<ver>.tar.gz。发布=可解包即靴:run.sh 直起 QEMU,
# 自靴判据(脚本尾部)在解包现场重演 musl hello 全链。
#
# 退出面:0=发布件+自靴绿;1=构建红;2=发布件红;3=自靴红。
# 用法:bash scripts/make-release.sh
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
VER="$(cat "$REPO/VERSION")"
OUT="$REPO/release"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

echo "== build (3rd x3 + make demos + make -B) =="
quiet() { local log="$1"; shift; "$@" > "$log" 2>&1 || {
    echo "== $1 failed, tail $log =="; tail -20 "$log"; exit 1; }; }
quiet /tmp/musl.log bash tools/build-musl.sh
quiet /tmp/cjson.log bash tools/build-cjson.sh
quiet /tmp/zlib.log bash tools/build-zlib.sh
quiet /tmp/demos.log make demos
quiet /tmp/make.log make -B
grep -qE "warning:|error:" /tmp/make.log && {
    echo "build not clean (warnings/errors in make -B)" >&2; exit 1; }

echo "== assemble release/eshine-os-$VER =="
IMG="$OUT/eshine-os-$VER"
rm -rf "$IMG"
install -d "$IMG/boot"
install -m 0644 eshine.elf rtos.bin guest2.bin "$IMG/boot/"
install -m 0644 rtos/ramfs.cpio "$IMG/boot/eshine-ramfs.cpio"
install -m 0644 LICENSE NOTICE "$IMG/"

# 工件账:版本+四件 sha256(站 61 manifest.json 同形,发布面)
{
    printf '{\n  "version": "%s",\n  "machine": "eshine-qemu-a57-type1",\n  "artifacts": [\n' "$VER"
    first=1
    for a in eshine.elf rtos.bin guest2.bin eshine-ramfs.cpio; do
        h=$(sha256sum "$IMG/boot/$a" | cut -d" " -f1)
        [ $first -eq 0 ] && printf ',\n'
        first=0
        printf '    {"name": "%s", "sha256": "%s"}' "$a" "$h"
    done
    printf '\n  ]\n}\n'
} > "$IMG/manifest.json"

# run 入口:站 61 eshine-image.bb 同形(启动参数=机器定义单一事实源
# eshine-qemu-a57-type1.conf 的镜像面;串口 socket 路径可入参)。
cat > "$IMG/run.sh" <<'RUNEOF'
#!/bin/sh
# eshine-os release boot entry (station 63; params mirror the machine
# definition meta-eshine/conf/machine/eshine-qemu-a57-type1.conf).
# Usage: ./run.sh [serial-socket-path]
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
SOCK="${1:-/tmp/eshine-release-boot.sock}"
rm -f "$SOCK"
exec qemu-system-aarch64 \
    -M virt,virtualization=on,gic-version=3 \
    -cpu cortex-a57 -m 512 -smp 4 \
    -display none -monitor none \
    -kernel "$DIR/boot/eshine.elf" \
    -device loader,file="$DIR/boot/rtos.bin",addr=0x50000000,force-raw=on \
    -device loader,file="$DIR/boot/guest2.bin",addr=0x51000000,force-raw=on \
    -chardev socket,id=ser0,path="$SOCK",server=on,wait=on \
    -serial chardev:ser0
RUNEOF
chmod 0755 "$IMG/run.sh"

echo "== pack release/eshine-os-$VER.tar.gz =="
rm -f "$OUT/eshine-os-$VER.tar.gz"
tar -czf "$OUT/eshine-os-$VER.tar.gz" -C "$OUT" "eshine-os-$VER"
[ -s "$OUT/eshine-os-$VER.tar.gz" ] || { echo "tar missing" >&2; exit 2; }

echo "== boot smoke from the packed release (unpack to a fresh dir) =="
BOOT="$STAGE/unpack"
mkdir -p "$BOOT"
tar -xzf "$OUT/eshine-os-$VER.tar.gz" -C "$BOOT"
python3 - "$BOOT/eshine-os-$VER" <<'PYEOF'
import socket, subprocess, sys, time

img = sys.argv[1]
run = img + "/run.sh"
q = subprocess.Popen([run], stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL)
sock_path = "/tmp/eshine-release-boot.sock"
try:
    dl = time.time() + 30
    s = None
    while time.time() < dl and s is None:
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(sock_path)
        except OSError:
            if s:
                s.close()
            s = None
            time.sleep(0.2)
    if s is None:
        print("release boot: serial socket never came up", file=sys.stderr)
        sys.exit(3)
    s.settimeout(1.0)
    buf = b""
    def read_until(needle, tmo):
        global buf
        dl2 = time.time() + tmo
        while needle not in buf:
            if time.time() > dl2:
                raise AssertionError("release boot: %r not found; tail=%r"
                                     % (needle, buf[-250:]))
            try:
                d = s.recv(4096)
                if d:
                    buf += d
            except socket.timeout:
                pass
    read_until(b"eshine> ", 30)
    s.sendall(b"vmcreate 0\n")
    read_until(b"vm0: CREATED", 15)
    s.sendall(b"vmrun 0\n")
    read_until(b"rtos: up", 30)
    s.sendall(b"vmconsole 0\n")
    read_until(b"console -> vm", 10)
    s.sendall(b"\n")
    read_until(b"rtos> ", 20)
    s.sendall(b"run /bin/hello\n")
    read_until(b"musl: hello from /bin/hello", 60)
    s.close()
finally:
    q.terminate()
    try:
        q.wait(5)
    except subprocess.TimeoutExpired:
        q.kill()
print("release: unpacked tar boots end-to-end (musl hello) — RELEASE OK")
PYEOF

echo "release: $OUT/eshine-os-$VER.tar.gz"
