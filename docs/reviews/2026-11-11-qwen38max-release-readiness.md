<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Review: 发布就绪度跨族复评（qwen3.8-max）

> ⚠️ 本文引用的提交 SHA 与当前公开历史不对应（已不可解析）；
> 请以文中描述与日期为准。

- Date: 2026-11-11
- Reviewer: qwen3.8-max（harness 上报 `bailian/qwen3.8-max`）
- Model: qwen3.8-max
- Scope: 工作树 @ `7ab9c23`（含两处未提交改动：`.gitignore` 根锚定修复、
  `docs/release/2026-11-11-release-readiness-assessment.md`）；同时逐条
  复核该评估报告（作者模型 `deepseek-v4.1-flash`）的每一项结论。
- Independence: **跨模型族**。本评审模型（Qwen 族）与前一份评估的作者
  模型（DeepSeek 族）、与绝大多数站点的主模型（GLM-5.3）均不同族，
  满足 `docs/MODEL-ROUTING.md` T3「评审模型须与作者不同族」的要求。
  诚实附注：模型族标识来自 harness 上报，我无法独立验证端点背后的真实
  提供方；若该端点实为同族代理，本条独立性声明即不成立（本项目已有
  「代理冒充实为同族」的先例，故必须写明）。
- 纪律：本件**不修改**前一份报告，只做复核与补充；两份并存即为证据。

## Verdict

**Technology Preview：pass-with-notes。正式 `v27.04`：fail（与前一份报告一致）。**

两处权重调整：

1. **构建可复现性比前一份报告评估的更强**，其 P1-3 应从「发布阻断候选」
   降为「跨机漂移防护」：本机两次 `make -B` 产出**字节完全一致**，且与
   `make-release.sh` 产物 manifest 的 4 个 sha256 **逐一相符**；入仓 dtb 与
   `dtc 1.7.0` 重建字节一致；7 个入仓 demo 二进制被发布脚本重建后
   `git status` 仍干净 = 与入仓字节一致。真实风险面是**跨主机/跨时间的
   工具链漂移**（apt 未锁版本），不是「当前产物不可复现」。
2. **新增一项前一份报告未覆盖、且直接咬到旗舰判据的缺口**（F-1：入仓
   demo 二进制的静默陈旧路径）。它不改变总体结论，但应进入发布前必修
   清单——因为它能让门 `[77]/[88]`（双 musl 世界）和发布自靴冒烟在
   **源码已改、二进制未重建**的情况下继续报绿。

## 独立复跑证据（本机实测，非引用前一份报告）

| 检查 | 命令 | 本次结果 |
|---|---|---|
| 干净重建 ×2 | `make -B -j$(nproc)`（两次） | 两次 rc=0，`warning:`/`error:` 各 **0**；两次产物 sha256 **完全一致** |
| 与发布件对账 | `sha256sum` vs `release/.../manifest.json` | `eshine.elf`/`rtos.bin`/`guest2.bin`/`ramfs.cpio` **四件全等** |
| 快环 | `make verify-fast` | `RESULT PARTIAL-PASS gates 1..36`，36 行门判据 |
| SBOM | `python3 tools/sbom-check.py` | `ALL GREEN`（8 组件 / 3 in-repo pin / 5 GPL 隔离） |
| 隔离 | `make check-isolation` | `host-independence: OK` |
| Trailer | `scripts/check-commit-msg.py --range d2a5d0c^..HEAD` | `range ok (7 commits)` |
| demo 二进制 | `make demos` + 前后 sha256 对账 | 无漂移；发布脚本 10:28 重建的 7 件与入仓字节一致（`git status` 干净） |
| dtb | `make -B`（dtc 1.7.0 在场） | 重建后 `git diff -- tools/dtb/` 为空 = 入仓 blob 与 `.dts` 同步 |
| 规模 | `git ls-files` / `wc -l` | 221 commits、675 tracked files；hypervisor 6,084 + rtos 16,860 + common 617 行；verifier 2,984 行；docs 27,409 行 |
| 确定性 | `grep -E '\b(malloc\|calloc\|realloc\|free)\s*\('` | 产品代码（hypervisor/rtos/common/guest）**0 处** |
| SPDX | 132 个构建/源文件扫描 | **132/132** |
| 门号 | `[N/183]` 打印点统计 | 187 个打印点 / 183 个唯一门号；`[105]`×2、`[106]`×4 |

未执行（因此不构成结论依据）：全量 183 门、Yocto 门 `[133]/[177]`
（`.work` 内无 `poky/`、`yocto-build/`）、容器从零网络重建、真实板卡、
安全审计、逐行 C/asm 正确性审计。

## 对前一份报告的逐条复核

