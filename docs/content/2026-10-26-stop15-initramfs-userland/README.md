<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第十五站内容包:rootfs 站(initramfs → busybox 用户空间)

`docs/content/2026-10-26-stop15-initramfs-userland/` —— 27.04 第十五站(master
站内,2026-10-26 完成)的公开内容底稿,按平台一文件。

| 文件 | 平台 | 形态 |
|---|---|---|
| `zhihu-gongzhonghao.md` | 知乎 / 微信公众号 | 深度长文 |
| `xiaohongshu.md` | 小红书 | 短内容 + 配图 + 标签 |
| `x-thread.md` | X / Twitter | 英文 8 推 |
| `youtube.md` | YouTube(B 站) | 分镜脚本 |

## 统一素材

1. 主视觉:`~ # cat /proc/cpuinfo` → `CPU part : 0xd07`——用户态 shell
   在自研 hypervisor 上读出自己的 CPU;上一个站这里还是 VFS panic
2. "我修了一个不存在的 bug":站 14 的 CPACR 3<<22 修复,写错了位
   (FPEN 真位 [21:20]),从来是空操作——boot 能跑全靠内核自己;
   而 `msr` 是整寄存器写,把我们没懂的位顺手清了零,用户态第一发
   SIMD 就崩。教训:读 spec,别猜位
3. 逃生字符 Ctrl-] 的诞生:裸机 guest 的"TIMER 睡自动归还控制台"
   遇上 Linux shell 的 250Hz 恒 armed——同一台机器两种生命节律,
   协议改显式接管 + 一字符逃生(垫 Xen/ssh 惯例)
4. 从零细节:仓内 python 写的 newc cpio 打包器(mknod 要 root,
   fakeroot 缺席——40 行代码换一个确定性工具链)
5. 48 步门禁 ×2;站 14 的 VFS panic 端点由更强端点取代
   (Run /init → busybox PID 1 → 交互 shell)
