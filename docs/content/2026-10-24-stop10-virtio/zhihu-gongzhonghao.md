<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 自制协议退役,virtio 接棒:数据一个字节不动

> 27.04 连载第 10 篇。第九站的 vblk 是我自制的协议——门铃加内存
> 命令块,五分钟设计,没人规定它长什么样。这一站把它退役,换成
> virtio:virtio-mmio legacy 传输 + split virtqueue + virtio-blk。
> 最好的部分是迁移本身:同一块磁盘,协议栈整个换掉,**数据一个
> 字节不动,guest 的输出判据原封不动**。

## 先说结局

    [vm0] virtio blk on        ← probe 三验 → Status 步进 → DRIVER_OK
    vm0> dwrite
    blk wrote                  ← 三描述符链 → 门铃 → 完成中断
    vm0> dread
    blk read: ESHINE-BLK-persist

和第九站一模一样的输出——因为门禁判据**原封不动**地迁移了。
协议栈从自制 vblk 换成行业标准 virtio-blk,而 `blk wrote`/
`blk read: ESHINE-BLK-persist` 这两个 needle 一字未改。销毁重建,
旧数据照旧在:第九站写入的内容,跨过一次协议栈更换,活到了
virtio 时代。

这就是"判据同强度迁移"——门禁只增不减纪律下,更换协议的
最优雅姿势:**行为不变,实现换了**。

## 为什么要退役自己的协议

自制协议的好处是五分钟能跑,坏处是**全世界只有你一个用户**。
guest 驱动、host 设备模型、测试判据,三点一线全是私有约定,
每次改动都要两侧同步。而 virtio 是行业标准:Linux/KVM 的
块设备就是它,guest 内核里有现成驱动,规范公开可查。

回头看,自制 vblk 是给 virtio 打的草稿——"内存即传输面,寄存器
即门铃"那八个字,本来就是 virtqueue 的哲学。种子长成真身的
过程里,第九站的每个设计决策都有了标准答案:

| 自制 vblk | virtio 对应物 |
|---|---|
| 内存命令块(CMD/ADDR/STATUS) | 三描述符链(header/data/status) |
| CMD 寄存器写一下 | avail 环入队 + QueueNotify 门铃 |
| 完成中断 vSPI 34 | 完成中断 vSPI 35(新号与旧代区分) |
| 磁盘静态数组 | 磁盘静态数组(**原封承接**) |

## virtio-mmio legacy:寄存器最小集

传输面是 MMIO 上的一个寄存器块,最小集十来个字:

- **probe 三验**:guest 先读 Magic(0x74726976)、Version(2)、
  DeviceID(2=块设备),确认"你是 virtio、你是 legacy、你是磁盘"
  ——设备身份不是假设出来的,是握手出来的。VendorID 我填了
  0x4553(ES)。
- **Status 步进**:ACK → DRIVER → FEATURES → DRIVER_OK,每步
  都回写状态寄存器,双向可见的协商最小实现。本站零特性协商
  (YAGNI,单队列单扇区用不上)。
- **队列三件套**:QueueSel 选队列 → QueueNumMax(8)报告深度 →
  **QueuePFN 交付**——legacy 的交付单位是**物理页帧号**:guest
  把整块队列区(8KB 对齐 4096)的 PFN 写进去,设备从此知道
  队列在 guest 物理内存的哪里。
- **DRIVER_OK 兼任中断使能**:virtio-mmio 没有 IMSC——驱动
  就绪即许可中断。语义比真 PL011 干净:没有"驱动了但没就绪"
  的中间态。

## split virtqueue:布局公式

legacy 布局是一张三段式公式,给定队列深度 N:

    desc 表    @ +0      (16 字节 × N:addr/len/flags/next)
    avail 环  @ +16N     (6 字节头 + 2 字节 × N,2 对齐)
    used 环   @ (16N + 6 + 2N + 4095) & ~4095   (4096 对齐)

三个角色:**desc 是自由链表**(每请求借三节:header→data→
status,设备回写 status 字节报 OK/ERR),**avail 是 guest 的
提交队列**(入队一个 desc 链头),**used 是设备的完成队列**
(append 一条 {id, len})。门铃响后宿主扫 avail → 逐链解析 →
扇区 I/O → used append → 置挂起;guest ISR 读 InterruptStatus
→ ACK → 查 used。

**宿主与 guest 两侧自洽**——两侧用同一张公式表,一侧造链,
一侧拆链。与 Linux 驱动的严格对齐(特性协商/真 DMA)留给
后续站。

## 战况:两个,全在构建期

**链接器摆放陷阱**。首版队列区用三个独立静态变量
(desc/avail/used 各自 `aligned(4096)`)——链接时 `aligned` 只保
证**对齐**,不保证**相邻**:used 不保证恰在基址+4096,链接器
完全可以在中间塞进其他 .bss 项,布局公式当场作废。修法:单块
8KB 静态区,desc@+0/avail@+128/used@+4096 按公式落位。教训
普适:**不把结构布局交给链接器摆放**——布局公式必须落在同一
块物理连续区上。

**宏展开优先级**。`#define g_avail ((struct vq_avail *)(g_vq+128))`
生成的是指针,后面写 `.idx` 直接编译错——指针宏配箭头 `->`。
编译器把这个错抓在了构建期,零运行时成本。

两处无一运行时 debug。九、十两站连绿,"首测一次全绿"从偶发
变成节奏:设备站的难点早已在六、八站付清(MMIO 分发/ELR 不
退休/SRT 回填/asm 访存器契约/LR 正形/泵注入纪律),后续设备
站越来越像"在新地基上按图施工"。

## 数字

- 门禁 31 步判据同强度迁移 ×2 双绿(`blk irq on`→
  `virtio blk on`,其余 needle 原封)
- vblk 自制协议退役:vblk.c/vblk.h/guest blk.h 删除;磁盘数组
  原封承接
- LR 通道:vSPI 35 走 LR3(旧 34 代际退役,LR2 空置)——
  a57 四槽的历史结构正好给设备代际留了考古层
- 已知限制(登记 #14):零特性协商/单队列深度 8/同步服务/
  静态翻译直访(真 DMA/多队列/packed/MSI 留后续)

代码、devlog 10:github.com/eshine-os/eshine-os · www.eshine-os.org。
每晚 1-2 小时,与 AI 结对,从零造 Type-1。

### 平台微调说明

- **知乎**:全文可发;自制 vs virtio 对照表 + "迁移即结构
  连续"是传播点
- **公众号**:布局公式做成三段式图;probe 三验做成握手时序图
- **B 站**:见 youtube.md,链接器摆放陷阱做核心分镜
