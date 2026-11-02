# VM 的四种"离开":PSCI 补面小记

站 55:hypervisor 的 PSCI 面补齐。之前只有 CPU_ON(次核上线)和
VERSION——一个 VM 只能被宿主"处决"(vmkill),不能自己体面地走。

## 四个函数,四种语义

**CPU_OFF**:调用者这个帧泊停(核 0 恒在——主核不许自己下线)。
**AFFINITY_INFO**:某个帧还在不在(0=ON)。
**SYSTEM_OFF**:整个 VM 干净关机——走 VM_DEAD,不走 fault 路径。
关机和事故死是两种死:关机没有 ESR/FAR 取证行。
**SYSTEM_RESET**:teardown + 重建 + 重启三段式。实现是"复位旗 +
泵出口消费":PSCI 处理器只立旗和退出,泵在 VM 生命周期收尾处看到
旗,原地重建重启——重启逻辑长在生命周期机器里,不长在异常路径里。

## 量子账

参考实现的 实时配额调度 有 BUDGET/USED 配额会计。我们的最小形:每个 VM
跑一帧泵量子就 +1,vmstat 一行可见。先记账后执法——强制节流等
真实消费者(RTOS 侧的 APS 分区已经治了消费者层)。

## 实测

AFFINITY_INFO 三脸:vcpu0=0、vcpu1=0、无效帧=0xFFFFFFFD。
pscireset:关机消息 → 重建 → 完整再启动(自报链全绿)→ 控制台
重接管。pscioff:干净死,无 fault 行,复活链就绪。冒烟 25/25,
门禁 157→161。
