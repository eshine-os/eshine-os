# 给 hypervisor 长一个 virtio-net,然后被 Linux 教育了一晚上

站 58:virtio-net 设备模型。这次的教训密度,一个站顶前面三个。

## clean-room 的边界

OASIS virtio 1.1 spec 是唯一合法参照。参考仓的 virtio-net 组件?权属红线,
连行为参考都不进。所以每个字段都是从 spec 读出来再被实测修正的——
legacy net 头,spec 说前六字段,我数成 12 字节;插桩一打,Linux 的 tx
描述符写着 10。10+2 的 num_buffers 要 MRG_RXBUF 特性位陪绑,我们一个
特性都没谈。这就是 clean-room 的成本和它的收获:每个数字都有出处。

## LR4 一写就死

给第二个 virtio 设备找中断通道,我看到 LR0-3 都被用了,顺手写 LR4。
第一次 ping,宿主直接 EL2 异常停机。ELR 指向 msr ich_lr4_el2,ESR
0x02000000——QEMU 的虚拟 GIC 接口只实现了 4 个 list register。

站 53 的 MDCR 撞位刚教过"平台假设必须实测",这里又犯:我臆断 QEMU
有 16 个 LR。修法是读 ICH_VTR_EL2 探测真实数量,够 5 个用 LR4,只有
4 个就和 vblk 共享 LR3——同物理 CPU 语义上不可能同时挂起,边界记档。
顺手给 el2_sync 加了 ELR 和 EC 打印,这行打印就是定谳 LR4 的那把刀。

## 九刀

设备通了,Linux 的 virtio_net 驱动自己绑上来,MAC 读回来就是配置区
写死的六个字节。然后 ping 一下,内核 Oops——在 netif_receive 的第二
条指令上执行 DTB 字符串。

二分从这开始,九刀:

1. 撤 vIRQ:活
2. IRQ+空 poll:活
3. rx 完成不写:活
4. 写完成不带帧:活
5. 零帧:活
6. 改他方 IP:死
7. 改单播 DST:死
8. 改 ARP 应答:死
9. 节流闸:死

矩阵收敛到唯一变量:ethtype 0x0806。任何 ARP 帧回环递送进去,这个
6.6 内核就死;换成 IP 头,完整链路活得好好的。中间还有两枚工具雷:
shell 分词把我的 gdump 命令吃了(站 54 记过档的病,复发);慢打印
把 MMIO 写拖过 watchdog,watchdog 把环表改了,我拿到的字节倾倒是
尸体数据——取证桩必须单行化。

host 侧新写了 gdump(十六进制倾倒客 RAM),崩前崩后各拍一张:物理
文本干干净净,纹丝不动。客 stage-1 和我 stage-2 之间有个映射分歧,
机制没有穿透——记档,不臆断。

## 诚实的收口

真实 NIC 从来不收自家出向的广播——回环把 ARP 请求原样弹回发送者,
这个语义本身就是病态的。v0 丢弃 ARP 回声(计数器记丢),TX 侧用
/proc/net/dev 的 42 字节/1 包做设备级铁证;完整 RX 递送留给原路设计:
宿主侧接 QEMU -netdev 的桥接弧。

门禁 167→171。批次 C 过半,下一站 cjson+zlib 台账。
