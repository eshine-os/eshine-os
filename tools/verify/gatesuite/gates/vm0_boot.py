# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""VM0 RTOS boot-stream gates 9..18 (one scenario fixture)."""

from __future__ import annotations

from ..registry import scenario, scenario_gate

_NAMES = {
    9: "vmrun 0 async -> rtos banner via vuart ([vm0] prefix)",
    10: "fdt self-check: cpu node (arm,cortex-a57)",
    11: "fdt self-check: uart node (arm,pl011)",
    12: "fdt self-check: blk node (virtio,mmio)",
    13: "pl011 amba id 0x00141011 (PERIPHID/PCELLID probe)",
    14: "gicd v3 probe via emulated GICD (PIDR2 == 0x30)",
    15: "gic enabled via GICD/GICR (ppi27 + spi33/35 readback)",
    16: "guest enabled RXIM -> uart irq mode (vSPI 33 path)",
    17: "virtio probe + DRIVER_OK -> virtio blk on (vSPI 35 path)",
    18: "rtos up: 32-prio bitmap scheduler, tick 100Hz",
}

_ANCHORS = {
    9: b"[vm0] eshine-rtos",
    10: b"[vm0] fdt ok: cpu arm,cortex-a57",
    11: b"[vm0] fdt ok: uart arm,pl011",
    12: b"[vm0] fdt ok: blk virtio,mmio",
    13: b"[vm0] pl011 on: amba 00141011",
    14: b"[vm0] gicd v3 ok",
    15: b"[vm0] gic on: 27 33 35",
    16: b"[vm0] uart irq on",
    17: b"[vm0] virtio blk on",
    18: b"[vm0] rtos: up (eshine-rtos",
}

for _gid in range(9, 19):
    scenario_gate(_gid, _NAMES[_gid], "boot-vm0",
                  suites=("smoke", "green"))


@scenario("boot-vm0", "vm0boot", tuple(range(9, 19)))
def scenario_boot_vm0(ctx):
    s = ctx.s
    p = ctx.mark()
    s.send("vmcreate 0\n")
    s.expect("vm0: CREATED", 30.0, start=p)
    p = ctx.mark()
    s.send("vmrun 0\n")
    for gid in range(9, 19):
        anchor = _ANCHORS[gid]
        ctx.check(gid, _NAMES[gid],
                  lambda anchor=anchor: s.expect(anchor, 90.0, start=p))