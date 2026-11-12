<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Review: 发布就绪度第一方复评（glm-5.3）

- Date: 2026-11-11
- Reviewer: glm-5.3（harness 上报 `fangzhou/glm-5.3`）
- Model: glm-5.3
- Scope: 工作树 @ `7ab9c23`（含三处未提交改动：`.gitignore` 根锚定修复、
  deepseek 的 `docs/release/2026-11-11-release-readiness-assessment.md`、
  qwen 的 `docs/reviews/2026-11-11-qwen38max-release-readiness.md`）；
  外加本日三轮门禁实测（r1/r2/r3，取证日志已归档 `.work/logs/`）。
- Independence: **第一方自族评审**。GLM-5.3 是 68 站开发的主模型
  （`docs/state/METRICS.md`: ~94% 事件），本件评审的是自家代码——按
  `docs/MODEL-ROUTING.md` T3 与 trailer 真实语义纪律，这不是独立评审。
  跨族权重由 qwen 件承担，宽基线证据由 deepseek 件承担；本件的独有
  价值是**一手历史定谳 + 当前 HEAD 的新活体取证**。附注：模型标识
  来自 harness 上报，端点真实提供方未独立验证（同 qwen 件纪律）。

## Verdict

**Technology Preview: pass-with-notes。正式 `v27.04`: fail。三轮结论一致。**

本轮的三点增量：

1. **P0-1 的一手根因**（§2）：不是「忘了跑」，是站 43-48 批令
   「写而不跑」的政策性欠账，且该批令指定的「站 48 后统一验收两连」
   从未发生。
2. **P0-2 从「已知挂账」升级为「当前 HEAD 实证必死」**（§3）：门
   `[37]` 今日 2/2 复跑必死、panic 签名逐字节相同（cpu1 任务
   `ipimain` 跳入毒化地址 0xa5a5a5…），而 `[36]` 双核 demo 两轮全绿
   ——污染是 ipidemo 路径特异，非 SMP 面整体塌方。缺陷预算条款
   （P0 未闭不开新站）第一次有了实测硬数据。
3. **环境与工具面三项新发现**（G-2/G-4/G-5）：孤儿 QEMU 泄漏路径、
   法医 dump 缺宿主负载快照、face-C 空格洪水与 `[19]` 红脸共现。

## 1. 本轮实测（非引用前两轮）

| 轮次 | 命令 | 结果 |
|---|---|---|
| r1 | `verify_el2.py --stop-after=132` | 门 1-18 绿 → **门 [19] 60s 超时红**（等 `\r\nrtos> `）；boot 尾全健康（log core mods=3、cpu1 online、pm bound、inittab 全走、musl hello）；伴随 face-C 空格洪水（8 条 >200 字符行、`[vm0]`×2673）；站 25/28 自动取证开火，gdb 读在脏窗下降级（packet error） |
| r2 | `verify_el2.py --stop-after=132` | 门 1-36 全绿（**[19] 本轮绿 = r1 红属负载瞬态**）→ **门 [37] 120s 超时红**：等 `ipi: sleeper done`；vm0 打印 `rtos panic: unhandled sync exception / task: ipimain (cpu 0x1) / esr=0x86000004 elr=0xa5a5a5a5a5a5a5a5 far=0xa5a5a5a5a5a5a5a5`，calltrace `rt_panic_fault+0xdc ← rt_sync_fault+0x28 ← guest_sync+0x50`；ipidemo 只跑到 `rx1=2 / rx0=1 roundtrips` |
| r3 | `verify_el2.py --stop-after=37` | 门 1-36 全绿 → **门 [37] 同签名逐字节复死**（同 task/esr/elr/far/calltrace/rx 计数）。**[37] = 2/2 必死（今日窗口）**；`[38]` 及以后未触达（顺序套件止步于此） |

其他重验（本轮独立跑）：仓库规模 221 commits/675 files、
SPDX 132/132、产品代码堆分配 0 处、门号 187 打印点/183 唯一
（`[105]`×2 `[106]`×4）、发布 tar 内容清单（9 文件、无 vm2 镜像）、
`docs/devlog`（28 旧+21 新）/`docs/devlogs`（22）分段、
`hypervisor/kernel/vm.c` 的 68I 迹环锁形态（DAIF 屏原子锁、
owner/nest 全删）与 `console.c` 同构预防（保留递归、屏蔽取锁窗）——
**devlog 声明的修真形态与代码一致**。

r1 红脸的环境窗口（诚实记录）：启动时宿主负载 ~8.35，其中两台
孤儿 QEMU 各 ~399% CPU；r1 的门 `[19]` 窗口内恰有一台（已驻留
4h49m）死亡——负载瞬态与红脸同时刻。r2/r3 在较干净窗（~5-6.6，
剩一台孤儿）下 `[19]` 均绿。按 KNOWN-ISSUES §7 顺序，r1 判负载脸、
机制未定谳；r2/r3 的 `[37]` 判机制面（签名确定性，见 §3）。

