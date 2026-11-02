# 站五十五:PSCI 补面+per-VM 泵量子会计

日期:2026-11-02 | 批次 B 第三站
参照:reference-audit 审计中优项(PSCI 补面:CPU_OFF/AFFINITY_INFO/
SYSTEM_OFF/SYSTEM_RESET——vm2 Linux 标准关机路径)+参考实现 实时配额调度
(BUDGET/USED 配额会计形)。

## §1 v0 形状

- CPU_OFF:调用者帧泊停(wait 无源泊,核 0 恒在拒;SGI 到发可再醒=
  记档,真消费者召唤再硬化);AFFINITY_INFO:客视角 MPIDR 帧号→
  0=ON/DENIED;SYSTEM_OFF:干净 VM_DEAD(无 fault 行);
  SYSTEM_RESET:复位旗+泵出口复位链(teardown 后 vm_create+
  vm_start 重建重启)。
- per-VM 泵量子会计(vm_quanta,泵每跑一帧量子+1;vmstat quanta 行;
  强制执行=消费者召唤后弧记档)。
- pscidemo/pscireset/pscioff 三命令(EL1 hvc 直调)。

## §2 判据(门禁 157→161,append-only)

- [158] pscidemo -> AFFINITY_INFO 三脸(0/0/0xFFFFFFFD)
- [159] vmstat -> quanta 行(per-VM 会计)
- [160] pscireset -> 复位链+re-boot(console 重接管)
- [161] pscioff -> 干净 VM_DEAD+复活链

## §3 非目标(记档)

- CPU_OFF 泊的 SGI 再醒硬化/MIGRATE_INFO_TYPE/PSCI_FEATURES 逐位
  (现恒 ~0)/量子超支强制节流(RTOS 侧 APS(站45)已治本层=消费者)
