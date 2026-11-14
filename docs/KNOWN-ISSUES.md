<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 已知问题与边界 / Known issues and scope

更新 / Updated: 2026-11-14

本仓处于快速迭代期。以下问题已知、已记录，按优先级持续收敛；门禁全绿不等于没有残余缺陷。

This repository iterates quickly. The issues below are known and tracked;
a fully green gate run does not imply the absence of residual defects.

## 1. SMP demo 深层污染族

- 门禁 `[37]`/`[38]`（双核 demo）存在深层污染面，已有守卫与全息取证，但根因尚未完全收敛。
- 2026-11-11 三轮实测升级：门 `[37]` 为签名确定性必死（2/2）而非偶发；root-a（IRQ 出口 requeue 池外 idle）已定谳修真（见 ARCHITECTURE.md 站 68B），root-b/c 武装待续。
- 现象线索：`sp=0xC`、self-switch `next==old`、CPU1 六拍落在 `ipimain` 栈域。

## 2. 交互 shell 空格洪水

- `bsh`（busybox ash REPL）场景下约 1/3 的运行会出现空格洪水残面；已有绊线，尚未自首。

## 3. 门禁耗时与宿主负载敏感

- 全量 183 门在当前宿主机负载下单轮可超过 30 分钟；部分门（futex/APS 时序窗）对宿主负载敏感。
- 结论纪律：门禁失败先查宿主负载与并行任务，再谈机制；两连真绿需在安静窗复跑。

## 4. 仅 QEMU virt 验证

- 当前全部判据在 QEMU virt（ARM64）上执行；真板启动链、时钟、GIC 实现差异尚未验证。
- FVP 参考平台不在本仓发布物中。

## 5. v0 安全边界

- 安全定位是路线图目标，不是当前完成态。
- 当前隔离手段：EL2 stage-2、per-VM 地址空间、fail-closed 的跨 VM 共享窗授权。
- 尚未包含：PAN/SMAP（目标 CPU 不含 PAN）、KASLR、栈保护（freestanding 构建按设计关闭）、形式化验证、模糊测试。

## 6. 维护结构

- 单维护者 + AI 结对开发；外部评审与贡献流程（CLA）在建立中。
- 审计、复现与门禁记录优先于功能扩张。

## 7. 缺陷预算与稳定化窗口

- **缺陷预算**：存在 P0（启动/全量门禁不可复现、数据损坏、跨 VM
  隔离失效）时不开新功能站；先修复或明确降级。
- **稳定化窗口**：每批站点收口前，在安静窗口跑全量 183 门两连；
  宿主负载、并行会话、残留 QEMU 进程先清场，再谈机制。
- **快环/全量分立**：迭代用 `verify --stop-after=36`（PARTIAL-PASS，
  止于已知 [37]/[38] 族之前，不是真绿）；收口必须全量两连。
  `--stop-after` 不得作为发布判据。
- **门禁插队规则**：新门改变世界状态时，下游旧门的前提可能失效；
  插门后必须跑全量，不能只跑新门。
- **失败分诊顺序**：宿主负载/陈旧二进制 → 探针/解析器 → 世界状态
  假设 → 被测机制 → 规格歧义。被证伪的假说记入交接，避免重走。
