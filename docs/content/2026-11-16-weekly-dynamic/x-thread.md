<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
<!-- X thread (English), 8 tweets; 也可截中文版另发 -->

# X / Twitter thread — first weekly report

**1/8**
eshine-os is open source since Nov 14: an ARM64 Type-1 hypervisor + a from-scratch RTOS + a real Linux guest, running side by side on one machine. 68 dev stations, 183 automated checks, ~24k lines of from-scratch C/asm, Apache-2.0. From now on: a weekly report every Monday. 🧵
#OpenSource #OSDev #Hypervisor #ARM64

📌 [attach: homepage + repo]

**2/8**
Two weeks, zero new features — all fixes. Three shell bugs killed: Ctrl-D now actually ends input (cat/wc no longer hang); relative paths like `cat ./etc/motd` work; and the real cause of a rare boot hang — two execution flows deadlocking on each other's lock, hiding for two weeks — is fixed. 6/6 clean boots after.

**3/8**
Crash messages became human: booting the second VM without a Linux kernel used to jump into empty memory and print a cryptic address fault. Now it says "Linux Image not loaded" and tells you what to fix.

**4/8**
One command to run: `make run` now auto-downloads and builds whatever is missing. A Docker path runs the whole thing inside a pinned toolchain container — build, boot, and an interactive console. Clone-and-build: no hardcoded machine paths, plus a host-independence scanner.
#BuildInPublic

**5/8**
Release engineering that nobody sees: a full-history scrub of 240+ commits until the keyword re-scan is CLEAN, and honest AI attribution — `Model:` / `Assisted-by:` trailers with a validator + git hook, so the git log records AI participation as a fact, not a slogan.

**6/8**
Honest boundaries: KNOWN-ISSUES.md is linked from the homepage with 5 open defects (dual-core test family, shell space-flood ~1/3, no double-green full run yet, QEMU-virt only). Shipping is a contract with future contributors, not a victory lap.

**7/8**
Day-1 field fix: the homepage release table overflowed phones by 70px. Porting kernel.org's open theme dropped one media-query rule (the #releases scroll container). Restored, cache-busted, verified by DOM probe and pixel diff. Port a theme, audit every block.

**8/8**
This week: close the dual-core contamination family ([37]/[38] failing 2/2), then two consecutive full 183-check green runs, then the shell space-flood confession — and only then station 69 (EL2 boundary fuzzing / sub-tick hrtimer / SMMU study).

Engineers welcome: run it, pick a known issue, open an issue, say hi. ⭐ github.com/eshine-os/eshine-os · gitee.com/eshine-os/eshine-os · www.eshine-os.org
#RTOS #Embedded #LinuxKernel #Virtualization

---

<!-- 圈层投递说明(不随文发布):
  1/8  = 开场钩子:开源+OSDev 圈 (#OpenSource #OSDev #Hypervisor #ARM64)
  4/8  = Build in Public 圈 (#BuildInPublic),引独立开发者/工程公开人群
  8/8  = 嵌入式/RTOS/Linux 内核圈 (#RTOS #Embedded #LinuxKernel #Virtualization)
  每条推 2-4 个标签即可,标签别堆满;链接只放最后一条,保触达 -->
<!-- 附图清单:1) 官网首页+repo 2) KNOWN-ISSUES 首屏 3) 手机修复前后对比 4) 容器 make run 到 banner 5) git log trailer 特写 -->
<!-- 首推可单独发手机修复前后对比图 + "Port a theme, audit every block." -->