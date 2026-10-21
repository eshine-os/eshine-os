<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Devlog 00 — 第一次点亮 EL2 / First light at EL2

**2026-10-21 · Week 1**

从零开始的第一周：eshine-os 在 QEMU virt 上直接以 EL2 起动，
串口打出 banner，一个带行编辑的 mini-shell 可交互，VM 状态机
以"默认拒绝"哲学先行落位。真绿门禁（token 双次回显）全程护航，
第一晚就绿了。

Week one from zero: eshine-os boots straight into EL2 on QEMU virt,
prints its banner over PL011, runs an interactive line-edited shell,
and lands the VM state machine with a default-deny philosophy. The
true-green gate (token echoed twice) guards every step - green on
night one.

下一步 / Next: stage2 页表与第一个 guest 地址空间。
