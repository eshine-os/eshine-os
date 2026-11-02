# 站五十三 devlog:debug/PMU 隔离面(批次 B 首站)

> 承接站 52(c2d714b)。门禁 **151→154**,冒烟 14/14 全 PASS。
> 镜像机 未同步(用户令)。

## 0. 机制(审计"两仓同病"项收口)

- MDCR_EL2.TDA guest 置/宿主清;EC 0x18:MDSCR per-VM 代访(帧新
  mdscr 槽)+debug/PMU 族 RAZ/WI。真硬件机制完备。

## 1. 战雷(两枚,均平台级定谳)

1. **QEMU TCG 不实现 MDCR_EL2 陷阱**(插桩零触发+PMCR 读回真
   ID 值定谳)——站改域:机制照码(真板就绪),dbgdemo 改平台探针
   (trapped/quiescent 双形态)。QEMU 阶段 debug/PMU=平台惰性存根
   (写弃读 0/ID 可读计数惰性)=无泄漏,平台兜底。
2. **参考实现头 TPM=bit0 毒雷**:0x203(照参考实现头)写入把 HPMN[4:0]
   改 3——QEMU 客进程(governor 链第三子)挂死;基线二分+逐位排
   除定谳,MDCR_GUEST=0x200(仅 TDA)。**基准仓的头文件也会撒谎
   (位号与 HPMN 撞位);撞 HPMN 的 MDCR 位定义=红灯**。
3. (小雷)治理 marker=initmark 重跑撞 -EEXIST——清单 marker 换
   hello(create 语义狭隘非清单服务之过)。

## 2. 判据与验收

冒烟 14/14(dbgdemo 探针三行+vmdbg 槽倾倒+vm1 probe+逃生回宿主);
门禁 [152-154] 写而不跑;help 154 步;三处同步同批。
