<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 已知问题与边界 / Known issues and scope

更新 / Updated: 2026-11-24

本仓处于快速迭代期。以下问题已知、已记录，按优先级持续收敛；门禁全绿不等于没有残余缺陷。

This repository iterates quickly. The issues below are known and tracked;
a fully green gate run does not imply the absence of residual defects.

## 1. SMP demo 深层污染族

- 门禁 `[37]`/`[38]`（双核 demo）存在深层污染面，已有守卫与全息取证，但根因尚未完全收敛。
- 2026-11-11 三轮实测升级：门 `[37]` 为签名确定性必死（2/2）而非偶发；root-a（IRQ 出口 requeue 池外 idle）已定谳修真（见 ARCHITECTURE.md 站 68B）。
- root-b 第五击（dead-slot live-stack guard，commit `1ba00dc`）落地后，独立 fixture 套件下 `[37]`/`[38]` 均已通过（含 2026-11-18 三分片 CI 173/173 全绿，其中本地高负载曾出现 attempt 2/3 的 `FLAKY`），不再阻塞下游。
- 现象线索：`sp=0xC`、self-switch `next==old`、CPU1 六拍落在 `ipimain` 栈域。故障族未宣告关闭；安静窗两连绿之前按挂账处理。

## 2. 交互 shell 空格洪水

- `bsh`（busybox ash REPL）场景下约 1/3 的运行会出现空格洪水残面；已有 `wvtrip: all-space chunk` 绊线兜底重试，根因尚未闭合。

## 3. 门禁耗时、隔离门与宿主负载敏感

- 独立 fixture 套件下每门自起 QEMU（普通门）或一个场景 fixture（状态型家族），支持 3 分片并行执行：GitHub Actions 实测 **403 s** 完成全部 **173/173** 绿门（2026-11-18，run `44265186558`，含静态检查、工件缓存与容器冒烟；本地批处理约 6–7 分钟）。
- 当前 `tools/verify/quarantine.txt` 共跟踪 **4** 道隔离门（`[122]`、`[135]`、`[140]`、`[182]`）；前期 `[103]`/`[141]`/`[172]`/`[173]` 已随 `f8a9d5b`（pick-to-switch 原子窗口修真）解除隔离并回归绿灯。
- 已知 boot 家族（zero-byte chardev、全空格洪水、RX 唤醒丢字节、VM0 init 停在 `srv: pm bound stdio` 之后不再打印 `init: inittab processed`）由 fixture 级重试兜底，重试后过的门标 `FLAKY`，不隐藏。部分门（futex/APS 时序窗）对宿主负载敏感。
- 结论纪律：门禁失败先查宿主负载与并行任务（含残留 QEMU），再谈机制；两连真绿需在安静窗复跑。

## 4. 仅 QEMU virt 验证与 QEMU 版本锁

- 当前全部判据在 **QEMU 8.2.2** `virt`（ARM64，Ubuntu 24.04）上执行；真板启动链、时钟、GIC 实现差异尚未验证。
- Ubuntu 26.04 搭载的 QEMU 10.2.1 在 EL2 Stage-1 MMU 关闭状态下会触发 `ESR_EL2=0x96000061` 对齐异常，因此 CI runner 暂锁定 `ubuntu-24.04`（待开启 EL2 Stage-1 MMU 后解耦）。
- FVP 参考平台不在本仓发布物中。

## 5. v0 安全边界

- 安全定位与 ASIL / CAL 分级均是架构设计目标，尚未启动正式第三方认证。
- 当前隔离手段：EL2 stage-2、per-VM 地址空间、fail-closed 的跨 VM 共享窗授权。
- 待加固项与尚未包含能力：EL2 Stage-1 MMU、严格的用户态 `access_ok` 与异常表拷贝保护、页表 W^X 权限拆分、跨核 `tlbi vmalle1is`、64 位自旋锁位宽统一、PAN/SMAP、KASLR、栈保护（freestanding 构建按设计关闭）、形式化验证、模糊测试。

## 6. 维护结构

- 单维护者 + AI 结对开发；外部评审与贡献流程在持续完善中。
- 审计、复现与门禁记录优先于功能扩张。

## 7. 缺陷预算与稳定化窗口

- **缺陷预算**：存在 P0（启动/全量门禁不可复现、数据损坏、跨 VM 隔离失效）时不开新功能站；先修复或明确降级。
- **稳定化窗口**：每批站点收口前，在安静窗口跑全量绿套件两连；宿主负载、并行会话、残留 QEMU 进程先清场，再谈机制。
- **快环/全量分立**：迭代用 `verify --stop-after=36`（PARTIAL-PASS，止于已知 `[37]`/`[38]` 族之前，不是真绿）；收口必须全量两连。`--stop-after` 不得作为发布判据。
- **门禁插队规则**：新门改变世界状态时，下游旧门的前提可能失效；插门后必须跑全量，不能只跑新门。
- **失败分诊顺序**：宿主负载/陈旧二进制 → 探针/解析器 → 世界状态假设 → 被测机制 → 规格歧义。被证伪的假说记入交接，避免重走。
