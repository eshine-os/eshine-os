<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 小红书:PL011 完整化站——设备证明自己是谁

## 正文(≤800 字)

第十三站:串口的 AMBA 身份证上线 🪪
AMBA 总线绑驱动不靠猜——设备自己用只读寄存器报身份:
PERIPHID 四字 → AMBA id **0x00141011**
PCELLID 四字 → **0xB105F00D**
没有身份证,pl011 驱动根本不绑——**总线先验身份证,再看演技** 🎭
guest 引导期读八字节验算,过了才自报:
pl011 on: amba 00141011 ✅

顺手补齐控制族:CR/IBRD/FBRD/LCR_H/IFLS/DMACR 存储回读,
guest 初始化改 Linux pl011 同律:
关机(CR=0)→配波特(115200)→8N1+FEN→开机(CR=0x301)
(波特率不仿真——没有时钟源就没有时序,登记而不演算)

本站唯一一战,全站最有味道 🌶️:
CR 真语义上线(DR 写要 UARTEN|TXE——关机态写 DR 无效)
首测:**一切开机前的输出全被门吞了**——
pl011 自报、连上一站的 gicd 自报,无声蒸发 😱
复位值 UARTEN=0,而我的引导序是"先打印后初始化"

这不是 bug,是**真语义第一次咬到自家引导顺序**:
以前模型"写忽略"太宽松,惯出了路径依赖
真芯片从不惯着谁——关机态就是没有输出
修法不删门,引导序回真 OS 形态:
**pl011 先行,开机后自报**——Linux 同律 📜
(驱动起完设备,才谈得上 console)

**真语义上线,第一件事就是咬自家引导顺序——
这不是倒退,是模型终于对了** 💪

回归即验收:旧 36 步一条没掉(echo/selftest/dwrite/sleep
全经 CR 门照常)门禁 36→37 双绿 ✅
三门欠账全消:DTB ✓ GICD ✓ PL011 ✓
**Linux bring-up 就绪** 🚀

每晚 1-2h 与 AI 从零造 Type-1,devlog 13 全记录。

## 配图清单

1. 封面:pl011 on: amba 00141011 自报特写
2. 身份证八字节表(PERIPHID/PCELLID → 0x00141011/0xB105F00D)
3. 引导序前后对比(被吞的输出 vs 开机后自报)
4. 旧 36 步回归 PASS 特写
5. make verify 37 步 PASS ×2

## 标签

#虚拟化 #hypervisor #ARM64 #操作系统 #内核开发 #嵌入式 #QEMU
#AI编程 #从零开始 #eshine-os #串口 #驱动开发
