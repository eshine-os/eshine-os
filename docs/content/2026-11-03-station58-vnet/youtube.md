# YouTube 分镜(站 58 virtio-net,九刀二分之夜)

标题:给 hypervisor 长 virtio-net,然后被 Linux 教育一晚上

## 分镜

1. **冷开场(0:00-0:40)**:eth0 出现在 vm2 的 /sys/class/net+MAC 读回
   ——Linux 自己的 virtio_net 驱动绑上来了(实机演示)。
2. **clean-room 边界(0:40-1:40)**:OASIS spec 是唯一参照;legacy 头
   10B 的插桩翻案(12 是臆断,num_buffers 要 MRG 特性位陪绑)。
3. **LR4 之死(1:40-3:00)**:msr ich_lr4_el2 → EL2 异常现场;ELR+EC
   打印(本站新增)如何一步定谳;VTR 探测修法+LR3 共道边界。
4. **九刀矩阵(3:00-6:00)**:ARP 回环死亡的二分全表——撤 vIRQ/空
   poll/零帧/IP 帧全活,ARP 全死;矩阵收敛到唯一变量 ethtype。
5. **取证工具学(6:00-7:30)**:gdump(客 RAM 倾倒);慢打印时间膨胀
   →watchdog 撕环表→尸体数据——取证桩单行化纪律。
6. **诚实收口(7:30-8:30)**:回环回 ARP 本就病态(真实 NIC 不收自家
   出向广播);v0 丢弃边界+42B TX 铁证;完整 RX 走 netdev 桥接弧。

## 素材清单

- QEMU 实机:eth0+MAC+ifconfig+ping+/proc/net/dev(42B/1 包)
- gdump 输出(ARMd magic+崩前崩后文本对照)
- 定谳矩阵表(九刀)
- ARCHITECTURE.md 站 58 段落
