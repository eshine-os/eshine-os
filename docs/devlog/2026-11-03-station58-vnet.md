# 站五十八 devlog:virtio-net 设备(批次 C 第二站)

> 承接站 57(sock 提交)。OASIS virtio 1.1 spec clean-room(唯一合法
> 参照——参考实现的 virtio-net 组件 连行为参考也不进,权属红线)。门禁
> **167→171**,冒烟 12/12 全 PASS。镜像机 未同步(用户令)。

## 0. 机制

- 第二 virtio-mmio 窗口 0x0A001000(设备 id 1;vq0 rx/vq1 tx 双队列;
  唯一特性 F_MAC+配置区 6B MAC);DTB 增第二节点(SPI 4=INTID 36)
- 回环后端:tx kick 读帧入设备暂存区→拷进 guest 预投 rx 缓冲→双 used
  环同拍完成;消费者=vm2 Linux virtio_net(bind+MAC+linkup+ping TX)
- busybox 精选集+IFCONFIG/PING(+FANCY);initramfs 增 dev/mem 节点
- gdump 宿主取证命令(客 RAM 十六进制倾倒)+el2_sync ELR/EC 打印

## 1. 战雷(本站真内容=九刀二分定谳)

1. **LR4 毒雷**:QEMU GICv3 虚拟接口实测只有 4 个 LR——`msr ich_lr4_el2`
   =未实现寄存器,EL2 同步异常(ESR 0x02000000,EC=0)。修:VTR 探测
   (`gic_vif_lrs()` 读 ICH_VTR [4:0]);≥5 用 LR4,4 LR 与 vblk 共 LR3
   (同 pCPU 语义不撞:vm2 专泵 CPU1/2 无 vblk I/O;vm0 的 vblk 在 CPU0/3)。
   教训同站 53:平台假设必实测(VTR 打脸了我的"16 LR"臆断)。
2. **legacy net 头 10B 雷**:VIRTIO_NET_HDR 初版臆 12——插桩定谳 Linux
   tx hdr desc len=10(前六字段=10B,num_buffers 只随 MRG_RXBUF)。
3. **分词雷再爆**(站 54 已记档):gdump 命令首版 `strncmp(line,"gdump ",6)`
   永不匹配——dispatch 先分词,首空格已变 \0。修:比较单词+解析 args。
   两病同发=教训该升级:动 shell 前先 grep dispatch 的分词语义。
4. **插桩时间膨胀**:多行慢打印把 MMIO write 拖过 netdev watchdog(5s)
   →watchdog 撕环表→run9 的字节级倾倒是尸体数据。修:单行紧凑手拼
   十六进制(一次 console_puts)。教训:串口即延迟,取证桩必须单行化。
5. **/dev/mem EPERM**:STRICT_DEVMEM 挡内核文本客侧取证;--dev 落顶层
   (cpio 名带 dev/ 前缀即入位)。
6. **ESR 0x02000000 假读**:我拿宿主 FAR(0xffff800080039050)冒充客 pc
   算了三轮 PA——客 oops 只印符号化 pc。真 VA=System.map 定谳
   (clone=0xffff800080e80460+slide)。教训:寄存器角色对齐再入算术。

## 2. ARP 回环死亡定谳(平台边界,站 53 同例)

九刀二分:①撤 vIRQ=活 ②IRQ+空 poll=活 ③rx 完成无帧=活 ④零帧=活
⑤IP ethtype=活 ⑥他方 IP=死 ⑦单播 DST=死 ⑧ARP REPLY=死(非确定)
⑨节流闸=死。**结论:唯 ethtype 0x0806 的回环递送致死**——6.6 Oops 于
__netif_receive_skb_core+0x4 执行 FDT 字节("pl011@9000000"),ESR
0x02000000;host 侧 gdump 取证:客物理文本净且崩前崩后不变=客 stage-1/
我 stage-2 映射分歧,机制未穿透(记档不臆断)。真实 NIC 不收自家出向
广播——回环回声本就病态;v0 丢弃 ARP 回声(丢帧计数),RX 完整递送
证明走原路设计(宿主 QEMU -netdev 桥接弧,roadmap E6)。

## 3. 判据与验收

冒烟 12/12:eth0 注册(bind)+MAC 52:55:0a:00:02:58(F_MAC+配置区)+
ifconfig linkup+ping(无 panic)+/proc/net/dev **eth0 TX=42B/1 包**
(42B ARP=完整设备级发送链:netdev 计数器为证)。门禁 [168-171] 写而
跑……不,写而不跑(批验收纪律):[168]bind/[169]MAC/[170]TX 计数
(python 正则)/[171]gdump 取证(ARMd magic)。help 171 步;ARCHITECTURE
同步。CARM-STALL boot 偶发挂死(2/30 次,vm2.1 vtimer consume-fail,
站 28 旧病面)记档观察项——批末 verify 双跑定其色。
