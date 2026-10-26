#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""newc cpio 打包器(第十五站 initramfs 用)。

为何自写:mknod 需 root 而本机无 fakeroot;newc 格式是 110 字节
定宽头 + 文件数据流,spec 一页可读——从零气质优先。

格式(cpio "newc",SVR4):每条目 = 6 字节魔法 "070701" + 13 个
8 字符十六进制字段(ino/mode/uid/gid/nlink/mtime/filesize/
devmajor/devminor/rdevmajor/rdevminor/namesize/check),随后
文件名(含 \\0),整体补齐 4 字节,再数据,再补齐 4。check
字段内核不校验,置 0(bins 常规)。收尾 TRAILER!!! 后零填充
到定长槽宽——cpio 尾零是合法 padding,内核解包器遇 TRAILER
即止(站 15 定长 8MB 槽的依据)。

用法:mkinitcpio.py <rootfs-dir> --size <bytes> [--dev name:major:minor]..
设备节点无需 root:以 spec 行虚拟 mknod。
"""
import argparse
import os
import stat
import sys


def _pad4(n):
    return (-n) % 4


def _entry(name, mode, size, rmaj, rmin, data):
    """单条目编码;namesize 含结尾 NUL。"""
    hdr_name = name.encode()
    head = ("070701" + "".join(
        "%08X" % v
        for v in (
            0,           # ino:内核解包器不用
            mode,
            0,           # uid:root
            0,           # gid:root
            1,           # nlink:无硬链接语义
            0,           # mtime:确定性构建
            size,
            0, 0,        # 文件所在"磁盘"设备:不适用
            rmaj, rmin,
            len(hdr_name) + 1,
            0,           # check:内核不校验
        )
    )).encode()
    out = head + hdr_name + b"\x00"
    out += b"\x00" * _pad4(len(out))
    out += data
    out += b"\x00" * _pad4(len(out))
    return out


def _walk(root):
    """rootfs 目录序:目录先于内容,内容按名字序(确定性)。"""
    ents = []
    for base, dirs, files in os.walk(root):
        dirs.sort()
        files.sort()
        rel = os.path.relpath(base, root)
        if rel != ".":
            ents.append(("d", os.path.join(rel, "")))
        for f in files:
            ents.append(("f", os.path.join(rel, f)))
        for d in dirs:
            if os.path.islink(os.path.join(base, d)):
                ents.append(("l", os.path.join(rel, d + "/")))
    return ents


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root", help="rootfs 目录")
    ap.add_argument("--size", type=int, required=True,
                    help="定长槽字节数(TRAILER 后零填充到此)")
    ap.add_argument("--dev", action="append", default=[],
                    help="设备节点 spec name:major:minor(如 console:5:1)")
    args = ap.parse_args()

    blob = b""
    for kind, rel in _walk(args.root):
        path = os.path.join(args.root, rel)
        name = rel.rstrip("/").replace(os.sep, "/")
        if kind == "d":
            blob += _entry(name, 0o040755, 0, 0, 0, b"")
        elif kind == "l" or os.path.islink(path):
            target = os.readlink(path).encode()
            blob += _entry(name, 0o120777, len(target), 0, 0, target)
        else:
            with open(path, "rb") as fh:
                data = fh.read()
            blob += _entry(name, 0o0100755, len(data), 0, 0, data)

    for spec in args.dev:
        name, major, minor = spec.split(":")
        blob += _entry(name, 0o020600, 0, int(major), int(minor), b"")

    blob += _entry("TRAILER!!!", 0, 0, 0, 0, b"")

    if len(blob) > args.size:
        sys.exit("FAIL: cpio %d 字节 > 槽 %d" % (len(blob), args.size))
    blob += b"\x00" * (args.size - len(blob))
    sys.stdout.buffer.write(blob)


if __name__ == "__main__":
    main()
