<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 发布就绪度评估报告

- 评估日期：2026-11-11
- 评估对象：`eshine-os` @ `7ab9c23`（`VERSION` = `27.04-dev`）
- 评估人：Claude Code（模型 `deepseek-v4.1-flash`）
- 独立性：**同族 AI 评估，不是独立第三方审计**。评估模型与被评估提交的主模型同族，结论不等同于跨族评审或人类评审。
- 方法：读树 + 实跑构建/快环/发布件自靴/SBOM/隔离检查；未跑全量 183 门与 Yocto 门。
- 结论口径：**技术预览可发；公开 alpha 基本就绪；正式 `v27.04` 发布未就绪；车规/安全认证远未就绪。**

## 0. 结论摘要

这个仓库的真实性、工程纪律和证据链明显高于一般个人项目：EL2 Type-1
hypervisor、从零 RTOS、真实 Linux 6.6 guest、双 musl 世界、183 道可重放
门禁、70 个五件套内容包，都是实打实的交付面。今天实跑的干净重建零警告、
快环 1..36 全绿、发布 tar 自靴到 musl hello，也证明主干路径活着。

但按本仓自己的发布纪律，当前 HEAD 还不具备「正式发布」资格：

1. **当前 HEAD 没有全量 183 门两连真绿的记录**。`PROJECT-STATE.md`
   自己把这件事列为 Next action 第 1 条；2026-11-10 的评审也把
   「全量未跑」列为 residual risk。今天只验证了快环 1..36。
2. **已知挂门未闭**：`[37]`/`[38]` 双核 demo 深层污染族、`bsh`
   空格洪水（约 1/3）都在 `KNOWN-ISSUES.md` 挂着；快环恰好止于
   `[37]/[38]` 之前，所以「快环绿」不能代表发布质量。
3. **CI 只跑快环**：`.github/workflows/ci.yml` 两处都是
   `--stop-after=36`；没有全量、没有发布件构建、没有 SBOM 门、没有
   Yocto 路径。CI 绿 ≠ 发布绿。
4. **发布件不封闭**：`make-release.sh` 不检查工作区是否干净、不拒绝
   `-dev` 版本、不记录源码 commit、不记录工具链版本；今天产出的
   manifest 版本就是 `27.04-dev`，没有 git SHA。

因此建议：**可以按「技术预览 / 公开开发中」发布，不要按「生产可用 /
车规级 / 安全认证」发布。** 对外叙事应以路线图和已知边界为准，而不是
以「车载自动驾驶操作系统已就绪」为准。

## 1. 今天实跑的证据

| 检查 | 命令 | 结果 |
|---|---|---|
| 干净重建 | `make -B -j$(nproc)` | rc=0，`warning:`/`error:` 计数 **0** |
| 快环 | `make verify-fast` | `RESULT PARTIAL-PASS gates 1..36`；门计时合计 27.8s；宿主 load ≈ 8.18 |
| 发布件 | `bash scripts/make-release.sh` | `RELEASE OK`；解包 tar 后 QEMU 自靴到 `musl: hello from /bin/hello`；tar 954K |
| SBOM | `python3 tools/sbom-check.py` | 8 组件，3 in-repo pin ok，5 out-repo GPL 隔离；`ALL GREEN` |
| 隔离 | `make check-isolation` | `host-independence: OK` |
| 轨迹 | `git rev-list --count HEAD` | 221 commits；675 tracked files；仓库 18MB（不含 `.work`） |
| 标签 | `git tag` / `git describe` | `v26.10` annotated；HEAD = `26.11-2w-13-g7ab9c23` |

未跑、因此不能作为结论依据的面：

- 全量 183 门（`make verify`）与其两连真绿；
- Yocto 门 `[133]`/`[177]`（`.work` 当前没有 `poky/` 与 `yocto-build/`，
  `make bootstrap` 默认也不会拉取 Yocto）；
- 从零网络构建容器镜像、真实板卡、SMMU、安全审计、CLA流程；
- 逐行代码正确性审计（本报告是整体与发布就绪度评估，不是深度代码审计）。

## 2. 评分卡

