# 站五十三:debug/PMU 寄存器跨 VM 隔离面(批次 B 首站)

日期:2026-11-02 | 批次 B 首站(参考实现隔离+治理收口)
参照:reference-audit 审计 §C4/§C5"两仓同病"(HCR 无 debug 陷阱位,帧
不含 MDSCR/DBGBCR 族——同 pCPU 多 VM 断点/PMU 互漏)。

## §1 v0 形状(真硬件机制+QEMU 平台实证)

- **机制**:MDCR_EL2.TDA(bit9)guest 进场置/宿主还场清;EC 0x18
  收口——MDSCR_EL1(sysreg 2,0,c1,c3,2)per-VM 代访(帧新增 mdscr
  槽,vm_create 清零起步);debug 族(crn 0-2:DBGIDR/DBGBCR/BVR/
  WVR/WCR/OSLAR...)+PMU 族(op0=3,op1=3,crn 9/14)RAZ/WI(读 0/
  写弃——Linux hw_breakpoint 探测见 0=优雅降级,计数互漏根除)。
- **QEMU TCG 平台定谳(插桩实证)**:不实现 MDCR_EL2 陷阱
  enforcement(handler 零触发);MDSCR=惰性存根(写弃读 0),PMCR=
  ID 真值可读计数惰性=平台级无泄漏——机制真板弧就绪,QEMU 阶段
  由平台存根兜底。
- **参考实现头 TPM=bit0/TPMCR=bit1 与 HPMN[4:0] 撞位记档**:0x203 写入
  把 HPMN 改 3=QEMU 客进程挂死毒源(实测定谳);TPM 位号存疑,
  真板弧重核。MDCR_GUEST=0x200(仅 TDA)。
- dbgdemo 探针(trapped/quiescent+raz/id 双形态平台事实报告)+
  vmdbg 槽倾倒+vm1 boot 探针(guest/main.c)。

## §2 判据(门禁 151→154,append-only)

- [152] vm0 dbgdemo -> 平台探针(quiescent+id readable;真板变体
  =trapped/raz)
- [153] host vmdbg -> per-VM mdscr 槽倾倒(TCG 全 0=陷阱惰性)
- [154] vm1 boot -> mdscr probe 0x0(第二客视图,平台存根隔离)

## §3 非目标(记档)

- TPM/TPMCR 位号(HPMN 撞位,真板重核)/DBGBCR/BVR per-VM 真仿真
  (QEMU 无陷阱面;真板弧)/PMU per-VM 计数器仿真(同)
