#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
# Populate the single work root (ESHINE_WORK) with external artifacts.
#
# Modes:
#   --adopt   copy artifacts from user-supplied legacy directories into
#             ESHINE_WORK (migration-only; no network, no baked-in paths)
#   --fetch   download pinned sources and build everything (network)
#   --all     adopt when legacy inputs are given, otherwise fetch
#   --check   verify the work root only (default)
#   --with-yocto  also adopt/fetch poky and the yocto build tree
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
. "$REPO/scripts/env.sh"

MODE=check
WITH_YOCTO=0
LEGACY="${ESHINE_LEGACY_ROOT:-}"
LEGACY_MUSL="${ESHINE_LEGACY_MUSL_DIR:-}"
LEGACY_CJSON="${ESHINE_LEGACY_CJSON_DIR:-}"
LEGACY_ZLIB="${ESHINE_LEGACY_ZLIB_DIR:-}"

usage() {
    cat <<'EOF'
usage: scripts/bootstrap.sh [mode] [options]

modes:
  --adopt   copy existing artifacts into ESHINE_WORK (no network)
  --fetch   download pinned sources and build everything (network)
  --all     adopt if legacy inputs are given, otherwise fetch
  --check   verify the work root only (default)

options:
  --from DIR     legacy root containing eshine-linux/ (and optionally
                 poky/, eshine-yocto-build/)
  --musl DIR     legacy musl build directory
  --cjson DIR    legacy cJSON build directory
  --zlib DIR     legacy zlib build directory
  --with-yocto   also adopt/fetch poky and the Yocto build tree

env equivalents: ESHINE_LEGACY_ROOT, ESHINE_LEGACY_MUSL_DIR,
  ESHINE_LEGACY_CJSON_DIR, ESHINE_LEGACY_ZLIB_DIR
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --adopt|--fetch|--all|--check) MODE="${1#--}"; shift ;;
        --from)
            [ "$#" -ge 2 ] || { usage >&2; exit 2; }
            LEGACY="$2"; shift 2 ;;
        --musl)
            [ "$#" -ge 2 ] || { usage >&2; exit 2; }
            LEGACY_MUSL="$2"; shift 2 ;;
        --cjson)
            [ "$#" -ge 2 ] || { usage >&2; exit 2; }
            LEGACY_CJSON="$2"; shift 2 ;;
        --zlib)
            [ "$#" -ge 2 ] || { usage >&2; exit 2; }
            LEGACY_ZLIB="$2"; shift 2 ;;
        --with-yocto) WITH_YOCTO=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

sha_of() {  # sha_of <artifact-key>
    python3 - "$REPO/tools/artifacts.json" "$1" <<'PY'
import json, sys
print(json.load(open(sys.argv[1]))[sys.argv[2]]["sha256"])
PY
}

url_of() {
    python3 - "$REPO/tools/artifacts.json" "$1" <<'PY'
import json, sys
print(json.load(open(sys.argv[1]))[sys.argv[2]]["url"])
PY
}

check_sha() {  # check_sha <artifact-key> <file>
    [ -f "$2" ] || { echo "missing artifact: $2" >&2; return 1; }
    echo "$(sha_of "$1")  $2" | sha256sum -c -
}

cp_n() {  # cp_n <src> <dst>; no-clobber across coreutils versions
    cp --update=none "$1" "$2" 2>/dev/null || cp -n "$1" "$2"
}

copy_tree() {  # copy_tree <src> <dst>; hardlink when possible, never clobber
    [ -d "$1" ] || return 0
    mkdir -p "$2"
    cp -al --update=none "$1"/. "$2"/ 2>/dev/null || \
        cp -a --update=none "$1"/. "$2"/ 2>/dev/null || \
        cp -a "$1"/. "$2"/
}

download() {  # download <key> <dst>
    [ -f "$2" ] && return 0
    local url
    url=$(url_of "$1")
    mkdir -p "$(dirname "$2")"
    if curl -fL --retry 3 -o "$2" "$url" 2>/dev/null; then
        return 0
    fi
    if [ -n "${ESHINE_PROXY:-}" ]; then
        curl -fL --retry 3 -x "$ESHINE_PROXY" -o "$2" "$url"
        return 0
    fi
    echo "ERROR: cannot download $url (set ESHINE_PROXY)" >&2
    return 1
}

