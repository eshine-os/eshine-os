<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# eshine-os 26.10

**2026-10-21 · 第一个正式版**

eshine-os 26.10 是从零构建的第一个正式版本，交付 EL2 骨架：

- **直启 EL2**：QEMU virt（`virtualization=on, gic-version=3`）直接进入 EL2，
  无 ATF/EL3 —— 加载基址 0x40200000，PHDRS 三段（R-X/R--/RW-）零警告构建
- **16 槽异常向量表**：current-EL sync/irq 先实（sync 报 ESR_EL2/FAR_EL2），
  其余 12 槽默认拒绝占位
- **交互 mini-shell**：help / version / echo / state，带行编辑
- **VM 状态机**（生命周期先行）：INIT → CREATED → RUNNING ⇄ PAUSED →
  TEARDOWN → DEAD，表驱动，表外转换一律 panic
- **真绿门禁**：`verify_el2.py`（banner + 提示符 + token 双次回显，
  静态 marker 不算），CI 于 GitHub 同步后生效
- **版权纪律**：clean-room 从零撰写，全部溯源公开规范（ARM PL011 手册、
  QEMU virt 文档），Apache-2.0 + 全文件 SPDX

```
$ make verify
  [1/3] banner ok (26.10)
  [2/3] shell prompt ok
  [3/3] token echoed back (typed-echo + command output)
RESULT PASS
```

**下一版**：27.04（2027-04）—— stage2 页表与第一个 guest 地址空间。
发布节奏：每年 4 月 / 10 月（见 [docs/VERSIONING.md](../../VERSIONING.md)）。
