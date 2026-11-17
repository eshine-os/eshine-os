<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# eshine-os 版本管理（Ubuntu 式）

## 伞版本（用户唯一需要知道的版本）

- **`YY.MM`，时间驱动**：每年 4 月 / 10 月各一版（6 个月节奏），"ship what's ready"。
- 后缀：`-dev` 开发线；`.N` 点版本（bugfix 累积，如 `26.10.1`）；`LTS` 长期支持（待首个稳定架构后启用）。
- 不使用发布代号，纯数字。

## 组件版本（独立演进，伞版本里钉死）

| 组件 | 版本策略 |
|---|---|
| `hypervisor` | 主仓代码 = 伞版本 tag 本身（`git describe` 可追溯） |
| `rtos`（VM0 管理域） | 独立 semver（`0.x.y`），每个伞版本在 manifest 里钉住 |
| `guest-linux`（VM1+） | **上游 LTS 钉版，不 fork**：每伞版本钉一个上游 LTS（如 6.12.x），仅 config fragment + 构建期补丁，全部文档化 |
| `toolchain` / `qemu` | manifest 记录实测版本 |

每个正式版一份 manifest：`docs/releases/vYY.MM/manifest.yaml` + 发布说明 `notes.md`。

## 机制（单一事实源 → 全链一致）

```
VERSION 文件（仓库根,唯一事实源）
  → Makefile 读取 → -DESHINE_VERSION_STR 注入 → version.h 引宏
  → banner / shell version 命令 / gate_suite.py 判据（脚本动态读 VERSION）
  → git tag vYY.MM（annotated,commit 规范同 commit-message conventions）
```

发布序列：manifest+notes commit → tag `vYY.MM` → 立即 bump `VERSION` 到下版 `-dev` 开发线。

## 与官网发布表的映射

- **mainline** = master tip（`YY.MM-dev`）
- **stable** = 最新 `vYY.MM` tag（点版本沿用同 tag 系列）
- **longterm** = LTS tag（未来）

## 开发周标签（dev tags）

- 每周一个轻量 tag，格式 `YY.MM-Nw`：
  - `YY.MM` = 该周周一所在月份；
  - `N` = 该周周一是该月的第几个周一（自然周序号，从 1 起）；
  - 例：2026-11-02 是 11 月第 1 个周一 → `26.11-1w`；
    2026-10-19 是 10 月第 3 个周一 → `26.10-3w`。
- tag 指向该周**最早提交**的 commit（按 author date，仅 master）。
- 开发 tag 为 lightweight；正式版 `vYY.MM` 仍为 annotated。
- 脚本：`scripts/tag-dev-week.py`（当前周）/ `--backfill`（全部历史周，幂等）。

## 提交 trailer 规范（真实语义）

提交消息末尾的 trailer 只记录**实际参与**：

    Model: <主模型>
    Assisted-by: <实际参与的其它模型>     # 没有就省略
    Reviewed-by: <评审者>                # 只有真做过评审才写
    Review-Ref: docs/reviews/<file>.md    # 有 Reviewed-by 时必须存在
    Signed-off-by: Julian Chu <rootuliux@gmail.com>  # 必须，最后一行

- 提交消息使用英文、纯 ASCII，subject ≤72 字符。
- 作者与提交日期不改写；`Signed-off-by` 即 DCO 声明。
- 2026-11-10 之前的历史提交带统一四行模板；那只是模板，不是逐条参与
  证明（见 [docs/COMMIT-ATTRIBUTION.md](COMMIT-ATTRIBUTION.md)）。
- 校验器：`scripts/check-commit-msg.py`；安装 hook：`scripts/install-hooks.sh`。
