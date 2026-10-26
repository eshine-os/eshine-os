<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Devlog 15 — rootfs 站(initramfs → busybox 用户空间)

日期:2026-10-26 · 版本:27.04-dev · 门禁:48 步真绿 ×2 连过

## 里程碑

站十四死在 `VFS: Unable to mount root fs`;本站把用户空间补上,
**eshine-os 史上第一个用户态**诞生:

    [vm2] [    1.017952] Run /init as init process
    [vm2] ~ # cat /proc/cpuinfo
    CPU implementer	: 0x41
    CPU architecture: 8
    CPU part	: 0xd07          ← cortex-a57,我们建模的那颗 CPU

busybox `sh` 以 PID 1 运行,`vmconsole 2` 直落交互 shell,
`cat /proc/cpuinfo` 由**用户态**读出真 CPU 身份——每一层都诚实:
QEMU a57 → EL2 → stage-2 → Linux 6.6 → initramfs → /init → sh → /proc。

## 形态

- **initramfs 槽位**(bootloader 同构,站 11 哲学延伸):guest IPA
  0x44000000 = 宿主 PA 0x5C000000(stage-2 非恒等映射换算),QEMU
  loader 直载;**定长 8MB**,newc 未压缩 cpio 零填充(cpio 解包器
  遇 TRAILER!!! 即止,尾零是合法 padding;压缩流尾零才是垃圾——
  这是选未压缩的结构理由)。DTB `chosen` 增 `linux,initrd-start/end`
  **常量**,零运行时 patch。
- **工件**:busybox 1.36.1 aarch64 静态交叉构建(GPL 隔离,仓内只留
  配方 scripts/build-initramfs.sh);**仓内 python newc 打包器**
  tools/mkinitcpio.py(mknod 要 root 而 fakeroot 缺,newc 一页可读,
  从零气质;设备节点 spec 化,`--dev console:5:1` 无 root 虚拟 mknod)。
- **rootfs**:/init(挂 proc/sys 后 `exec /bin/sh`,PID 1 平滑过渡)+
  busybox + sh/cat/echo/mount 等符号 + console/null 节点。
- **内核配方零补丁**:设计期以为要补 CONFIG_RD_NONE,实测 6.6 已
  固化未压缩 cpio 支持(init/initramfs.c 的 070701 魔数直入解析器,
  尾零被显式跳过)——RD_NONE 选项根本不存在。站 14 的 Image 即成品。

## 战况:两个实现期根因

### ① CPACR 归还 guest(站 14 "FPEN 修复"的翻案)

首跑死法最华丽的一版:boot 越过 VFS panic,PID 1 (Comm: sh) 诞生,
`ret_from_fork → asm_exit_to_user_mode`——项目第一次返回用户态——
然后在 `fpsimd_load_state+0x4`(首条 SIMD 指令)上 EC=0x07 陷阱,
内核"Unhandled exception"恐慌。

`-d int` 定谳:陷阱投递在 guest 自己的向量(0x80091200),ELR =
fpsimd_load_state。但 6.6 的内核**从不**在正常运行中写 CPACR——
`__cpu_setup` 只在开机设一次。谁把 FPEN 清了?

**站 14 的 `msr cpacr_el1, 3<<22`**(vcpu_run 每次进场)——内核
sysreg.h 白纸黑字:`CPACR_EL1_FPEN_EL1EN = BIT(20)`、`EL0EN =
BIT(21)`——**FPEN 真位是 [21:20],3<<22 写的是 TTA/RES0**,
对 FPEN 是空操作;而 `msr cpacr_el1, x` 是**整寄存器写**,把内核
布好的 FPEN [21:20] **清成了 00**。首次抢占重入后,第一个显式
SIMD(用户态 FP 恢复)即陷阱。站 14 的 boot 之所以能跑:内核
`__cpu_setup` 自己写的 3<<20 生效(我们那个 3<<22 从来没起过作用,
那个"bit position correction"修了个寂寞)。

修法一行:`vcpu_run` 的 CPACR 写**移除**。第三次"仿真层不越权"
(SP_EL0、vtimer 之后),同一哲学:**guest 的现场 guest 做主**。

### ② owner 显式接管 + 逃生字符(Ctrl-])

shell 会话首验:prompt 出来了,但后续输入全喂给了**宿主** shell
(`eshine> cat /proc/cpuinfo` → `unknown command: cat`)。

站 6 的设计"TIMER 睡自动归还 owner"(宿主睡期间要能敲命令)对
裸机 guest 是体贴,对 Linux 壳是灾难:**busybox 壳等输入时 WFI,
tick 恒 250Hz armed——按站 6 判据永远归 TIMER 类,owner 每次睡
都被收走**,会话形同虚设(实测)。

修法:owner 改**显式接管**(泵撤自动归还),归还三口:`vmconsole
host` / **逃生字符 0x1d(Ctrl-],垫 Xen/ssh 惯例)**/ VM 死亡;
裸机客另有 host(HVC#4)自报。宿主路由层截获 0x1d,永不进 guest,
卡死的 owned 客也夺不宿主的面。门禁 [26]/[28]/[48] 同步补逃生
序列(旧依赖自动归还的步骤如实改写)。

另收两个小坑:门禁 [46] token 计数竞态(read_until 首中打字回显
即回,sh 输出未到——锚下一个 prompt 再计数);verify 的 read_until
不传 start 会命中旧 prompt(token_echo docstring 里八站的教训,
换个马甲又踩一次)。

## 战法记事

- busybox 1.36.1 撞 Ubuntu 24.04 新内核头(tc applet 的 CBQ 定义
  自 linux-libc-dev 移除)——配方禁 tc 一行了事。
- 未压缩 cpio 定长槽的确定性:工件大小恒 8MB,DTB 属性恒定,
  与"压缩流尾随零是解压器垃圾"的隐患一次解清。
- busybox 静态 glibc 的 "Final link with: m resolv" 照常可用,
  rdinit 换 /init 脚本(挂 proc/sys)后用户态自证判据成立。

## 残余与后续

- shell 会话里 VM 死亡/处决后 owner 由 vm_finish/vm_kill 清(已核),
  但会话中途宿主侧只有逃生字符一条路——多通道(如串口多路)留后续。
- SIMD per-VM ctx 仍未入帧:当前单 Linux 槽无碍,多 FP guest 并跑
  前须补(站 14 记案沿用)。
- initramfs 8MB 槽与 dts/initrd-end、build 脚本 SLOT 三方绑死,
  升容须同 commit 同步。