| 维度 | 评级 | 判断依据 |
|---|---|---|
| 架构与实现 | **B+** | 6,084 行 hypervisor + 16,860 行 RTOS + 51 个 C 文件；真 EL2、真 stage-2、真 Linux guest；产品代码 0 处 `malloc/calloc/realloc/free`，固定池/静态表，确定性设计 |
| 验证体系 | **B+** | 183 个门号（2,984 行 verifier）；append-only、两连真绿、失败分诊顺序都写进了流程；但当前 HEAD 缺全量两连，[37]/[38] 与 bsh 残面未闭 |
| 构建与可复现 | **B** | kernel 风格 Makefile、零警告、Linux/busybox SHA pin、3rd tarball 入仓、容器路径、host-independence 检查；但基础镜像/apt/actions 未钉死，全量依赖 Yocto 外置树 |
| 发布工程 | **B-** | `make-release.sh` 能产出可解包自靴的 tar；但无脏树守卫、无源码 SHA、`-dev` 可被打包、无发布 CI/归档/签名 |
| 安全与合规 | **C+** | SPDX 132/132、SBOM、GPL 隔离、clean-room 规则、DCO 都扎实；但无 `SECURITY.md`、无 CLA文档、无外部安全审计/模糊测试，安全边界仍是路线图目标 |
| 文档与过程 | **A-** | 27,409 行文档 > 23,561 行产品代码；58 份 spec、49+22 篇 devlog、70 个五件套内容包、交接/路由/归因/站规齐全；两处目录与状态块有陈旧面 |
| 社区与治理 | **D+** | 单维护者 + AI；无 CoC、无 issue/PR 模板、CLA待办；bus factor 1；评审是同族 AI 自评 |
| 对外沟通真实性 | **B** | `KNOWN-ISSUES.md` 很诚实；但 README 的「车载自动驾驶 / 生命攸关」叙事容易被读成安全认证，需要主动降级为愿景 |

## 3. 阻断项（P0）

### P0-1 当前 HEAD 没有全量 183 门两连真绿

**证据**：`docs/state/PROJECT-STATE.md` 的 Next action 第 1 条要求
「full 183-gate suite twice in a quiet window and record both RESULT
lines」；2026-11-10 评审的 residual risks 明确写「Full 183-gate
double-green not run」；今天只跑了 `verify-fast`，得到
`PARTIAL-PASS gates 1..36`。

**影响**：按本仓 DoD，站点/批次都不能算关闭；对外不能声称「183 门
全绿」。这是正式发布的第一阻断项。

**建议**：

1. 安静窗内先 `scripts/setup-yocto.sh`（或 `bootstrap --with-yocto`），
   再跑两次 `make verify`，把两次完整 `RESULT PASS` 行记入
   `PROJECT-STATE.md` 与 release 注记。
2. 全量通过前，对外只使用「技术预览」口径。
3. 把「全量两连」做成 CI/发布脚本的硬门槛，而不是靠人记得。

### P0-2 已知挂门未闭：[37]/[38] 与 bsh 空格洪水

**证据**：`docs/KNOWN-ISSUES.md` §1（`[37]`/`[38]` 双核 demo 深层污染
族，根因未完全收敛）、§2（bsh 空格洪水约 1/3，有绊线未自首）；
`README.md` 与 `STATION-PLAYBOOK.md` 明确说快环止于已知
`[37]/[38]` 族之前。

**影响**：全量套件不是稳定的发布信号；默认开发路径恰好绕开已知
失败面，容易让「日常绿」掩盖发布风险。这不是新缺陷，但在发布决策上
必须先处置。

**建议**：

- 要么在发布前修复并跑全量两连；
- 要么由维护者签署正式豁免：写明影响门、复现率、对发布件的实际
  风险、后续站点计划，并把豁免写进 release notes；
- 两者都不做，就不应把 `v27.04` 标成正式发布。

## 4. 高优先级（P1）

### P1-1 CI 覆盖与发布信号不匹配

`.github/workflows/ci.yml` 只做：`make`、静态语法检查、trailer 校验、
host independence、`--stop-after=36`。没有：

- 全量 183 门（哪怕 nightly）；
- `scripts/make-release.sh` 与解包自靴；
- `tools/sbom-check.py`；
- Yocto `[133]/[177]` 路径；
- 发布件 artifact 上传/保留。

**建议**：增加 nightly full-suite job 与 tag/release job；发布 job 至少
执行「全量 ×2 → make-release → 解包自靴 → SBOM → 上传 artifact」，
任一失败即阻断 tag 发布。

### P1-2 发布件不封闭

今天的 `release/eshine-os-27.04-dev/manifest.json` 只有 `version`、
`machine`、四件 `sha256`；没有源码 commit、没有工具链版本/image
digest。`scripts/make-release.sh` 也没有：

- `git status --porcelain` 干净检查；
- 拒绝 `-dev` 版本或显式 `--allow-dev` 开关；
- 从 `git archive HEAD` 构建（当前直接吃工作区）；
- 发布 tar 归档/留存/签名。

**影响**：无法从发布件反查「这是哪次提交、哪套工具链、是否夹带未提交
改动」。对一个强调可复现与证据链的项目，这是发布工程的实质缺口。

