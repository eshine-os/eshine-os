# 站 58:virtio-net 设备(内容包口径)

## 一句话

hypervisor 长出第二个 virtio 设备:Linux 自己的 virtio_net 驱动来绑——
九刀二分才定谳:回环不能回 ARP。

## 技术叙事

1. **clean-room**:OASIS virtio 1.1 spec 是唯一参照(参考仓的 virtio-net 组件
   权属不许碰);设备 id 1、F_MAC、10 字节 legacy 头——每个字段都
   被插桩定谳过一次(12 字节头就是臆断,实测 10)。
2. **绑定即判据**:Linux 的 virtio_net 驱动自己发现设备、协商特性、
   读 MAC、建 vq——eth0 出现在 /sys/class/net,MAC 是我们配置区
   写死的那六个字节,这就是设备模型的满分答卷。
3. **九刀**:回环递送 ARP 帧会让 6.6 在 netif_receive 执行 DTB 字节
   死掉。二分矩阵:撤中断=活,空 poll=活,零帧=活,IP 帧活,
   ARP 必死(请求应答、自 IP 他 IP、单播广播全试过)。
4. **诚实收口**:真实 NIC 本来就不收自家出向广播——回环回 ARP 本就
   病态。v0 丢弃 ARP 回声,42 字节 TX 计数器是设备级铁证;完整 RX
   递送留给宿主侧 netdev 桥接弧(原路设计)。

## 金句

- 平台假设必须实测:LR4 一写就死,VTR 一读才知道 QEMU 只给 4 个。
- 二分到第九刀,才配说"平台边界"四个字。

## 数字

- 门禁 167→171;冒烟 12/12;eth0 TX=42B/1 包。
- 9 刀二分;LR 通道 VTR 探测;gdump 取证命令入库。
