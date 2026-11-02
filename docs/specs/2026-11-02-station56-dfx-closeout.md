# 站五十六:DFX 收口(迹环过滤查询+per-VM 抢占会计+同病自审定谳)

日期:2026-11-02 | 批次 B 末站(53-56 参考实现隔离+治理收口批)
参照:reference-audit 审计中优项(迹环 dump/query 工具面+vmstat 扩展)+
同病自审项 §C23/§C25/§Q10。

## §1 v0 形状

- vmtrail 查询面:`vmtrail [N] [cpu0-3]`——CPU 过滤=vm 过滤的拓扑
  近似(cpu1/2=vm2、cpu3=vm0.v1 专泵;cpu0=vm0.v0+vm1 双客混载,记档)。
- per-VM 抢占会计:vm_preempts(双扳机点=量子耗尽泵侧+帧转写异常侧),
  vmstat 双会计行(vm-preempts+quanta)。
- 同病自审三件定谳(记档):
  - §C25 量子记账无 cycle 补偿:granularity 即量子本身(1 tick/片),
    无累积损失路径;亚拍量子再议。
  - §C23 CPU_ON 握手原子性:有界等待兜底(丢拍降级不挂死);真板
    event register 语义差异真板弧重审。
  - §Q10 宿主侧 FP eager:TCG 非性能目标;RTOS 侧已 lazy(站19C);
    真板弧。

## §2 判据(门禁 161→164,append-only)

- [162] vmstat -> vm-preempts+quanta 双会计行
- [163] vmtrail N cpu3 -> 仅 cpu3 行(cpu0 泄漏断言)
- [164] vmtrail N cpu0 -> 仅 cpu0 行(cpu3 泄漏断言)

## §3 非目标(记档)

- 迹环条目入 vmid 域(cpu 近似记档)/per-VM wait/exit 计数
