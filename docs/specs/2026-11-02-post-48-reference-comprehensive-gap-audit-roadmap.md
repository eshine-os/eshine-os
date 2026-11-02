# 站 48 后参考实现全量差距终审 + 迁移路线 spec（站 49-63+）

日期:2026-11-02 · 作者:Julian Chu + Claude Code
方法:四路代理普查（参考实现侧 reference-audit 报告+结构对照/用户态服务
+日志库侧/测试·构建·发布侧/bsp·3rd·参考侧）+ 树内判读,对照站 41
后审计 A-J 的站 42-48 兑现态。红线不变:机制参考、零代码拷贝、
参考实现权属私藏;派生区（其虚拟化件/ KVM-GPL、参考RTOS/Linux 派生层、samples/、
3rd/ 源码）不进。

**范围令（用户 2026-11-02）**:eshine-os 全面参考参考实现,尽可能迁移
功能直至 ready。**Type-2 形态暂不开发**——范围外记档（§2.1）。

## 0. "ready" 判读

ready = 迁移清单（§1,扣范围外+平台 N/A）逐面有站内形+门禁判据+
批验收两连真绿;范围外面在偏离设计记录与终态线记账可见（诚实账）。

## 1. 全量差距清单（五域）

### 1.1 RTOS 内核机制域——主干已平,余深水区

站 41 审计 A-I 全兑现（站 42-44）;站 45-47 补 APS/MPRT/崩溃取证/
跨 VM。**判型已对齐项（销账）**:零拷贝消息窗——msgseg 单拷+延迟
访问（站 36/37）=参考实现 VM_SET 同形,safeshm 零拷（站 47）反超其单拷。
余项（记档差异,消费者门控）:

- ptrace+GDB stub/coredump ELF 核文件/kerr 取证族（crash 环内存
  形已做,站 46;ELF 核=翻案候选——ramfs COW 可写,前提已备）
- 用户态中断接管（InterruptAttach/Wait 面;v0 设备全 EL1）
- vDSO 数据页 / hrtimer 完整栈（软定时器绝对截止账本=站 22）/ 
  物理内存工程族（buddy/SLUB/CMA/struct-page;8MB 平铺页池 v0）/
  ELF 动态链接（静态 musl v0）
- ERESTARTNOHAND/restart_block/SA_RESTART、MPRT resident 记账、
  QNX 临界线程豁免（无消费者）
- sysvipc msg/sem 号面、AF_UNIX sock 服务、sysfs EREDIRECT 形、
  tty ldisc/termios/作业控制、time settime 面
- 跨 VM 后弧:vm2 入窗/doorbell vIRQ/长消息>64B（站 47 记档）
- CFS 背景类:记档不移植（登记 #25,维持）

### 1.2 用户态服务域——资源服务主干已平,治理+契约双缺

七服务器（ramfs/tty/pipe/blkdev/proc/slog/rng）+CAPR+crash+health
覆盖参考实现开机链主干。缺口按优先级:

- **eshine_log 三仓统一日志契约**:参考实现+RTOS 双侧已 vendored
  （弱符号后端钩子冻结 ABI/linker section 模块聚合/32KiB 单写者
  ring/ratelimit/panic 通道/运行时调级）——新仓**零接入**,全仓
  现状=rt_puts 家族+slogsrv 用户环。高优。
- **slog 持久化+klog 双源+log_ctrl**:/var/log 轮转落盘（ramfs COW
  前提已备）/内核环收割/运行时控制面。高优（crash 环断电即失）。
- **driver_manager 治理面**:JSON 服务清单+有序拉起+就绪门
  （文件/chid 探测）+PID 登记+waitpid 监控+崩溃恢复+RPC 注册
  （v0 治理 busybox 服务域,服务器迁用户态=终态线另记）。定谳后弧。
- **ham 健康面**:条件-动作策略树+心跳打点+自愈守护——与治理面
  互为表里。定谳后弧。
- procfs 扩面（meminfo/cpuinfo/interrupts/ps 域,seq_file 行游标
  形）/time settime/log_ctrl:中优。
- strace/ptrace/gdb 生态、random 密码级（无硬件熵源,算法面后弧）、
  AF_UNIX sock、sysfs:低优-深水区。
- N/A:kms/TEE/HSM/hwcrypto（QEMU virt 无硬件）。

### 1.3 参考实现 hypervisor 域——骨架五域反超,隔离三缺

