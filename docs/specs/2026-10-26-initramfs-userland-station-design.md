<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Spec — 第十五站:rootfs 站(initramfs → busybox 用户空间)

2026-10-26 · 用户三决策全选推荐(见下) · 状态:实现中

## 0. 目标

站十四端点是 `VFS: Unable to mount root fs`——内核死在"没有用户空间"。
本站给 VM2 补上 rootfs,Linux 走完全程进用户空间,终点为:

1. busybox `sh` 作为 init 运行(`rdinit=/bin/sh`);
2. **vmconsole 2 直落 Linux shell 交互**(人机弧线收尾镜头);
3. shell 内 `cat /proc/cpuinfo` 自证 CPU part 0xd07(cortex-a57)——
   Linux 用户态在 eshine-os 上自报家门。

站十四的 VFS panic 判据由更强端点取代(boot 越过 root-mount 直达
用户空间;门禁总数只增不减,判据严格变强)。

## 1. 用户三决策(2026-10-26,全选推荐)

| # | 决策 | 定案 |
|---|---|---|
| 1 | rootfs 装载形态 | **initramfs + DTB 属性**(loader 同构,走真启动协议);否决内核内嵌(绕开协议)与 virtio 磁盘(过重,留后续) |
| 2 | 用户空间内容 | **busybox 1.36.1 aarch64 静态交叉编译** + `rdinit=/bin/sh` 交互 shell;busybox 同 Image 外部工件(GPL 隔离,仓内只留配方) |
| 3 | 门禁终点 | **shell 交互 token 双回显 + cat /proc/cpuinfo 自证**;不止步于 init 日志行 |

## 2. 形态

### 2.1 initramfs 槽位(bootloader 同构,站 11 哲学延伸)

- **位置**:guest IPA **0x44000000**,宿主物理 **0x5C000000**
  (0x58000000 + 0x4000000,stage-2 非恒等映射换算),QEMU loader
  `addr=0x5C000000,force-raw=on` 直载——与 Image 同一机制。
- **定长槽**:**8MB**。cpio(newc,未压缩)按零填充到恰好 8MB——
  cpio 解包器遇 TRAILER!!! 即停,尾零是合法 cpio padding;
  定长 ⇒ DTB 属性为常量,**零运行时 patch**。
- **范围核查**:内核 Image 止于 ~0x42C80000;CMA 由内核自选
  (memblock 先预留 initrd,CMA 只在空闲区落位);DTB 在 0x40F00000
  ——三方无重叠。
- **DTB**:eshine-linux.dts `chosen` 增
  `linux,initrd-start = <0x0 0x44000000>` /
  `linux,initrd-end   = <0x0 0x44800000>`(u64);
  bootargs 增 `rdinit=/bin/sh`。

### 2.2 内核配方补丁(scripts/build-linux.sh 追加)

- `CONFIG_RD_NONE=y`(本机 .config 实测未开)——未压缩 cpio 直读,
  与定长槽互为因果(压缩流尾随零是解压器垃圾,未压缩 cpio 尾零合法)。

### 2.3 busybox 构建(scripts/build-initramfs.sh,新)

- busybox 1.36.1 自 busybox.net 经代理取源(已探 200);
- aarch64 交叉静态:`defconfig` + `CONFIG_STATIC=y` +
  `make CROSS_COMPILE=aarch64-linux-gnu- -j`;
- 组装 rootfs:`/bin/busybox` + `/bin/sh → busybox` 符号 +
  `/dev/console`(c 5 1)节点 + `/proc`、`/sys`、`/etc` 骨架;
- **cpio 打包器:仓内 python 实现**(tools/mkinitcpio.py,newc 格式,
  设备节点无 root 依赖,fakeroot 本机实测缺;~40 行,从零气质);
- 产出 `~/projects/eshine-linux/initramfs.cpio`(外部工件区,
  仓内零字节),构建终检 size ≤ 8MB 硬门。

### 2.4 宿主零改动核验

hypervisor 本体**不需要任何代码改动**:stage-2 已盖全 128MB,
DTB 是站 11 的整树 memcpy,initrd 由 QEMU loader 载入 RAM——
站 11"bootloader 同构"投资在此兑付。

## 3. 门禁 45 → 50(needle 首跑实证后锁)

- [46] initramfs 装载与展开(内核日志证据,首跑定 needle)
- [47] init 接力:Run /bin/sh as init process
- [48] vmconsole 2 → shell prompt + token 双回显
  (RX 环→vSPI 33 注入→tty ldisc→sh 全链;站 12 GICD word1 修复
  的 Linux 红利在此兑付)
- [49] `cat /proc/cpuinfo` → `CPU part : 0xd07`(用户态自证)
- [50] 收尾:vmconsole host + vmkill 2(killed)+ tick 递增 + 宿主 token
- 旧 [44] VFS panic 判据:被更强端点取代,演进在 ARCHITECTURE
  验证节诚实标注(总数 45→50,判据只强不弱)。
- 三处同步同 commit:verify / shell help 走查 / ARCHITECTURE
  (+Makefile 注释)。

## 4. 已知风险与预案

- /dev/console 缺失 → kernel "unable to open an initial console",
  sh 无 stdin:cpio 打包器设备节点为硬性第一步。
- shell TX:pl011 驱动中断式 TX,我们的 vuart FR.TXFF 恒 0
  ⇒ 写同步排空,TX 中断缺席无害(站 14 已实证 console 链)。
- shell RX:vuart RX 环→vSPI 33,链路门禁 8 站实证;Linux tty
  使能 SPI33 走 GICD word1(0x104,站 14 修复)——若链断,先查
  vgic_spi_enabled(33) 与 RIS/DR 语义。
- busybox 静态 glibc NSS 噪音(getpwnam 类警告):接受,不修
  (rdinit 直跑 sh,不走 login)。
- 8MB 不够(busybox defconfig 膨胀):配方终检硬门挡下,升槽位
  需同步 DTB 属性与 ARCHITECTURE 记案。

## 5. DoD

spec → 实现(配方/打包器/dts/门禁)→ 门禁 50 ×2 真绿 →
devlog 15 → 偏差 #19 → 内容包 5 文件 → commit → 镜像机 → 记忆。
