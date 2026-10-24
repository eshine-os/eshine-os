<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Devlog 10 — virtio 真身站（virtio-mmio legacy + split virtqueue）

日期:2026-10-24 · 版本:27.04-dev · 门禁:31 步真绿 ×2 连过（判据同强度迁移）

## 里程碑

第九站的"virtqueue 种子"长成行业标准设备——guest 从此说 virtio：

    [vm0] virtio blk on          ← probe 三验(magic/version/DeviceID)
                                   → Status 步进 → 队列就位 → DRIVER_OK
    vm0> dwrite                   ← 三描述符链(header/data/status)
                                    → avail 入队 → 门铃(QueueNotify)
    blk wrote                     ← 完成中断(vSPI 35)→ ISR 清挂起 → 回报
    vm0> dread
    blk read: ESHINE-BLK-persist  ← 同一静态磁盘:自制 vblk 时代移交,
                                    数据一个字节不动
    （销毁 → 重建）
    vm0> dread → blk read: ESHINE-BLK-persist   ← 持久化照旧

## 形态（用户四决策全选推荐）

- **virtio-mmio legacy**：寄存器最小集（magic/version/DeviceID/
  QueueSel/QueueNumMax/QueuePFN/QueueNotify/InterruptStatus-ACK/
  Status），无 MSI 无协商字节；**Status.DRIVER_OK 兼任中断使能位**
  （virtio-mmio 没有 IMSC——驱动就绪即许可，语义更干净）。
- **split virtqueue（legacy 布局公式）**：desc 表 16N、avail 紧随
  （2 对齐）、used 环 4096 对齐。宿主与 guest 驱动**两侧自洽**
  ——自制两侧用同一张公式表（Linux 驱动对齐留站后）。
- **三描述符链**：virtio-blk 规范形态——header（type/sector）→
  data（512B）→ status（设备回写 OK/ERR）。每请求一条链，门铃即
  服务（扫 avail→逐链解析→扇区 I/O→used 环 append→挂起）。
- **退役与移交**：vblk 自制协议删除（vblk.c/vblk.h/guest blk.h），
  virtio-blk **承接同一静态磁盘**——迁移即结构连续。门禁判据同强度
  迁移：`blk irq on`→`virtio blk on`，`blk wrote`/`blk read:` 输出
  needle 原封不动（行为不变，协议栈换了）。
- **完成中断 vSPI 35（LR3）**：新号与 34 代际区分（用户批准）；
  通道编号化收束 LR0=vtimer(HW)/LR1=vuart/LR3=virtio，LR2 随 vblk
  空置——a57 四槽的历史结构正好给设备代际留了考古层。

## 本站战况（很少，但有含金量）

1. **链接器摆放陷阱**（构建期抓获）：首版队列区用三个静态变量
   （desc/avail/used aligned(4096)）——**used 不保证恰在基址+4096**，
   链接器可在中间塞其他 .bss 项，布局公式即破。修法：单块 8KB
   静态区按公式落位（+0/+128/+4096），**不把结构布局交给链接器
   摆放**——宿主与 guest 两侧的公式必须落在同一块物理连续区上。
2. **宏展开优先级**（编译期抓获）：`#define g_avail ((struct ...*)(...))`
   生成的是指针，用法点号 `.idx` 直接编译错——指针宏配箭头，
   编译器替我把这个错抓在了构建期，零运行时成本。

## 第二座"首测一次全绿"

手测五链（probe/三链写/读完整/销毁/重建持久化）一次全过。九、十
两站连绿的模式已经清晰：**设备站的难点早已在六、八站付清**
（MMIO 分发/ELR 不退休/SRT 回填/asm 访存器契约/LR 正形/泵注入
纪律），后续设备站越来越像"在新地基上按图施工"——怀疑论的红利
期。

## 已知限制（#14 记录）

- 零特性协商、单队列深度 8、同步服务、静态翻译直访（真 DMA/多队
  列/packed/MSI 留后续站）
- legacy 布局与 Linux 驱动的严格对齐留真机/Linux guest 站
