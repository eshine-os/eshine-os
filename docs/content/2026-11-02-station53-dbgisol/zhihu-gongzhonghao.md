# 基准仓的头文件也会撒谎:一个位定义毒死整个启动链

站 53:补 hypervisor 的 debug/PMU 寄存器跨 VM 隔离。这是参考实现审计
报告里"两个仓得同一种病"的项:guest 共享物理 debug 寄存器,VM A
的断点设置能被 VM B 看见。

## 机制本身很直白

ARM 给了 MDCR_EL2.TDA:置位后 EL1 的一切外部 debug 寄存器访问都
陷阱到 EL2。陷阱处理器里,MDSCR_EL1 做 per-VM 仿真(每 VM 一个
槽),断点/PMU 族直接 RAZ/WI(读零写弃)——Linux 的硬件断点探测
看到 0,优雅地认为自己没有断点硬件。机制写完,真硬件上这就闭环了。

## 第一雷:QEMU 根本不实现这个陷阱

插桩一跑,handler 零触发;直接读 PMCR,读回了真 ID 值。QEMU TCG
不 enforce MDCR_EL2.TDA/TPM。但峰回路转:QEMU 的 debug 寄存器本来
就是惰性存根(MDSCR 写进去读回 0)——平台自己就没有泄漏面。
结论:机制为真硬件保留,QEMU 阶段由平台存根兜底,探针把两种形态
都报告出来。

## 第二雷:基准仓头文件的位定义是毒

照参考实现的 sysregs.h 抄 TPM=bit0、TPMCR=bit1,拼出 0x203 写进
MDCR_EL2。结果:治理链的第三个子进程无声挂死。二分定位到这一位
写入——bit0-1 根本不是 TPM 的地盘,是 HPMN(性能计数器数量)的
低两位!写入把 HPMN 改成 3,QEMU 在某个 PMU 路径上直接沉默。

教训:基准仓也是人写的。位定义这种"文档",在撞上保留字段
(HPMN[4:0])的时候必须重验——两位权威撞车,一位是抄错的。

## 收口

MDCR_GUEST=0x200(只留与 ARM ARM 一致的 TDA);dbgdemo 探针报告
平台事实;vmdbg 命令倾倒 per-VM 槽位。门禁 151→154,冒烟 14/14。
