# 站五十五 devlog:PSCI 补面+泵量子会计

> 承接站 54(39490a5)。批次 B 第三站;门禁 **157→161**,冒烟 25/25
> 全 PASS。镜像机 未同步(用户令)。

## 0. 机制

- 四 PSCI 函数补面(CPU_OFF 泊/AFFINITY_INFO 查/SYSTEM_OFF 净死/
  SYSTEM_RESET 复位链);复位链=复位旗+泵出口消费(vm_finish 后
  vm_create+vm_start)——泵出口语境=内核语境,重建与 print 同锁安全。
- per-VM 泵量子会计(vm_quanta):实时配额调度 BUDGET/USED 白描的会计面;
  强制执行后弧(RTOS 侧站 45 APS 已治消费者层)。

## 1. 战雷(小雷三枚)

- vm_id_label 是 vm.c 的 static——exceptions.c 本地字符打印替代;
- 场景把宿主命令(vmstat)发给客壳——两侧命令面不同(逃生后跑);
- reset 释放 console owner——复活后重接管(vmconsole 再入);boot 窗
  竞态家族再犯(场景加 "inittab processed" settle 后接管)。

## 2. 判据与验收

冒烟 25/25(aff 三脸+quanta+复位链全弧+干净死+复活);门禁
[158-161] 写而不跑;help 161 步;三处同步同批。
