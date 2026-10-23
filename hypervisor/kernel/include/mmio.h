/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_MMIO_H
#define ESHINE_MMIO_H

#include "kernel/include/types.h"

struct vcpu;

/* 27.04 第六站:仿真设备 MMIO 分发。
 * data abort(EL1)入口:ESR 解码 -> HPFAR=IPA -> 按 EMULATED 设备窗口
 * 分发;无窗命中/无寄存器现场 = guest fault(设备层默认拒绝)。 */
void mmio_dispatch(struct vcpu *v, u64 esr) __attribute__((noreturn));

#endif /* ESHINE_MMIO_H */