新仓在 VM 状态机（表驱动 vs 参考实现 §C21 病灶）、陷阱帧全 EL 共享
覆盖面、默认拒绝哲学上**反超**。真实缺口:

- **debug/PMU 寄存器跨 VM 隔离**（两仓同病,§C4/§C5）:HCR 仅
  RW|VM|IMO|TWI,无 TDA/TDB/TPM trap,帧五组不含 MDSCR/DBGBCR 族
  ——同 pCPU 多 VM 断点/PMU 互漏。高优（QEMU 可实证可门禁化）。
- **跨域 hcall fail-closed 权限门**:safeshm（站 47）后任何跨域
  op 出现前的必立门（§C7-A3 模式前置,与登记 #29 CAPR 同向）。
- **运行时 stage-2 改表守则**（§C2）:s2_lock+跨核 TLB 无效——
  demand paging/balloon 入场券;立预警不立码。
- PSCI 补面:CPU_OFF/AFFINITY_INFO/SYSTEM_OFF/SYSTEM_RESET
  （vm2 Linux 标准关机路径消费者;仿真面已就位）。中优。
- per-VM 调度配额会计（参考实现 实时配额调度 BUDGET/USED 形;CPU0 泵
  多 VM 竞争已存在）。中优。
- 事件迹环 dump/query 导出+vmstat 扩展（参考实现 stats 形等价物）。中优。
- 同病自审小项:CPU_ON 握手原子性自审/量子 tick 递减无 cycle 补偿/
  hypervisor 侧 FP eager（Q10）——记档或小站并收。
- 低优:virtio 族扩展（net/rng/console,按消费者）/vGIC maintenance
  兜底/perf 面。
- N/A:SMMUv3（真板 DMA 隔离硬前置,真板弧）/OP-TEE/EL3/ATF/
  SIP-SMC 白名单/ITS/LPI/MSI/物理直通/多板族/reserve_reboot_mem/
  用户态 vdms+virt_dom0.ko 工具栈（前提=Linux service VM 治理面,
  终态线）。

### 1.4 测试/构建/发布域——门禁与构建反超,发布工程双缺

判型:133 门 CI 内嵌（vs 参考实现权威门外置 Jenkins）+构建零补丁
（vs 78 补丁）**更优**。缺口:

- **docker 弧**（用户指令④,唯一未兑现项;参考实现亦无先例=绿地
  自建）:Dockerfile.build 钉工具链+CI 容器化（顺解 apt 漂移）+
  发布镜像 tar。高优。
- **部署件版本 pin 台账+SBOM**:musl/busybox/linux/gcc/qemu 版本
  +来源+许可清单（参考实现 3rd/manifest.yaml+manifest.json 形;其
  batch2/3 开源准备学费不重交）。高优。
- yocto 镜像聚合:ramfs.cpio 入 bitbake+machine conf+eshine-boot
  形终装+manifest.json（指令③延伸:当前层只出 RTOS 四件,ramfs/
  linux/工具在层外）。中优。
- **benchmark 延迟基线**:cyclictest 形唤醒-运行延迟直方图
  （参考实现 benchmark 359 文件样板;"QNX 味实时"卖点无数据）。中优。
- 网络/存储测试深度（随 virtio-net/fs 弧推进逐族补门）。中优。
- CUnit 白盒/LTP 成套 conformance/长稳 24h/gcov:低优（CI 成熟度）。

### 1.5 生态域——网络栈是总闸

- 现阶段可跑:cjson（治理面 JSON 消费者）/zlib（slog 压缩落盘
  消费者）——小件,随批次 A 入。
- 卡在网络栈前置:dropbear/openssl/curl/nginx/zmq——AF_UNIX 无
  硬件依赖先行;TCP/IP 栈=决策点（§5.1）。
- 真板+大内存弧:python/ROS2/ffmpeg/opencv/mesa/lvgl/fastDDS
  （参考实现大半本就未接线,manifest 记账即可）。
- bsp 真板弧:多 SoC DTS 族/板级件 驱动面/ATF 固件链/SMMUv3——
  真板阶段整弧（现阶段 N/A,台账记档）。

## 2. 范围决策

### 2.1 Type-2 范围外（用户 2026-11-02 令,入设计记录 #31）

Type-2 形态暂不开发;其机制件（virtio-net/rng、halt polling 等）
按参考实现路线图件按需吸收,不整形态搬迁。参考审计(type-2) 报告仅作机制
存在性记录;Type-2 核心价值「RTOS 主+Linux 客」已由参考实现形态覆盖
（vm0=管理 RTOS 客+vm2=Linux 客,README 目标架构定格）。