## 2. P0-1 的一手定谳：批令欠账，不是遗忘

devlog 证据链（全部 tracked 文件）：

- **最后一次套件级两连 = 112 门纪元**：站 40 收口
  （`docs/devlogs/2026-11-01-station40-services-busybox.md:148`）
  「收口=三轮连掷 r9+r10 两连 RESULT PASS(112 计=…)」。
- **站 43-48 批令**（`docs/release/2026-11-02-batch-43-48.md`）：
  「门禁：123 → 133（append-only+顺移；**批令=写而不跑**，
  站 48 后统一验收两连）」；站 48 批收口 devlog 同句。
- **该「统一验收」从未发生**：站 49-68 再加 50 门（133→183），
  站 68 devlog（2026-11-06）仍写「全量 183 门两连（收口判据）=
  本站 DoD」——悬空至今；`PROJECT-STATE.md` Next action 第 1 条
  还是它。

结论：P0-1 = **政策性欠账**（「写而不跑」批令把验收无限后置）×
**结构性障碍**（qwen 件 F-2：全量必经 Yocto 重构建链，CI 不可达）。
两份前报告说的「缺两连记录」是对的，但根因链本轮补全。
71 个新门（站 41-68 时期）从未随套件整体绿过——这也解释了为什么
`[37]` 的必死能潜伏到今天才被活捕：**快环 `--stop-after=36` 是在
11-06 批次为绕开已知族而设的，CI 只跑快环，等于 CI 对 37..183
结构性失明**。

## 3. P0-2 实证升级：[37] 当前 HEAD 必死（已知族签名）

本轮三跑给出今日窗口的硬数据：

- `[19]`：3 跑 1 红 2 绿——负载敏感脸（与站 25/33 记档一致），
  且**在 CI 快环覆盖范围内**（G-3）。
- `[36]`（smpdemo 双核计数交错）：2/2 绿——次核上线、双核调度、
  FP 帧均正常。
- `[37]`（ipidemo SGI 往返）：2/2 必死，签名逐字节相同：
  cpu1 的 EL0 任务 `ipimain` 跳入毒化地址（`elr=far=0xa5a5a5…`，
  0xa5 = 毒化填充），ESR 0x86000004 = EC 0x21（来自低异常级的指令
  中止，translation fault）→ panic 链 `guest_sync→rt_sync_fault→
  rt_panic_fault` 全帧符号化干净。ipidemo 死前 `rx1=2、rx0=1`。

判读（第一方视角）：签名与 KNOWN-ISSUES §1 的「深层污染族」
（sp=0xC / self-switch next==old / cpu1 六拍落在 ipimain 栈域）
同族——**毒化跳转发生在 ipidemo 的跨核路径，smpdemo 纯计算路径
不受累**，这把污染面收窄到 SGI 往返/唤醒链一侧。两轮签名完全相同
说明这不是负载竞赛的随机产物，而是当前 HEAD 上的确定性缺陷
（是否 68 收口后引入回归，需用户核对上次 `[37]` 绿的记录；本件
只报实测）。取证日志：
`.work/logs/verify132-r1-gate19-red-20261111.log`、
`.work/logs/verify132-r2-gate37-red-20261111.log`、
`.work/logs/verify37-r3-gate37-red-20261111.log`。

**对发布的直接含义**：`[37]` 必死 ⟹ 当前 HEAD 永远出不了全量绿
⟹ P0-1 的两连永远做不出来——**P0-2 现在是 P0-1 的前置**。
缺陷预算条款（存在 P0 不开新站）在此第一次有实测依据：下一站
应当是 `[37]` 修复专站（复现器与取证已备齐）。

## 4. 对前两份报告的复核

| 前件条目 | 复核结论 | 本轮依据 |
|---|---|---|
| deepseek P0-1（缺全量两连） | **CONFIRMED + 根因补全** | §2 |
| deepseek P0-2（[37]/[38]+bsh 未闭） | **CONFIRMED + 实证升级为必死** | §3（bsh 面本轮未触达，维持原记档） |
| deepseek P1-1（CI 只跑快环） | **CONFIRMED + 加重**：快环对 37..183 结构性失明，且 `[19]` 脸就在快环内 | §1/§3 |
| deepseek P1-2~P1-6、P2-1~P2-6 | **CONFIRMED**（本轮抽验：tar 清单、devlog 分段、状态块陈旧、`.gitignore` 修复正确） | §1 |
| qwen F-1（入仓 demo 二进制静默陈旧路径） | **CONFIRMED**（作者族补充：这是已知陷阱家族——站 31 「幽灵二进制」与批末 verify 的 busybox baked 哈希过期同种；README 已文档化重建命令但无机器强制，维持 major） | §1 |
| qwen F-2（发布判据绑 Yocto 重链） | **CONFIRMED + 政策根因** | §2 |
| qwen F-3~F-5（tar 作用域/dtb 回退/拼写） | **CONFIRMED** | §1（tar 清单直验） |
| qwen P1-3 降级（本机字节可复现） | **同意降级** | r2/r3 门 1-36 全绿 = 当前产物功能面复核 |