**建议**：发布脚本先拒绝脏树与非发布版本；manifest 增加
`git_commit`、`toolchain`、`container_image_digest`；从 `git archive`
构建；把 tar 与 manifest 作为 CI artifact 保留。

### P1-3 「pinned toolchain」的说法强于事实

`docker/Dockerfile.build` 用 `FROM ubuntu:24.04`（无 digest pin），
`apt-get install` 不锁版本；CI 用 `ubuntu-latest`，`actions/checkout@v4`
也未按 commit SHA pin。容器确实解决了「宿主 apt 漂移」的一部分问题，
但没有解决「镜像/软件包版本漂移」。

**建议**：基础镜像按 digest pin；关键包版本锁定或在镜像内记录
`dpkg-query` 清单；GitHub Actions 按 SHA pin；把镜像 digest 写进发布
manifest 与 SBOM。

### P1-4 安全与社区治理文件缺失

仓内没有 `SECURITY.md`、CLA文档、CoC、issue/PR 模板。
`KNOWN-ISSUES.md` §5 已诚实列出安全边界：无 PAN/SMAP（目标 CPU 不含）、
无 KASLR、无栈保护（freestanding 按设计关闭）、无形式化验证、无模糊
测试；仅 QEMU virt 验证。

**影响**：对外发布一个「生命攸关」定位的 hypervisor，却没有漏洞披露
渠道与安全边界声明，是沟通与治理风险；接受外部贡献前没有 CLA
安排，是法律风险。

**建议**：发布前补 `SECURITY.md`（披露渠道、支持版本、响应时限）、
威胁模型、CLA状态页；增加 CoC 与 issue/PR 模板；把「无安全认证」
写进 README 与 release notes 的显眼位置。

### P1-5 全新克隆的全量验证不是一条命令

`make bootstrap` 走 `scripts/bootstrap.sh --all`，默认不拉 Yocto；
全量 `[133]/[177]` 需要 `scripts/setup-yocto.sh`（poky 浅克隆 + 外置
build 树）。README 已注明「needs .work artifacts + Yocto」，但快速
开始只写了 `make bootstrap`。

**建议**：提供 `make verify-full`（或 `bootstrap --with-yocto` 的
显式文档与错误提示），把「全新机器 → 全量两连」的路径做成一条可
复制命令；CI 的 release job 用同一条路径。

### P1-6 `docs/release/` 被 `.gitignore` 的 `release/` 规则吞掉

**证据**：`.gitignore` 的 `release/` 未加根锚定，`git check-ignore -v`
显示它同时匹配 `docs/release/`。评估过程中新建的
`docs/release/2026-11-11-release-readiness-assessment.md` 因此不出现在
`git status` 里；既有的 `docs/release/2026-11-02-batch-43-48.md` 只是
因为先被跟踪才幸存。

**影响**：未来任何 `docs/release/` 下的发布注记/评估都会静默丢失，
是一个直接咬人的发布工程陷阱。

**建议**：已在本评估中把规则改为 `/release/`（根锚定）；后续新增
发布文档时用 `git status` 验证可见性。该修复应与本报告同批提交。

## 5. 中低优先级（P2）

1. **状态块陈旧**：`PROJECT-STATE.md` 的 generated block 仍是
   `HEAD 3db2a2f`，实际 HEAD 是 `7ab9c23`；`scripts/update-state.sh`
   还硬编码了 `Dev tags: 26.08-1w .. 26.11-2w`。发布前跑一次更新，
   把 tag 范围改为派生值。
2. **两个 devlog 目录**：`docs/devlog/`（49 篇）与 `docs/devlogs/`
   （22 篇）并存，站段不同；外部读者难以判断权威序列。建议合并或
   在 README 写清「哪一段在哪个目录」。
3. **文档里的宿主路径**：`3rd/LEDGER.md`、
   `docs/specs/2026-10-24-linux-bringup-station-design.md`、
   `docs/specs/2026-10-26-initramfs-userland-station-design.md` 仍有
   `~/projects/eshine-linux` 形态；这是历史迁移说明，但与
   host-independent 的对外形象不一致。建议改成 `$ESHINE_WORK/...`
   或明确标注「legacy path，仅迁移参考」。
4. **门号重号**：`verify_el2.py` 有 187 个 `[N/183]` 打印点、183 个
   唯一门号，`[105]` 出现 2 次、`[106]` 出现 4 次。解析器与
   `--stop-after` 不受影响，但人类按门号定位失败时可能歧义。建议
   后续站点收口时消除重号，或引入子门号（如 `[106a]`）。
5. **单维护者 + 同族 AI 评审**：`docs/reviews/` 已诚实标注
   「same model family」。正式发布前至少安排一次跨族或人类评审，
   覆盖构建/验证/发布语义变更。
