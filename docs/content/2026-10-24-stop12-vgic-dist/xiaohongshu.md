<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 小红书:vgic 仿真站——distributor 成为权威

## 正文(≤800 字)

第十二站:guest 的中断终于有了"户口管理机构" 🏛️
以前:宿主看到事件就直接注入,谁决定该不该注?宿主独断
现在:**guest 在仿真 GICD/GICR 里使能了哪个中断,宿主才注哪个**
——distributor 成为权威,guest 没使能就不注入、不代完成 WFI

guest 引导期是真 OS 全套姿势:
PIDR2 探测((x&0xf0)==0x30 才认 v3)→ 开 CTLR → 清 WAKER 睡眠
→ ISENABLER 使能 vtimer 27 / vuart 33 / virtio 35
→ **回读验证** → 自报:
gicd v3 ok ✅ gic on: 27 33 35 ✅
第十一站树里缺席的 intc 节点,兑现归位 🌳

本站三战况,两处被"验证"自己抓了现行:

①stage-2 单页铁律拦下真布局(vGICD 64KB/GICR 128KB)
 → EMULATED 窗口放行多页(反正不建映射,窗口大小只是陷阱区)

②GICD 寄存器族解码 bug:(off-0x080)>>2 把 ISENABLER0 算成 n=32
  越界,写静默 WI——**guest 使能回读当场抓获**
  我设计时主动加的回读验证,第一个抓的就是我自己写的 bug 😅
  **判据即测试:把验证放进系统,bug 会自己走进来**

③SAS 编码值 vs 字节数:把"2"(=4 字节)当"2 字节"传给模型,
  4 字节读变半字,掩码把 0x08000000 吃成 0
  纸面自证三遍全对,探针打点两行实锤:
  **自证无误≠实测无误** 🔬

小彩蛋:SPDX 行被我收成 `-->`(markdown 肌肉记忆),
块注释没闭合吞了版权行——-Wcomment 一发命中 😂

门禁 34→36 步双绿 ✅ offset 全经 ARM 手册复核(站八纪律)
每晚 1-2h 与 AI 从零造 Type-1,devlog 12 全记录。

## 配图清单

1. 封面:gicd v3 ok / gic on: 27 33 35 自报特写
2. 门控语义对比图(宿主独断 vs distributor 权威)
3. 寄存器族 0x80 分立解码表(7 族基)
4. 探针打点输出(W off=0x10100 val=0x08000000 / R ret=0)
5. make verify 36 步 PASS ×2

## 标签

#虚拟化 #hypervisor #ARM64 #操作系统 #内核开发 #嵌入式 #QEMU
#AI编程 #从零开始 #eshine-os #GIC #中断