### 2.2 平台 N/A 台账（不立项,真板弧重评）

SMMUv3/OP-TEE/EL3-ATF/SIP-SMC/ITS-LPI-MSI/物理直通/多板/
kms-TEE-HSM-hwcrypto/RTC 硬件/密码级熵源/watchdog-vnet 硬件面。

### 2.3 记档偏差维持（不重开）

CFS 不移植（#25）/自有编号→真号（#27）/服务器驻内核 v0（站 31
记档;迁用户态=终态线）/#24-30 全部维持。

## 3. 迁移路线（批次 A-E;每站 DoD=站规不变:spec→实现→门禁
×2→三处同步→内容包→commit→镜像机→记忆;门禁 133 只增不减,
append-only 顺移）

### 批次 A 站 49-52「可靠面+治理面」

| 站 | 块 | 内容 | 参照（参考实现白描） | 判据形 |
|---|---|---|---|---|
| 49 | eshine_log 契约接入 | hypervisor+rtos 双侧后端钩子（cntpct/pl011、tick/uart）/LOG_MODULE_REGISTER linker section 聚合（GNU ld __start_ 段,EL2 安全无 constructor）/32KiB 单写者 ring+dump/ratelimit/panic 通道/运行时调级命令 | log/（自研 MIT,机制白描:钩子集冻结 ABI:log_clock_us/emit_char/emit_str/emit_panic_char/vsnprintf/ring_store/halt） | 双侧模块自证+panic dump+ring 倾倒门 |
| 50 | slog 持久化+procfs 扩面 | /var/log 轮转落盘+klog 内核环收割+log_ctrl 控制面;procfs meminfo/cpuinfo/interrupts/ps 域（seq_file 行游标形仿 procsrv） | slog（10MB×10 轮转/水位 flush/双源）/proc 28 文件面 | 落盘-重启-回读门+proc 文件读出门 |
| 51 | driver_manager 治理面 v0 | 服务清单 JSON 化（自研最小子集:对象/串/整/布尔,零依赖——cjson 引入留决策点 §5.2）+有序拉起+就绪门（文件/chid 探测）+waitpid 监控+崩溃恢复（重启计数上限）;v0 治理 busybox 服务域（站 40 inittab 壳代处理器升格） | driver_manager（JSON 配置/order/pre_wait/post_wait/recover/PID 登记/RPC 注册;RPC 面 v0 不立） | 杀服务自动重启+按序启动日志+就绪门 gating 三门 |
| 52 | ham 健康面 v0 | 心跳打点（软定时器 1s）+条件-动作最小形（进程消失/心跳停拍→{重启/记录/消息}）+health 面板（站 48）订阅升格 | ham（condition/action 二型+五动作型+心跳三态+自愈守护;策略树 v0 取最小子集） | 心跳停拍→动作触发门 |

### 批次 B 站 53-56「参考实现隔离+治理收口」

| 站 | 块 | 内容 | 参照 | 判据形 |
|---|---|---|---|---|
| 53 | debug/PMU 跨 VM 隔离 | HCR_EL2 增 TDA/TDB/TPM trap;MDSCR_EL1/DBGBCR/DBGBVR/PMCR 族 per-VM 帧（帧五组→六组,全 EL 共享面纪律同 vtimer 家族） | reference-audit §C4/§C5（两仓同病） | vm0 硬件断点 vm1 不可见/不互扰门 |
| 54 | 跨域 hcall fail-closed 门 | safeshm 窗访问授权表（宿主配置,默认拒绝;capability 门与登记 #29 同哲学） | reference-audit §C7-A3 模式前置 | 未授权窗访问=guest fault 隔离死门 |
| 55 | PSCI 补面+配额会计 | CPU_OFF/AFFINITY_INFO/SYSTEM_OFF/SYSTEM_RESET 最小形;per-VM CPU 泵记账 BUDGET/USED 表 | 参考实现 PSCI 面/实时配额调度形 | vm2 poweroff 真关机门+配额表可读门 |
| 56 | DFX 收口+同病自审 | vmtrail dump/query 导出+过滤;vmstat 扩展;timeslice cycle 补偿评估（记档或落码）;CPU_ON 握手原子性自审记档 | 参考实现 stats/dfx 面 | 迹环导出门 |

### 批次 C 站 57-59「IPC+网络前置」

