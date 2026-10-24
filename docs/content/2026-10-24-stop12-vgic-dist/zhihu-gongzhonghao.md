<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# distributor 成为权威:中断面 OS 化

> 27.04 连载第 12 篇。前十站的 guest 中断"没有 distributor"——宿主
> 直接往 GIC 列表寄存器注入,guest 只开虚拟 CPU 接口。这一站补上
> 整条链的最后一环:仿真 GICD/GICR,guest 按真 OS 的姿势探测、使能、
> 唤醒,宿主泵注入改为按 guest 的使能位门控。Linux 的 gic-v3 驱动,
> 从此有了可以落地的地基。

## 先说结局

    [vm0] gicd v3 ok
    [vm0] gic on: 27 33 35

guest 引导期做了真 OS 的全套 GIC bring-up:读 PIDR2 确认"你是
GICv3"(判据位 [7:4]==3),开 distributor 和 redistributor,清
WAKER 睡眠位,经 ISENABLER 逐 INTID 使能 vtimer PPI 27、vuart
SPI 33、virtio SPI 35——然后**回读验证**,确认每一笔都真的落了地,
才敢自报。门禁 34→36 步双绿。

同时,第十一站里"因未仿真而不登记"的 intc 节点请回了设备树。
树不替未来撒谎——但兑现的那天,它就回来了。

## 为什么要门控注入

旧模型:宿主看到事件就写 LR,guest 醒来消费。谁决定"该不该注"?
宿主独断。新模型:注入前查 guest 的 distributor 状态——SPI 33 要
GICD_ISENABLER0 的 bit1 置位,PPI 27 要 GICR_ISENABLER0 的 bit27
置位。**guest 没使能就不注入,也不代完成 WFI**——它自己的配置自己
负责,这是隔离面哲学在中断面的延伸。

别小看这一步的语义变化:它把"宿主替 guest 做决定"改成了"宿主执行
guest 的决定"。真 OS 会在运行时动态开关中断源——门控在,行为就
正确;门控不在,宿主就是那只伸进 guest 车里的手。

## 战况一:stage-2 单页铁律

仿真设备窗口至今都是一页(4KB)。vGIC 真布局天然跨页:GICD 64KB、
GICR 是 RD+SGI 双帧 128KB——stage2_build 的单页校验当场拒绝。

对 EMULATED 窗口这是过严约束:它根本不建映射,窗口大小只是"访问
即陷阱"的范围标记。放行多页(对齐+4K 整数倍),直映射设备仍单页。
**校验规则是给布局形态立法的,布局形态变了,法要跟着审**——这一站
的立法场景是"真实设备布局"。

## 战况二:寄存器族解码 bug,回读验证当场抓获

首版把 GICD 的 0x080-0x3FF 一整段当一族共用 `(off-0x080)>>2` 算
寄存器索引——ISENABLER0(0x100)被算成 n=32,越界,写静默 WI、读
RAZ。guest 自报失败:`gic FAIL: ppi27`。

值得说的不是 bug 本身,是**谁抓住了它**:guest 的使能序列最后一步
是回读验证——写进去的东西读出来对不对。这一步是我在设计时主动加的
判据,结果它第一个抓住的就是我自己几小时前写下的 bug。**判据即
测试:把验证放进系统,bug 会自己走进来。**

## 战况三:SAS 编码值 ≠ 字节数

mmio 分发把 ISS 综合症里的 SAS 字段原样传给设备模型。SAS 是编码:
0=1 字节、1=2 字节、2=4 字节、3=8 字节。我把编码值 2 当成"2 字节"
传了——模型里 4 字节读被当半字,掩码 0xFFFF 把 ISENABLER0 的
0x08000000 吃成了 0。

这个 bug 最刁的地方:我在纸面上把读写路径自证了三遍,全部"正确"。
最后是探针打点两行实锤——写确实落了,读回来是 0。**自证无误≠实测
无误**;自证验证的是"我认为的代码",探针看的是"跑起来的代码"。
修法一行:`size = 1 << sas`。

## 顺手记:SPDX 行的 markdown 肌肉记忆

新建源文件,SPDX 行被我收成了 `<!-- ... -->` 的 `-->`——块注释没
闭合,版权行被吞进上一个注释。-Wcomment 一发命中,秒修。教训不值
一提,现象值得乐:写多了文档,手会自己串台。

## 数字

- 门禁 34 → **36 步双绿**(+gicd v3 探测/gicd+gicr 使能回读)
- intc 节点回树(blob 1057→1303 字节);its/psci/maintenance 仍缺席
- vGIC 模型 ~250 行;guest bring-up ~10 次陷阱走全链;
  ESHINE_MAX_DEV=4 恰好满编(vuart/virtio/vgicd/vgicr)
- 已知限制(登记 #16):无 LPI/ITS/1-of-N 路由/TYPER 全字段位,
  Linux bring-up 站按需补

代码、devlog 12:github.com/eshine-os/eshine-os · www.eshine-os.org。
每晚 1-2 小时,与 AI 结对,从零造 Type-1。

### 平台微调说明

- **知乎**:全文可发;"判据即测试"与"SAS 编码值"双传播点
- **公众号**:寄存器族解码表做成图;门控前后语义对比图
- **B 站**:见 youtube.md,三战况做三段分镜
