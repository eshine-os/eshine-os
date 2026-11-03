#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""站六十二:SBOM 台账完整性校验(门 [179] 消费者)。

校验面(全部绿才算过):
  1. manifest.yaml 每组件字段齐(名/版/许可/来源/in-repo);
  2. in-repo 工件存在且名含钉死版本(版本 pin 面);
  3. GPL 组件全部 in-repo: false(仓外隔离政策);
  4. NOTICE 列全组件名(聚合对照);
  5. LICENSE = Apache-2.0(本体许可)。

用法:python3 tools/sbom-check.py   # 0=全绿;非 0=首红项
"""
import hashlib
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

REQUIRED_FIELDS = ("name", "version", "license", "source", "in-repo")


def load_manifest(path):
    """极简 YAML 子集解析(components: 下连行 '- key: value')。"""
    comps = []
    cur = None
    with open(path, encoding="utf-8") as f:
        for ln in f:
            s = ln.rstrip("\n")
            m = re.match(r"^  - name: (.+)$", s)
            if m:
                if cur:
                    comps.append(cur)
                cur = {"name": m.group(1).strip()}
                continue
            m = re.match(r"^    ([a-z0-9-]+): (.*)$", s)
            if m and cur is not None:
                cur[m.group(1)] = m.group(2).strip()
    if cur:
        comps.append(cur)
    return comps


def main():
    mpath = os.path.join(REPO, "3rd/manifest.yaml")
    comps = load_manifest(mpath)
    if not comps:
        print("FAIL: manifest.yaml parsed empty")
        return 1

    for c in comps:
        for f in REQUIRED_FIELDS:
            if f not in c:
                print("FAIL: component %r missing field %r" % (c.get("name"), f))
                return 1
        # 版本 pin:in-repo 工件名含钉死版本
        if c["in-repo"] == "true":
            art = os.path.join(REPO, c["artifact"])
            if not os.path.isfile(art):
                print("FAIL: in-repo artifact missing: %s" % c["artifact"])
                return 1
            if c["version"] not in os.path.basename(art):
                print("FAIL: version pin broken: %s vs %s"
                      % (c["version"], c["artifact"]))
                return 1
        # GPL 隔离政策
        if c["license"].startswith("GPL") and c["in-repo"] == "true":
            print("FAIL: GPL component in-repo: %s" % c["name"])
            return 1

    with open(os.path.join(REPO, "NOTICE"), encoding="utf-8") as f:
        notice = f.read()
    for c in comps:
        if c["name"] not in notice:
            print("FAIL: NOTICE missing component %r" % c["name"])
            return 1

    with open(os.path.join(REPO, "LICENSE"), encoding="utf-8") as f:
        lic = f.read()
    if "Apache License" not in lic or "Version 2.0" not in lic:
        print("FAIL: LICENSE not Apache-2.0")
        return 1

    inrepo = [c["name"] for c in comps if c["in-repo"] == "true"]
    print("sbom: %d components, %d in-repo (pin ok), %d out-repo "
          "(GPL isolated)" % (len(comps), len(inrepo),
                              len(comps) - len(inrepo)))
    print("sbom: NOTICE aggregated, LICENSE Apache-2.0 — ALL GREEN")
    return 0


if __name__ == "__main__":
    sys.exit(main())