| 前报告条目 | 复核结论 | 依据 |
|---|---|---|
| P0-1 当前 HEAD 无全量两连真绿 | **CONFIRMED** | `PROJECT-STATE.md` Next action 第 1 条；本次亦未跑全量；`.work` 无 Yocto 树 |
| P0-2 `[37]/[38]` + bsh 残面未闭 | **CONFIRMED** | `KNOWN-ISSUES.md` §1/§2；快环设计止于 36 |
| P1-1 CI 只跑 `--stop-after=36` | **CONFIRMED** | `.github/workflows/ci.yml:41,65`；无全量/发布/SBOM/Yocto job |
| P1-2 发布件不封闭 | **CONFIRMED + 补强** | `make-release.sh` 无脏树守卫、无版本守卫、manifest 无 commit/工具链；**且与 `scripts/archive-source.sh` 的 `git archive HEAD` 纪律自相矛盾**——Yocto 路径吃提交树，发布 tar 路径吃工作树 |
| P1-3 工具链 pin 强于事实 | **CONFIRMED（事实）/ 建议降级** | `Dockerfile.build:7` `FROM ubuntu:24.04` 无 digest、apt 不锁版；CI `ubuntu-latest` + `actions/checkout@v4` 非 SHA pin。但本机产物已证字节可复现（见上表），风险是跨机漂移而非当前不可复现 |
| P1-4 无 SECURITY/CLA/CoC | **CONFIRMED** | 全树查找无 `SECURITY*`/`CLA*`/`CODE_OF_CONDUCT*`/`CODEOWNERS`/`dependabot*` |
| P1-5 全新克隆全量非一条命令 | **CONFIRMED** | `scripts/bootstrap.sh:18,62,140,166,189`——`WITH_YOCTO` 默认 0，`make bootstrap` 走 `--all` 不带该旗 |
| P1-6 `.gitignore` 的 `release/` 吞掉 `docs/release/` | **CONFIRMED，修复正确** | 改后 `git check-ignore -v release/...tar.gz` 仍命中 `/release/`，而 `docs/release/` 新文件已出现在 `git status` |
| P2-1 状态块陈旧 | **CONFIRMED** | 生成块 `HEAD: 3db2a2f`，实际 `7ab9c23`；`scripts/update-state.sh:33` 硬编码 `Dev tags: 26.08-1w .. 26.11-2w` |
| P2-2 双 devlog 目录 | **CONFIRMED** | `docs/devlog/` 49 篇、`docs/devlogs/` 22 篇 |
| P2-3 文档内宿主路径 | **CONFIRMED + 机制补强** | 3 处 `~/projects/eshine-linux`；**根因是扫描面**：`check-host-independence.sh` 只扫 `Makefile\|scripts/\|tools/\|meta-eshine/\|docker/\|.github/`，`docs/`、`README.md`、`ARCHITECTURE.md`、`3rd/` 全不在内，所以 CI 永远看不见 |
| P2-4 门号重号 | **CONFIRMED** | 187 打印点 / 183 唯一号 |
| P2-5 同族评审 | **本件即为跨族补位** | 见 Independence（含端点 provenance 附注） |
| P2-6 QEMU-only / 非硬实时证据 | **CONFIRMED** | `docs/benchmark/wake-latency-baseline.md` 自陈 TCG、100Hz、无亚拍唤醒 |
| 「优点」各项（真系统/验证纪律/确定性设计/合规面/发布件自靴） | **独立复验通过** | 见上表；`[77]/[88]` 双 musl 世界门确在 verifier（`verify_el2.py:1537,1652`） |

无 REFUTED 项。前一份报告的事实陈述在本次复核中全部站得住。

## 新增发现（前一份报告未覆盖）

