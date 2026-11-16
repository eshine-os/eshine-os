<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
<!-- X thread (English), 8 tweets; 也可截中文版另发 -->

# X / Twitter thread — open-source week 1

**1/8**
Week 1 of eshine-os in the open is done: the repo went public on GitHub (primary) + Gitee (mirror), and the homepage now carries the ASIL/CAL architecture figure and the roadmap. 68 dev stations, 183 replayable gates, ~25k lines of from-scratch C/asm under Apache-2.0. 🧵

📌 [attach: homepage + repo]

**2/8**
The twist: this week wrote zero lines of kernel code. It was all "release engineering" — making a repo that RUNS into a repo you're comfortable letting strangers read. Five things, each a lesson. 👇

**3/8**
1/ History compliance. The danger isn't today's code — it's the entire commit history carrying internal references. Five full-history rewrites, then a keyword re-scan of every commit until CLEAN. Write the checklist first; "feels done" is not a criterion.

**4/8**
2/ Honest AI attribution. This codebase is human + AI pair-programmed, and the git log says so with real semantics: `Model:` (primary), `Assisted-by:` (actually involved), `Signed-off-by:` (DCO). A validator + git hook rejects the old uniform template on new commits. AI participation is a fact; record it as a fact.

**5/8**
3/ Environment isolation → 4/ Containerization. One work root (`ESHINE_WORK`), a host-independence scan, and a pinned container that builds and boots QEMU with `--network none` — if it runs with no network, there are no hidden downloads. A field report Monday (curl fails, wget resolves) became a fallback fix + host-side bootstrap, closed before lunch.

**6/8**
5/ Honest boundaries. KNOWN-ISSUES.md is linked from the homepage: the dual-core gate [37]/[38] contamination family, the shell space-flood face (~1/3), "full double-green not yet achieved", QEMU-virt-only. Shipping is not a victory lap; it's a contract with future contributors: known debt, don't step in it, come collect it.

**7/8**
Day-1 field bug, fixed live: the homepage release table overflowed phones by ~70px. Root cause: when porting kernel.org's open CSS theme, the 848px media query dropped ONE rule — the #releases scroll container. Restored the upstream rule (v7 stylesheet, cache-busted). Verified by DOM probe (doc scrollWidth 445→360 @375px) and a byte-identical deploy-vs-local screenshot. Port a theme, audit every block.

**8/8**
Next week, defect budget first: the [37]/[38] root-b/root-c forensics, then two consecutive full 183-gate green runs, then the bsh space-flood confession — and only then station 69 (fuzzing the EL2 boundary / sub-tick hrtimer / SMMU bring-up study).

⭐ https://github.com/eshine-os/eshine-os · https://gitee.com/eshine-os/eshine-os · https://www.eshine-os.org

---

<!-- 附图清单:1) 官网首页+repo 2) KNOWN-ISSUES 首屏 3) 手机修复前后对比 4) 容器 make run 到 banner 5) git log trailer 特写 -->
<!-- 首推可单独发手机修复前后对比图 + "Port a theme, audit every block." -->
