# 站四十九 devlog:eshine_log 三仓统一日志契约接入(双侧)

> 承接站 48(批收口,133 门)。批次 A 首站(参考实现全量差距终审
> §3 批次 A「可靠面+治理面」);门禁 **133→139**(append-only),
> 批验收纪律:写而不跑,冒烟电池自证当场抓雷。镜像机 未同步
> (代理退出,用户令明早统一同步)。

## 0. 契约形态(参考实现 log/ 机制白描,零代码拷贝)

- **方案 A**:共享核心(契约/格式/过滤)+各侧自实现后端钩子。
  `common/eshine_log.{h,core.c}` 双树单源——一源两物
  (elog_core_el2.o/elog_core_rt.o):契约单源与构建隔离两立。
- **冻结 ABI**:级别枚举值(ERR=1..TRC=5,数值越大越啰嗦,Zephyr
  对齐)/`eshine_log_module_t` 布局(name+level+pad[7])/六后端钩子
  签名(log_clock_us/emit_char/emit_str/emit_panic_char/vsnprintf/
  ring_store+log_halt)。
- **模块注册**:LOG_MODULE_REGISTER 入 "eshine_log_mods" linker
  section,GNU ld 自动聚合 __start_/__stop_(双侧 linker.ld KEEP);
  不依赖 constructor——EL2/EL1 bare-metal 不保证运行。
- **调用点宏内联级别检查**:低于阈值零参数求值零函数调用。

## 1. 机制修正两笔(白描对实态)

- **"单写者 ring"假设在 4 CPU 下不成立**(参考实现的 32KiB 单写者环
  假设单写者;我们 4 CPU 多任务并发 log)——修法:`log_ring_store`
  为弱钩子,默认 raw 直写;双侧 override 为锁窗内调 raw(宿主=
  console 锁窗,RTOS=自旋锁窗)。参考实现弱钩子的本意正是让各仓挂
  自带锁环,站 20 实态顺着契约原意落。
- **核心自带最小 vsnprintf**(%d/%u/%x/%s/%c/%p/%%,l/ll/z 收纳):
  两侧本无 snprintf 面(house 手搓行传统),契约 API
  (LOG_INF("x=%d",x))需要格式化面——80 行手搓,边界纪律
  (put_ch 逐字节护栏,未识 spec 原样两字符)。

## 2. 接入面

- 后端:宿主 elog_be.c(cntpct/freq 时钟+console 行级锁窗整行+
  ring console 锁窗);RTOS rt_log_be.c(cntvct/freq 时钟+
  rt_puts_n DAIF 行级窗+ring 自旋锁窗)。panic 通道代码完备
  (ESHINE_PANIC/BUG_ON+无锁后端+环倾倒+halt),无判据=记档
  非目标(会 halt 系统,首个真实消费者接入时验)。
- 四命令双侧:宿主直调;RTOS 经 sys_demo 蹦床(RT_U_LOG*
  追加枚举尾=wire ABI 稳定;loglevel 参数串整串直递内核解析,
  k_spawn 同律)。logdemo=三级现身+dbg/trc 默认 INF 滤除(禁现
  判据)+ratelimit 3/10 面;loglevel=全局与 per 模块调级;
  logmods=段聚合模块表;logdump=环倾倒。
- boot 自证:双侧 banner 后 LOG_INF "log core up, mods=2"(模块
  计数=linker section 聚合自证,门 [137] 环审计复验)。

## 3. 战雷(纪律进账)

1. **Makefile 首目标劫持**(站 49 战雷第一枚,静默三连):
   对象规则插在 `all:` 之前→规则成为 Makefile 首目标=make 默认
   goal→`make` 只编 elog_core_el2.o 即报"已是最新"收工——
   elog_be.o 从未编译、main.o 陈旧、elf 不重链,**构建静默假绿**。
   首发靠 objdump 段聚合验证抓到(段不存在→顺藤摸瓜)。纪律:
   **规则必须落 all: 之后;新目标的"什么没被编"比"什么被编了"
   更要眼见为实**。修复时二连雷:插的 `all:` 在 TARGET 定义前
   展开 $(TARGET)=空——靠正主 all 合并依赖侥幸,整块重构收口。
2. **冒烟驱动排空窗竞态**(自建工具自雷):read_until 命中后
   0.25s 排空窗内提前到达的下一条输出落在下一步 pos 之前——
   等待型步骤对已来的输出永超时。修法:命令纪元(pos 于 send
   时刻定格,等待型步骤从纪元起搜)。
3. **IDE clangd 全项目无 compile_commands 噪音**:每文件几十条
   假错(-std=c++11/include 找不到)——**以 make 为唯一真值**,
   IDE 诊断不进判断流。

## 4. 验收

- 冒烟 24/24 全 PASS(smoke.py+场景文件,双侧全脸:三级/滤除
  禁现/调级生效/模块表/环倾倒/限流计数/vm0 蹦床全套)。
- 门禁 [134-139] 写而不跑(批验收纪律);help 走查 139 步活体
  对齐(scene-help 4/4);ARCHITECTURE 验证节+站 49 段同 commit。
- 段聚合终验:双侧 ELF eshine_log_mods 段 0x20=2 模块+
  __start_ 符号(objdump/nm 双证)。