| Severity | File:line | Summary | Status |
|---|---|---|---|
| major | `Makefile:123-128,162-172` | **入仓 demo 二进制的静默陈旧路径**：`tools/ramfs/bin/` 有 9 个入仓二进制，Makefile 只为 4 个（`jsontest`/`ztest`/`cyclictest`/`coreparse`）提供规则；另 5 个（`hello`/`beat`/`initmark`/`pt`/`sockdemo`）只能由 `tools/build-musl.sh` 重建。`RAMFS_INPUTS` 把这 5 个列为 `ramfs.cpio` 的先决条件却无规则 → `make`/`make verify`/CI 永远视其为最新。实测 `touch tools/ramfs-hello.c && make -n` = 「无需做任何事」。后果：改了 `tools/ramfs-hello.c` 而未跑 `build-musl.sh`，门 `[77]/[88]`（README 旗舰判据「双 musl 世界」）与 `make-release.sh` 的自靴冒烟会**继续用旧字节报绿**——判据绿 ≠ 源码绿。**当前无漂移**（重建字节与入仓一致），属潜在而非现存缺陷。 | proposed |
| major | `tools/verify/verify_el2.py:2198`、`scripts/yocto-build.sh:16-20` | **发布判据被绑在重构建链上**：全量的 `[133]/[177]` 需要 poky 克隆 + bitbake，CI 无此路径、本机 `.work` 亦无。等于「正式发布判据」中有一段在常规环境里跑不到，只能靠维护者单机手动执行——这正是 P0-1 长期悬空的结构性原因。建议把 Yocto 门拆为独立 profile/job，让「全量减 Yocto」能在 CI 常态化跑。 | proposed |
| minor | `tools/ramfs/README.md:27` | **规范类文档与 11-10 隔离批次冲突**：仍写「`make demos` expects the cJSON and zlib builds under `/tmp`」，而 Makefile 实际用 `$ESHINE_WORK/{cjson,zlib}-build`，且 `check-host-independence.sh` 明确禁止 `/tmp/(musl\|cjson\|zlib\|busybox)-build`。历史 devlog 里的 `/tmp` 属存档可保留，但本文件是操作指引，应改。 | proposed |
| minor | `Makefile:55-64` | **入仓 dtb 陈旧面（同源问题，风险更低）**：无 `dtc` 时回退入仓 blob 且只打 WARNING，无门禁校验 blob 与 `.dts` 一致。本机 dtc 1.7.0 在场、重建字节一致，CI 与容器都装 dtc，故实际暴露面小；建议加一道哈希对账门（与 F-1 同一机制）。 | proposed |
| minor | `scripts/make-release.sh`、`release/*/manifest.json` | **发布件作用域需在对外文案写明**：tar 只含 vm0 侧四件（GPL 隔离，正确），自靴冒烟只验 `run /bin/hello`；真实 Linux 客（vm2）不在发布件内。这不是缺陷，但对外若说「解包即靴」，必须同时说明「双世界中的 Linux 侧需自备 Image/initramfs」，否则读者会高估发布件范围。 | proposed |
| minor | `scripts/bootstrap.sh:190` | 错误信息拼写：`(ynocto)` 应为 `(yocto)`。 | proposed |

### F-1 的建议修法（择一，按代价排序）

1. **最便宜**：`make verify` / CI 前置一道新鲜度预检——任一
   `tools/ramfs-*.c` 的 mtime 新于对应 `tools/ramfs/bin/*` 即 FAIL，
   并打印该跑哪条重建命令（`tools/ramfs/README.md` 的表已是事实源）。
2. **更彻底**：给 5 个 musl 件补 Makefile 规则，musl 依赖用 order-only
   或「工作根缺失则跳过并 WARN」的开关，保持「干净 checkout 无需 musl
   即可构建」的既有设计意图。
3. **门禁化**：新增一道 append-only 门，实算 `hello` 的 sha256 并与
   入仓字节对账（本项目已有「baked 哈希过期→宿主树实算」的先例可循）。

## 发布门槛清单（对前报告 §7 的增量）

前报告 §7 的 8 条我全部同意，不重复。追加两条：

9. 关闭 F-1：让「入仓 demo 二进制 vs 其源码」的新鲜度由机器保证，
   而不是由维护者记得跑 `tools/build-musl.sh`。
10. 关闭 F-2（结构性）：把 Yocto 门与核心发布判据解耦，使「全量两连」
    在 CI 里可常态化执行——否则 P0-1 会反复出现。

## Tests run

```
git rev-parse --short HEAD / git rev-list --count HEAD / git describe
make -B -j$(nproc)                      # 两次；0 warning；产物 sha256 两次一致
sha256sum eshine.elf rtos.bin guest2.bin rtos/ramfs.cpio   # 与 release manifest 四件全等
make verify-fast                        # PARTIAL-PASS gates 1..36
python3 tools/sbom-check.py             # ALL GREEN
make check-isolation                    # OK
python3 scripts/check-commit-msg.py --range d2a5d0c^..HEAD   # range ok (7)
make demos + sha256 前后对账             # 无漂移
git diff --stat -- tools/dtb/           # 空（dtb 与 dts 同步）
touch tools/ramfs-hello.c && make -n    # 「无需做任何事」= F-1 实证
git check-ignore -v release/... docs/release/...            # P1-6 修复验证
git grep（SPDX / malloc / 宿主路径 / /tmp / 治理文件）
```

副作用披露：为验证 F-1，对 `tools/ramfs-hello.c` 执行了 `touch`
（**仅 mtime，无内容改动**，`git status` 对该文件为空）；两次 `make -B`
与一次 `make demos` 重建了 gitignore 内的产物，tracked 文件除
`.gitignore`（前一份评估的修复）外无改动。

## Residual risks

- 全量 183 门、Yocto 门、容器从零重建、真板、安全审计均未执行；本件
  不构成对「系统功能正确」的独立背书，只构成对**发布就绪度与证据链**
  的跨族复核。
- 未做逐行 C/asm 审计；F-1 是构建系统层面的判据完整性问题，不等于
  已发现内核缺陷。
- 本机可复现 ≠ 跨机可复现：容器基础镜像与 apt 版本未锁，另一台机器
  或另一时间点的重建可能产出不同字节。此面未测。
- 独立性依赖 harness 上报的模型标识；端点真实提供方未独立验证。
- 两份评估同一天、同一 HEAD、同一工作树；若随后提交或改动，结论需
  按新 HEAD 重跑关键检查（尤其 F-1 与 P0-1）。