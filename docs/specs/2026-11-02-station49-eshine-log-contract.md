# 站四十九:eshine_log 三仓统一日志契约接入(双侧)

日期:2026-11-02 | 批次 A 首站(参考实现全量差距终审 §3 批次 A)
参照:参考实现 log/ 库(自研 MIT,机制白描;零代码拷贝)——冻结 ABI=
级别枚举值/eshine_log_module_t 布局/后端钩子签名;方案 A=共享核心
(契约/格式/过滤)+各侧自实现后端钩子。

## §1 为什么

站 48 后终审(§1.2)定谳:参考实现+RTOS 双侧已 vendored 统一日志
契约(linker section 模块聚合/弱符号后端/32KiB 单写者 ring/ratelimit/
panic 通道/运行时调级),新仓零接入——现状=rt_puts 家族直出,无
模块化过滤、无 ring、无限流。三仓契约是既定方向,本站接入。

## §2 v0 形状

- `common/eshine_log.h`(契约头)+ `common/eshine_log_core.c`
  (共享核心):级别 ES_LOG_NONE..TRC(Zephyr 对齐,数值越大越
  啰嗦)/LOG_MODULE_REGISTER 入 "eshine_log_mods" linker section
  (GNU ld __start_/__stop_,不依赖 constructor——EL2 bare-metal
  安全)/弱符号后端六钩子(log_clock_us/emit_char/emit_str/
  emit_panic_char/vsnprintf/ring_store+log_halt)/调用点宏
  LOG_ERR..TRC(级别检查内联,低于阈值零参数求值)/ratelimit
  (窗口+burst+suppressed 汇总)/panic 通道(无锁后端+banner+
  ring dump+halt)/运行时控制(log_level_set per 模块、
  log_set_global_level、log_list_modules)。
- 核心自带最小 vsnprintf(%d %u %x %s %c %p %llu %llx %%)——
  两侧本无 snprintf 面(house 手搓行传统),契约需要格式化面。
- ring:32KiB 字节环覆盖最旧;`log_ring_store` 为弱钩子,默认
  raw 直写;双侧 override 为加锁窗(hypervisor=console 自旋锁窗、
  RTOS=DAIF 窗)内调 raw——SMP 多写者下环完整性由后端锁负责
  (参考实现单写者假设在 4 CPU 下不成立,站 20 实态修正)。
- 后端:`hypervisor/kernel/elog_be.c`(clock=cntpct/freq、
  emit_str=console 锁内整行、panic_char=裸 pl011、halt=wfi)+
  `rtos/rt_log_be.c`(clock=cntvct/freq、emit_str=rt_puts_n
  行级 DAIF 窗、panic_char=puts_uart、ring=DAIF 窗)。
- 消费者:双侧 boot 一行(模块计数自证);shell 四命令
  logdemo/loglevel/logmods/logdump(hypervisor=直调;RTOS=经
  demo op 蹦床 RT_U_LOG*,name 指针走 uaccess 单窗)。
- 链接:两侧 linker.ld 增 `.eshine_log_mods` KEEP 段(data 段,
  level 运行时可改)。

## §3 判据(门禁 133→139,append-only)

- [134] 宿主 boot:log core up 行+模块计数>0
- [135] 宿主 logdemo:err/wrn/inf 三行可见+dbg/trc 默认滤除
- [136] 宿主 loglevel logdemo dbg→logdemo 复跑 dbg 行现身
- [137] 宿主 logdump:环倾倒已知消息行
- [138] vm0 logdemo:rtos 侧三行+滤除面
- [139] vm0 loglevel+logmods:调级生效+模块表列出

## §4 非目标(记档)

- panic 通道判据(ESHINE_PANIC 会 halt 系统,首个真实消费者
  接入时验;log_halt/panic_char 代码完备)
- slog 持久化+klog 双源(站 50)/兼容层 LOG_INFO 旧宏(无存量)
- 逐 TU 全量迁移现有 rt_puts 调用点(按需渐进,新代码用契约)
