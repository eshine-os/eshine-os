<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 27.04 第六站:vuart/MMIO 设备仿真站设计 spec

- 日期:2026-10-23 · 版本:27.04-dev · 前置:d6dab43(第五站调度站,22 步门禁双绿)
- 用户批准:四决策全选推荐(vuart/MMIO 仿真站 / 全切换陷阱仿真(HVC #0/#3 退役)/
  vcpu 增 wait_reason 两源等待 / 前台裸透传+后台 [vmN] 前缀),并按既有模式推进至完成

## 0. 目标与真绿定义

guest 的 UART 驱动**不换语义**,宿主把真 PL011 从它脚下换成仿真设备:
UART IPA 撤出 stage-2 直通映射 → 访问全部 data abort 陷阱化 →
ESR/HPFAR 解码 → 按 IPA 窗口分发到 per-VM vuart 模型。这是通向
virtio 的 trap 型设备模型门,同时消掉第五站 getc 的 HVC 风暴
(每字符 2 trap → FR/DR 陷阱 + WFI 等待),并对齐参考实现 vuart 形态
(偏差 #12 预留方向收口)。真绿=门禁 22 → 24 步连续 2 过,原 22 步
判据全部保持(门禁只增不减)。

## 1. 形态决策(用户四选,全推荐项)

| 决策 | 定案 |
|---|---|
| 站点 | vuart/MMIO 设备仿真站(vtimer_ctx 多等待者继续后置) |
| guest UART | 全切换陷阱仿真:HVC #0(putc)/#3(getc) 退役,HVC 只剩控制类(#1 exit/#2 yield/#4 host);guest 驱动 TX 路径代码零改动(u32 访问 DR/FR 原样,物理行为从直访变陷阱),RX 路径从三态 HVC 换成 PL011 原生 FR 轮询+WFI |
| 等待语义 | vcpu 帧契约尾部追加 `wait_reason`(0=无/1=TIMER/2=RX);WAIT 入口由泵判定(vtimer 已武装=TIMER,否则 RX),WOKE 后泵按原因走:TIMER→注 LR,vtimer 链原样;RX→直接重跑(guest 自查 FR) |
| TX 展示 | 前台(owner==cur_vcpu)裸透传;后台输出行缓冲,换行时整行打 `[vmN] ` 前缀(截断即冲,无前缀歧义) |

## 2. 设备表与 stage-2(结构迁移:映射层不再有 UART)

- `vm.h`:`vm->devices[0]` 改为 `{ipa=0x09000000, pa=0, size=0x1000,
  attr=S2_ATTR_EMULATED}`(新 attr 值);`struct eshine_vm` 内嵌
  `struct vuart vu;`(静态零堆纪律不变)
- `stage2_build`:attr==EMULATED 的条目**不建 stage-2 映射**——
  guest 一访问即 stage-2 translation fault,这正是分发入口;
  未命中任何仿真窗口的 fault 走既有 guest fault 隔离死(**设备层
  默认拒绝**:窗口外一律死,guest 的 `fault` 命令(0x30000000)
  行为不变,判据 [17] 保持)
- 指令中止(EC=0x20,在未映射区取指)仍走 fault 死:设备窗口
  永不可执行

## 3. trap 分发(新 kernel/mmio.c + exceptions.c 增支)

- `el1_sync` 增 EC=0x24(data abort from lower EL)分支 → `mmio_dispatch(v, esr)`:
  - ISS 解码(ISV/SAS/SRT/WnR/DFSC 按 ARM ARM ISS 布局):
    `isv=iss>>24 &1`、`sas=(iss>>22)&3`、`srt=(iss>>16)&0x1f`、
    `wnr=(iss>>6)&1`;ISV=0(无寄存器现场)→ fault 死
  - **IPA = HPFAR_EL2 << 8**(stage-2 fault 的 IPA 权威来源;
    FAR_EL2 对 S2 fault 可能无效,不信。QEMU 8.2.2 实现了
    HPFAR——落地前活探针复核,方法论同第四站)
  - 窗口匹配:扫 `vm_of_vcpu(cur_vcpu)->devices[]` 中 EMULATED 条目,
    IPA 落窗 → 交 `vuart_mmio_read/write`;窗口外 → fault 死
  - 读回填:val 写 `gprs[srt]`(srt==31=xzr 不回填);exit 走
    `resume_or_preempt(v)`(量子契约不破)
- guest 现场事实:miniuart 为 `volatile unsigned int *`——
  **u32 访问,SAS=2**;vuart 语义按 32 位读写实现,DR 读出低 8 位
- fault 诊断(guest fault 路径)增打 HPFAR_EL2:既是调试素材,
  又是 HPFAR 探针(0x30000000 → 应见 0x30000)

## 4. vuart 模型(新 drivers/vuart.c + include/vuart.h)

per-VM 实例,PL011 最小子集(QEMU 已把 PL011 初始化好,guest 不碰
CR/波特率,只碰 DR/FR——子集与 guest 访问面严格对齐):

| 偏移 | 读 | 写 |
|---|---|---|
| DR 0x00 | 弹 RX 环(空则回 0,防御) | TX:前台裸 `console_putc`,后台入行缓冲;`\n`/满冲 |
| FR 0x18 | RXFE=环空(bit4);TXFF=0(bit5,host console 即时排空,guest TX 轮询永不自旋);BUSY=0 | 忽略 |
| 其余 | 0 | 忽略 |

- RX 环:64B(`rx[64] + head/tail`,生产者=宿主 shell,消费者=guest DR 读)
- TX 行缓冲:128B + tagged 标志(行首定 tag);触发:`\n`/满/
  **owner 切换(vmconsole/handback)/vm_finish**(死前冲尾,不吞半行)
- 中断不注入:guest 轮询 FR+WFI,无 UART IRQ(偏差 #13 登记)

## 5. RX 路由与唤醒(shell 泵协同)

- shell 主循环**恒排空**硬件 RX(不再 owner 门控):有字符时
  owner 非空 → `vuart_rx_push(owner_vm->vu, ch)` + 若等待者
  `wait_state==1 && wait_reason==RX` → `wait_state=2`(WOKE-rx);
  owner 空 → 原样进 shell 行处理
- 前台 guest 在跑量子期间到达的字符暂存 PL011 硬件 FIFO,
  泵回后 shell 排空进环——人类敲键节奏(≤16B/s)远低于 1B/量子
  的排空速率;粘贴长行可能溢 16B FIFO,登记为已知限制
- WAIT 判定入泵(泵有 vm 语境,el1_sync 不猜):cntv_ctl 使能即
  TIMER,否则 RX;单源等待假设(sleep 与 getc 互斥),多路等待留后
- vmconsole <id>:owner 切换前先冲 TX 行缓冲(tag 定前缀),再置
  owner;目标 VM 若 WAIT 则置 runnable(现有 1→0 语义不变)
- WOKE-rx 泵路径:`wait_state=0` 直接跑,**不注 LR**(FR/DR 陷阱
  自会取数;per-pCPU 单 LR 通道纪律不变)

## 6. guest 侧改造(miniuart.h / hvc.h / main.c)

- `uart_putc`:**零改动**(代码原样,FR.TXFF 轮询+DR 写入,物理
  行为直访→陷阱;vuart 恒回 TXFF=0 故轮询即过)
- `uart_getc`:HVC 三态退役,换 PL011 原生语义:
  `while (FR & RXFE) wfi; return DR;`——WFI 陷阱(TWI)→WAIT(rx),
  字符入环后 WOKE-rx,泵放回,FR 命中,DR 取字。每字符 2-3 trap
  (旧 getc 风暴每字符 ≥2 HVC + 轮询),且等待零开销
- `hvc.h`:删 #0(putc)/#3(getc),留 #1/#2/#4;selftest 的
  `hvc_putc('.')` → `uart_putc('.')`(寄存器存活性判据升级:
  跨 100 次 **MMIO 陷阱**比跨 100 次 HVC 更硬)
- `main.c`:help 文本不动(`host` 命令与 HVC #4 保留)

## 7. 门禁 22 → 24(只增不减)

- 原 22 步全部保留,行为等价迁移:banner/token/echo 经陷阱出,
  needle 仍子串命中(前缀只增文本);sleep/spin/公平/隔离判据原样
- 新增两步:①后台输出前缀判据——vmrun 1 后(未 vmconsole 前)
  泵跑 vm1,banner 以 `[vm1] eshine-guest VM1 @EL1` 出现;
  ②RX 唤醒链显式判据——前台 vm0 WAIT-rx 挂起,宿主发空行入环,
  `[WOKE] guest 重打 prompt`(push→WOKE-rx→泵放回→FR 命中全链)
- selftest 步文案更新:100 MMIO 陷阱

## 8. 基准对齐预登记(#13)

参考实现 vuart:完整 PL011 语义 + IRQ 注入支持 + 宿主转发线程化 →
新仓:DR/FR 最小子集 + 轮询/WFI(无 UART IRQ)+ 行缓冲前缀 +
RX 环 owner 路由。收口偏差 #12(console 形态:HVC 代理→vuart
陷阱仿真)并新登 #13(vuart 子集与无中断形态;IRQ 注入留
GIC SPI 站)。

## 9. 已知限制(登记+README 说明)

- 粘贴长行可溢 PL011 16B FIFO(排空窗=1 量子;人类敲键无碍)
- 单源等待假设:同一 guest 不得同时 armed vtimer 又等 RX
- vuart 无中断注入:guest 轮询+WFI;SPI 注入留站后
- 双字/半字访问未实现(guest 现只用 u32,SAS=2;其他 SAS 入
  模型即拒?——不,按字截断返回,防御不致命)