6. **QEMU-only 与实时性证据**：`docs/benchmark/wake-latency-baseline.md`
   已诚实说明 QEMU TCG、100Hz tick、无亚拍唤醒。对外不应把该数据
   读成硬实时保证；真板弧需要单独的时钟/中断/延迟验证计划。

## 6. 优点（不应被上面的清单掩盖）

- **系统是真的**：EL2 Type-1、stage-2 隔离、真实 Linux 6.6 guest、
  双 musl 世界、Yocto 层、SBOM、发布 tar，不是只有文档的 demo。
- **验证纪律罕见**：append-only 门禁、两连真绿、失败分诊顺序、
  已知问题诚实登记、每站内容包与交接模板。183 门/2,984 行 verifier
  对 23.5k 行产品代码，测试投入比很高。
- **确定性设计**：产品代码 0 处堆分配，固定池/静态表；`-ffreestanding`
  + `-mgeneral-regs-only`；干净重建零警告。对 RTOS/车规方向是正确
  取舍。
- **合规面扎实**：132 个构建/源文件 SPDX 全覆盖、Apache-2.0、NOTICE、
  3rd 台账、GPL 仓外隔离、DCO 与真实 trailer 规则。
- **发布件今天可自靴**：`make-release.sh` 产出的 tar 在解包现场跑到了
  `musl: hello from /bin/hello`，这是「发布件不是构建系统自嗨」的
  直接证据。

## 7. 建议的发布路径

### 可以现在做（技术预览 / 公开开发中）

- 以 `27.04-dev` / 「Technology Preview」名义发布，不承诺生产可用；
- README 显眼位置写明：QEMU-only、已知 [37]/[38] 与 bsh 残面、
  安全边界未认证、无车规认证；
- 附本报告与 `KNOWN-ISSUES.md`；
- 内容包叙事从「已完成的自动驾驶 OS」改为「从零构建中的
  Type-1 + RTOS + Linux 双世界，全部判据可重放」。

### 正式 `v27.04` 发布前必须完成

1. 安静窗全量 183 门 ×2，记录两次 `RESULT PASS`；
2. `[37]/[38]` 与 bsh 残面：修复，或签署正式豁免并写入 release notes；
3. CI 增加全量 nightly + tag release job（含 make-release、SBOM、
   解包自靴、artifact 保留）；
4. `make-release.sh` 增加脏树守卫、版本守卫、`git_commit`/工具链记录、
   `git archive` 构建；
5. 容器基础镜像/apt/actions 钉死，镜像 digest 入 manifest；
6. `SECURITY.md` + 威胁模型 + CLA状态；至少一次跨族/人类评审；
7. 刷新 `PROJECT-STATE.md`，整理 devlog 目录与宿主路径；
8. 从全新克隆按文档走一遍「bootstrap → 全量两连 → release tar →
   解包自靴」，把这条路径写成一条命令。

### 车规/安全认证（远景，不在本次发布范围）

需要独立的 ISO 26262 过程、安全案例、静态分析/MISRA、WCET、故障注入、
形式化或系统性模糊测试、真板与 SMMU 验证、外部审计。当前仓库是
「可验证的技术底座」，不是「已认证的安全产品」。

## 8. 评估局限

- 本报告是**同族 AI 的整体评估**，不是独立第三方审计，也不是逐行
  代码审计；对具体 C/asm 缺陷的覆盖有限。
- 全量 183 门、Yocto 门、容器从零重建、真实板卡、安全审计、许可证
  逐行核对均未执行。
- 今天的快环在宿主 load ≈ 8.18 下通过；按本仓纪律，负载敏感门仍需
  安静窗复跑，快环通过不等于全量稳定。
- 报告中的「未就绪」是**发布门槛判断**，不是对项目价值的否定；
  相反，本仓库的证据链与工程纪律是本报告给出「技术预览可发」的依据。

## 附录：本次使用的关键证据

```
git rev-parse --short HEAD                  -> 7ab9c23
git rev-list --count HEAD                   -> 221
git describe --tags --always HEAD           -> 26.11-2w-13-g7ab9c23
make -B -j$(nproc)                          -> rc=0, 0 warnings
make verify-fast                            -> PARTIAL-PASS gates 1..36 (gate-time sum 27.8s)
bash scripts/make-release.sh                -> RELEASE OK (tar 954K, self-boot)
python3 tools/sbom-check.py                 -> ALL GREEN (8 components)
make check-isolation                        -> OK
python3 scripts/check-commit-msg.py \
  --range d2a5d0c^..HEAD                    -> range ok (7 commits)
SPDX scan (132 build/source files)          -> 132/132
malloc/calloc/realloc/free in product C     -> 0
```