adopt() {
    echo "== adopt: legacy artifacts -> $ESHINE_WORK =="
    if [ -z "$LEGACY$LEGACY_MUSL$LEGACY_CJSON$LEGACY_ZLIB" ]; then
        echo "adopt: no legacy inputs; pass --from/--musl/--cjson/--zlib" >&2
        exit 2
    fi
    if [ -n "$LEGACY" ] && [ -d "$LEGACY/eshine-linux" ]; then
        for f in linux-6.6.tar.xz busybox-1.36.1.tar.bz2; do
            [ -f "$LEGACY/eshine-linux/$f" ] && \
                cp_n "$LEGACY/eshine-linux/$f" "$ESHINE_DOWNLOAD_DIR/$f"
        done
        copy_tree "$LEGACY/eshine-linux/linux-6.6" \
                  "$ESHINE_LINUX_DIR/linux-6.6"
        if [ -f "$LEGACY/eshine-linux/linux-6.6/arch/arm64/boot/Image" ]; then
            mkdir -p "$ESHINE_LINUX_DIR/linux-6.6/arch/arm64/boot"
            cp_n "$LEGACY/eshine-linux/linux-6.6/arch/arm64/boot/Image" \
                 "$ESHINE_LINUX_DIR/linux-6.6/arch/arm64/boot/Image"
        fi
        [ -f "$LEGACY/eshine-linux/initramfs.cpio" ] && \
            cp_n "$LEGACY/eshine-linux/initramfs.cpio" \
                 "$ESHINE_LINUX_DIR/initramfs.cpio"
    fi
    [ -n "$LEGACY_MUSL" ] && copy_tree "$LEGACY_MUSL" "$ESHINE_MUSL_DIR"
    [ -n "$LEGACY_CJSON" ] && copy_tree "$LEGACY_CJSON" "$ESHINE_CJSON_DIR"
    [ -n "$LEGACY_ZLIB" ] && copy_tree "$LEGACY_ZLIB" "$ESHINE_ZLIB_DIR"
    if [ "$WITH_YOCTO" = 1 ]; then
        if [ -n "$LEGACY" ]; then
            copy_tree "$LEGACY/poky" "$ESHINE_POKY_DIR"
            copy_tree "$LEGACY/eshine-yocto-build" "$ESHINE_YOCTO_BUILD"
        else
            echo "skip yocto adopt: --from is not set" >&2
        fi
    fi
}

fetch() {
    echo "== fetch: pinned sources -> $ESHINE_WORK =="
    download linux "$ESHINE_DOWNLOAD_DIR/linux-6.6.tar.xz"
    check_sha linux "$ESHINE_DOWNLOAD_DIR/linux-6.6.tar.xz"
    download busybox "$ESHINE_DOWNLOAD_DIR/busybox-1.36.1.tar.bz2"
    check_sha busybox "$ESHINE_DOWNLOAD_DIR/busybox-1.36.1.tar.bz2"
    if [ ! -d "$ESHINE_LINUX_DIR/linux-6.6" ]; then
        tar -C "$ESHINE_LINUX_DIR" -xf \
            "$ESHINE_DOWNLOAD_DIR/linux-6.6.tar.xz"
    fi
    "$REPO/scripts/build-linux.sh"
    "$REPO/tools/build-musl.sh"
    "$REPO/tools/build-cjson.sh"
    "$REPO/tools/build-zlib.sh"
    "$REPO/tools/build-busybox.sh"
    "$REPO/scripts/build-initramfs.sh"
    if [ "$WITH_YOCTO" = 1 ]; then
        "$REPO/scripts/setup-yocto.sh"
    fi
}

check() {
    local rc=0
    for f in "$ESHINE_LINUX_IMAGE" "$ESHINE_INITRAMFS" \
             "$ESHINE_BUSYBOX" \
             "$ESHINE_MUSL_DIR/inst/lib/musl-gcc.specs" \
             "$ESHINE_CJSON_DIR/libcjson.a" \
             "$ESHINE_ZLIB_DIR/libz.a"; do
        if [ -e "$f" ]; then
            echo "ok: $f"
        else
            echo "MISSING: $f" >&2; rc=1
        fi
    done
    if [ -f "$ESHINE_INITRAMFS" ]; then
        size=$(stat -c%s "$ESHINE_INITRAMFS")
        [ "$size" = 8388608 ] || {
            echo "MISSING/BAD: initramfs must be 8 MiB, got $size" >&2; rc=1; }
    fi
    if [ "$WITH_YOCTO" = 1 ] && [ ! -d "$ESHINE_POKY_DIR/bitbake" ]; then
        echo "MISSING: $ESHINE_POKY_DIR/bitbake (ynocto)" >&2; rc=1
    fi
    [ "$rc" = 0 ] && echo "work root ready: $ESHINE_WORK"
    return "$rc"
}

case "$MODE" in
    adopt) adopt; check ;;
    fetch) fetch; check ;;
    all)
        if [ -n "$LEGACY$LEGACY_MUSL$LEGACY_CJSON$LEGACY_ZLIB" ]; then
            adopt
        else
            fetch
        fi
        check ;;
    check) check ;;
esac