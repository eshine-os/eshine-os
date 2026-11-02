# 照着基准仓抄位定义,第三个子进程就挂死了 😵

站 53:补 debug/PMU 寄存器跨 VM 隔离(参考实现审计"两仓同病"项)。

照参考实现头文件抄:TPM=bit0、TPMCR=bit1 → MDCR_EL2=0x203
→ 治理链第三子进程无声挂死 💀

二分定谳:bit0-1 是 HPMN(计数器数量)的地盘!写入把 HPMN 改成 3,
QEMU 在 PMU 路径上沉默。

而真正的机制位 TDA=bit9,QEMU TCG 根本不实现它的陷阱
(插桩零触发)——但 QEMU 的 debug 寄存器本来就是惰性存根,
平台级无泄漏 😮‍💨

收口:0x200 仅 TDA + 平台探针 + vmdbg 槽倾倒。
基准仓也是人写的,撞保留字段的位定义必须重验 ⚠️

#嵌入式 #hypervisor #ARM #cleanroom #buildinpublic