无 REFUTED。三轮无事实冲突——这是三个模型族在同一 HEAD 上的
收敛，本身就是证据链可信度的加分项。

## 5. 新增发现（本轮独有）

| Severity | File:line / 位置 | Summary | Status |
|---|---|---|---|
| major | 门 `[37]`（`verify_el2.py:918`） | 见 §3：当前 HEAD 必死、签名确定、ipidemo 路径特异（`[36]` 绿）。**建议立 `[37]` 修复专站为下一站**（缺陷预算条款）。 | captured |
| major | `scripts/run_qemu.sh:32`（`exec` 形） | **非 tty 场景 `make run` 泄漏孤儿 QEMU**：exec 无 reap，父 shell 亡后 QEMU 存活。实测两台孤儿（20h31m 静默标本，疑似 boot 挂脸族活体，~82 核时已烧；+4h49m 噪声标本在 r1 窗口内自然死亡——死 pty 写满 EIO 假说）。建议：非交互冒烟用 `timeout` 包装；加一键 stray 清理助手；**20h 标本先按 68I 冻尸工具箱解剖再清**（boot 挂脸族挂账案件的一手活体）。 | captured |
| minor | `.github/workflows/ci.yml:41,65` | **`[19]` 负载脸在 CI 快环覆盖内**（今日 3 跑 1 红）：建议 CI 快环带「标注式单次重试 + 红红才红」的薄片计数，或快环两跑双绿制——与项目两连纪律同律。 | proposed |
| minor | `tools/verify/verify_el2.py`（法医 dump 头部） | **法医 dump 无宿主环境快照**（无 loadavg/孤儿 QEMU 计数/git describe）——而分诊顺序第一条就是宿主负载。建议法医头部加一行环境快照。 | proposed |
| minor | r1 取证 | **face-C 空格洪水与 `[19]` 红脸同靴共现**（8 条 >200 字符行；单样本，无因果主张）。68C 记账里「新绊线待自首」的绊线值得先落，让洪水面自首。 | captured |

## 6. 发布路径增量（对 deepseek §7 八条 + qwen 9/10 条）

11. **`[37]` 修复专站**：复现器（本件三轮取证）+ 既有守卫 + 毒化跳转
    单一根因；修完 `[38]` 同族连验。这是 v27.04 全量绿的前置。
12. **`make run` 非交互卫生**：timeout 包装 + stray 助手；20h 标本
    先解剖后清。
13. **法医环境快照**（G-4）：让每次门红的第一行证据自带环境事实。
14. **批令还账**：站 49-68 的「统一验收两连」作为独立收口动作立项
    （spec + 安静窗两连记录入档），把 P0-1 的欠账变成有史可查的
    闭环；Yocto 解耦（qwen 10）是其可执行前提。

## Tests run

```
python3 tools/verify/verify_el2.py --stop-after=132   # r1: [19] 红（负载瞬态窗）
python3 tools/verify/verify_el2.py --stop-after=132   # r2: 1-36 绿，[37] 红（毒化跳转）
python3 tools/verify/verify_el2.py --stop-after=37    # r3: 同签名复死 = 2/2
aarch64-linux-gnu-nm -n rtos.elf                     # calltrace 三帧符号化
git grep / wc -l / tar -tzf / ps / pgrep              # 规模·文档·标本复核
```

## Residual risks

- **本件是自族评审**：GLM-5.3 写了大多数站点，自评天然偏宽——独立
  交叉请依 qwen 件，宽基线依 deepseek 件；本件的可信部分是实测数据
  与 devlog 引用（可复核），不是评价性结论。
- 三轮门禁跑在非安静窗（孤儿 QEMU 存活、负载 5-8.4）：`[37]` 的
  确定性判断基于签名相同（两轮逐字节），彻底定谳仍需安静窗复跑；
  `[19]` 的负载归因是时序推断（瞬态同时刻），非受控实验。
- `[38]` 及 39..183、Yocto 门、bsh 面、真板、安全审计：未触达。
- 端点模型标识为 harness 上报，未独立验证。
- 三份评估同日同 HEAD；提交后需按新 HEAD 重跑关键面。
