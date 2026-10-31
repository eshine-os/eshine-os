#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""站 31:构建期打包 rootfs——tools/ramfs 目录树 -> cpio(newc) 归档。

ramfs 服务器(rtos/rt_fsrv.c)开机解析此归档:newc 磁带格式,
110B 头(6B 魔数 "070701" + 13 段 8 字符大写十六进制域)+
名字(含 NUL)+4 对齐补零 + 数据 + 4 对齐,TRAILER!!! 收卷。
域序:ino mode uid gid nlink mtime filesize devmaj devmin rdevmaj
rdevmin namesize check——filesize@54、namesize@94(头内偏移)。
路径存相对名(cpio 通例,"etc/motd");服务器挂名时补前导 '/'。

用法: gen_ramfs.py <srcdir> <dstfile>
"""
import os
import sys


def field(v):
    return "%08X" % v


def entry(name, data, ino):
    vals = [ino, 0o100644, 0, 0, 1, 0, len(data), 0, 0, 0, 0,
            len(name) + 1, 0]
    out = b"070701" + "".join(field(v) for v in vals).encode()
    out += name.encode() + b"\0"
    out += b"\0" * ((4 - len(out) % 4) % 4)      # 头+名补到 4
    out += data
    out += b"\0" * ((4 - len(data) % 4) % 4)      # 数据补到 4
    return out


def main(src, dst):
    blob = b""
    ino = 1
    for root, dirs, files in os.walk(src):
        dirs.sort()
        for fn in sorted(files):
            full = os.path.join(root, fn)
            rel = os.path.relpath(full, src)
            with open(full, "rb") as f:
                data = f.read()
            blob += entry(rel, data, ino)
            ino += 1
    blob += entry("TRAILER!!!", b"", 0)
    with open(dst, "wb") as f:
        f.write(blob)
    print("gen_ramfs: %d bytes -> %s" % (len(blob), dst))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.stderr.write("usage: gen_ramfs.py <srcdir> <dstfile>\n")
        sys.exit(2)
    main(sys.argv[1], sys.argv[2])