| 站 | 块 | 内容 | 参照 | 判据形 |
|---|---|---|---|---|
| 57 | AF_UNIX sock 服务 | socket/bind/listen/accept/connect/send/recv/close 真号面（实测定号纪律,站 42）+sun_path 名字表+消息队列+accept 等待;骑站 31 fd 三元组 | unix_sock（idr 句柄+skb 队列+accept 回调） | 本机 echo 服务对往返门 |
| 58 | virtio-net 后端 | virtio 1.1 spec clean-room（net 设备 id 1+net_hdr+vq 复用站 10 机器）;QEMU -netdev 接线 | 参考实现的 virtio-net 组件 存在性记录+OASIS spec（唯一合法参照） | vm2 侧驱动绑定+帧收发设备级门（诚实形:无栈先设备级） |
| 59 | 生态小件入仓 | cjson/zlib 构建配方+ramfs 装载+demo 消费者（JSON 解析/slog 压缩落盘）;3rd 台账起步（§62 前置） | 参考实现 in_build 8 库中现阶段可跑者 | demo 两门+台账完整性校验门 |

### 批次 D 站 60-63「性能基线+发布工程」（指令③④兑现弧）

| 站 | 块 | 内容 | 参照 | 判据形 |
|---|---|---|---|---|
| 60 | benchmark 延迟基线 | cyclictest 形:周期唤起任务实测唤醒-运行延迟直方图+调度延迟分解;基线数值落盘 docs/benchmark/ | benchmark/cyclictest 形 | 直方图输出+基线趋势对照门（数值非硬门） |
| 61 | yocto 镜像聚合 | ramfs.cpio 入 bitbake image 配方+machine conf+eshine-boot 形终装（run 脚本 emit+manifest.json） | meta-eshine 镜像聚合形态 | bitbake 出全部署件+第二台 QEMU 真靴两连（站 34 同型） |
| 62 | 版本 pin+SBOM 台账 | 外部件台账（musl/busybox/linux/guest Image/gcc/binutils/qemu:版本+来源+许可）+LICENSE/NOTICE 聚合对照 | 3rd/manifest.yaml+manifest.json+prebuilts 台账形 | 台账完整性脚本校验门 |
| 63 | docker 弧 | Dockerfile.build 钉工具链→CI 容器化;发布镜像 tar（部署件+ramfs+run 入口）+publish 流 | 绿地自建（参考实现无先例;用户指令④） | docker build→run→133 门两连真绿 |

### 批次 E 站 64+「深水区（消费者门控,不预排死序）」

- E1 ptrace+gdb stub（gdb remote serial 协议 clean-room;消费者
  =strace/调试生态）
- E2 ELF core 文件（站 46 记档翻案:ramfs COW+coredump 服务+
  binfmt_elf 形;消费者=崩溃复盘离线化）
- E3 用户态中断接管（InterruptAttach/Wait;消费者=设备面迁用户态）
- E4 vDSO/hrtimer 栈/物理内存工程族/ELF 动态链接（消费者=大内存/
  真板/共享库生态）
- E5 sysvipc msg/sem、sysfs EREDIRECT、tty ldisc（消费者=参考实现
  legacy 兼容需求,默认不立）
- E6 virtio rng/console 设备（消费者=硬件熵源面/多路控制台）

### 终态线（记录不立项）

Linux service VM 治理面（vdm/ioreq 完整形态,参考实现架构差 #3 终态）/
服务器迁用户态/SMMUv3/真板 bsp/ATF/TCP/IP 栈+dropbear-openssl/
Type-2（范围外）。

## 4. 每站 DoD（站规不变）

spec 报备（含与参考实现机制差异入设计记录续册）→实现（clean-room,
参考源纪律）→门禁×2 两连真绿→三处同步（verify_el2.py/shell
help/ARCHITECTURE.md 同 commit）→内容包→commit→镜像机→记忆。

## 5. 开放决策点（报用户）

1. **TCP/IP 栈**:AF_UNIX（站 57）先行;TCP/IP 自研最小 vs lwIP
   类库评估 vs 真板弧再议——推荐:dropbear 类消费者明确再定,
   站 58 先设备级。
2. **cjson vs 自研最小 JSON**:推荐自研最小子集（零依赖,治理面
   消费者简单）;cjson 待批次 D 台账成熟按需。
3. **批次顺序**:默认 A→B→C→D→E;D（发布工程+指令③④兑现）
   可提前至 A 后即做——用户定。
4. **深水区触发**:默认消费者上门制（站 33 纪律:qemu-user
   -strace 实测召唤,不臆测）;用户可指定优先面。
