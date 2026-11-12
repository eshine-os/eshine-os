<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第十四站设计:Linux bring-up 直接攻坚站(三门地基的会师之战)

日期:2026-10-24 · 版本:27.04-dev · 前站门禁:37 步真绿 ×2

## 0. 决策记录

用户令"按照推荐选项继续"——执行前报推荐:**Linux bring-up 直接攻坚**。
范围哲学承 bring-up 传统:**内核放进来,缺什么补什么**——修补面严格
限定在"让 Linux 在 EL1 起跑并走到可预期的终点"之内;终点定为
**无根文件系统的 VFS panic**(panic 得其所=门禁判据),后续站再续
initramfs/根文件系统。

## 1. 三门地基现状与内核需求对账

| 地基 | 已就位 | Linux 还要什么 |
|---|---|---|
| DTB | 注入+三节点自查+intc | **memory 128MB**/chosen/**bootargs**(console=ttyAMA0 earlycon);per-VM 树第一次真分歧——站 11 欠账兑现 |
| GICD/GICR | 探测/使能/门控注入 | TYPER 字段容忍度(实测补);优先级/ICFGR 写入面(已存储回读) |
| PL011 | AMBA 身份证+控制族 | earlycon 是否先于驱动写 CR(**实测定 TX 门松紧**) |
| virtio | legacy+split 两侧自洽 | **version 2->1**(自洽版号与布局不符,Linux 会按 v2 现代布局走);QueueAlign;**config 空间**(blk capacity);特性协商返回 0 |

## 2. 方案

### 2.1 内核:外部构建工件,GPL 隔离

- Linux **6.6 LTS** 源码自 cdn.kernel.org(直连实测通);defconfig+
  virt 必备内建,`make Image`。
- **内核与 Image 不进 eshine-os 仓**:仓库全 Apache-2.0(deviation #2),
  Linux 内核 GPL-2.0,二进制入库=许可污染;Image 是"可从源构建的
  工件"(同 guest1.bin 哲学),构建目录=legacy 外部工件区(现
  `ESHINE_WORK`,仓外)。仓内存 **scripts/build-linux.sh**(可复现配方)。
- 门禁确定性:verify 先验 Image 存在,缺则打"跑 scripts/build-linux.sh"
  明确指引后 FAIL(在本机它将存在,双绿可复验)。

### 2.2 第三槽:Linux VM 几何(ESHINE_MAX_VMS 2->3)

- 配置表化:per-slot RAM PA/尺寸/入口/x0/DTB。
- 槽 2(Linux):RAM **128MB@PA 0x58000000**(IPA 0x40000000;恰占满
  QEMU 512MB 顶,零 -m 改动;stage2 规则 IPA 1-2GB 内、l2_ram[512]
  上限内全兼容);Image 载于 +0x80000(text_offset),pc=0x40080000,
  **x0=0x40F00000**(DTB 指针,Linux 入口协议),x1-x3=0;MMU 由内核
  自启 stage-1(设计好的两级翻译,无需宿主动作)。
- 槽 0/1(裸机双客)几何不动——回归面全保。

### 2.3 per-VM 设备树(站 11 欠账兑现)

`tools/dtb/eshine-linux.dts`:同诚实裁剪原则,memory=128MB、
chosen 增 bootargs=`console=ttyAMA0 earlycon`(stdout-path 已有,
裸 earlycon 走 DT 探测)。dtb.c 双 incbin,vm_create 按 slot 选。
已知限制:两 dts 并存有漂移风险,分歧再长大时上运行时 patch。

### 2.4 virtio legacy 对齐核验

- **version 2->1**(关键:自洽期 v2 号配 legacy 布局,Linux 会按
  v2 现代布局寻址——QueueReady/QueueDescHigh,现模型没有);
- QueueAlign 存储并约束 4096(legacy used 环对齐公式同构);
- **config 空间只读**(0x100+:virtio-blk capacity u64 LE=128 扇区);
- DeviceFeatures 返回 0(零特性=512B 扇区语义,最简诚实)。

### 2.5 vuart TX 门松紧:earlycon 实测定

pl011 earlycon 是否先写 CR(UARTEN)再输出,以内核源码为准;若
earlycon 不置 UARTEN,则 TX 门从 UARTEN|TXE 放宽为 **TXE**(仿真器
务实主义,QEMU 行为基准同律;RX 中断门不动)。实测后锁,记录进
devlog。

### 2.6 门禁(37->44+,bring-up 实证锁针)

类别判据(needle 首次成功启动后按实测定字,锁前不进 verify):
- [38] 内核起跑:Booting Linux on physical CPU
- [39] DT 闭环:Machine model: eshine-os,vm
- [40] pl011 console:console [ttyAMA0] enabled(或 earlycon 行)
- [41] 中断面:gic-v3/arch_timer 初始化行
- [42] virtio 探测:virtio_blk 或 capability 探测行(若达)
- [43] 预期终点:VFS panic(无根文件系统,panic 得其所)
- [44] 宿主完整性复验:Linux VM 退场后宿主 shell 照常
(最终步数与 needle 以首跑实证为准;只增不减,旧 37 步全保。)

## 3. 偏离登记

#18:Linux guest 槽——参考实现有完整 Linux 启动链;新仓首次引入
Linux guest:第三槽几何表+外部内核工件(GPL 隔离,仓内只留构建
配方)+per-VM DTB 分歧+virtio legacy 对齐核验。

## 4. 不做(YAGNI)

- initramfs/rootfs/用户态:下一站
- PSCI/SMP(vCPU>1):Linux 单核起跑不需要
- ITS/LPI:无消费者
- 内核调优(tinyconfig):defconfig 驱动覆盖优先,够用即可
