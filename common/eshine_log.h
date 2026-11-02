/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/*
 * eshine_log.h — eshine 统一日志契约(hypervisor/rtos 双树共享)
 *
 * 形态:共享核心(common/eshine_log_core.c:契约/格式/过滤)+各侧
 * 自实现后端钩子(方案 A,参考实现 log/ 机制白描)。冻结 ABI:级别枚举值/
 * eshine_log_module_t 布局/后端钩子签名。调用点宏级别检查内联,低于
 * 阈值零参数求值零函数调用;源位置 __FILE__/__LINE__/__func__ 恒捕获。
 * 类型=纯内建(两侧树各有 u64 typedef,契约头不依赖不冲突)。
 *
 * 用法:每个翻译单元一次 LOG_MODULE_REGISTER("sched");再
 * LOG_INF("tick=%u\n", t)。未注册 TU 不许用宏(无 __eshine_log_mod)。
 */
#ifndef ESHINE_LOG_H
#define ESHINE_LOG_H

/* ============================================================
 * 1. 级别模型(数值越大越啰嗦;默认 ES_LOG_INF)
 * ============================================================ */
typedef enum {
    ES_LOG_NONE = 0,
    ES_LOG_ERR  = 1,
    ES_LOG_WRN  = 2,
    ES_LOG_INF  = 3,
    ES_LOG_DBG  = 4,
    ES_LOG_TRC  = 5,
} eshine_log_level_t;

#define ES_LOG_LEVEL_DEFAULT  ES_LOG_INF

const char *eshine_log_level_str(eshine_log_level_t lvl);

/* ============================================================
 * 2. 模块描述符 + 注册(linker section 聚合)
 *
 * LOG_MODULE_REGISTER 在本 TU 创建 static 模块结构入
 * "eshine_log_mods" section;GNU ld 聚合后自动给 __start_/
 * __stop_ 符号(linker.ld 两侧均 KEEP 该段)。不依赖
 * constructor——EL2/EL1 bare-metal 不保证运行。
 * ============================================================ */
typedef struct eshine_log_module {
    const char   *name;      /* 模块名,如 "sched" */
    unsigned char level;     /* 本模块当前级别(运行时可改) */
    unsigned char _pad[7];   /* 对齐+预留 */
} eshine_log_module_t;

/* 未注册 TU 兜底模块(compat 面;新代码必须注册) */
extern eshine_log_module_t eshine_log_default_mod;

#define ES_LOG_SECTION __attribute__((used, section("eshine_log_mods")))

#define LOG_MODULE_REGISTER(_name) \
    static eshine_log_module_t __eshine_log_mod ES_LOG_SECTION = { \
        .name = (_name), .level = ES_LOG_LEVEL_DEFAULT }

#define ES_LOG_THIS_MODULE  (&__eshine_log_mod)

/* ============================================================
 * 3. 后端钩子契约(各侧实现的弱符号)
 * ============================================================ */

/* boot 后微秒数(时间戳源) */
unsigned long long log_clock_us(void);

/* 普通路径:整行输出(后端负责行级原子;弱默认=逐字符) */
void log_emit_char(char c);
void log_emit_str(const char *s, unsigned int len);

/* panic 路径:无锁、仅 panicking CPU 输出 */
void log_emit_panic_char(char c);

/* 格式化(核心自带最小 vsnprintf 实现为默认) */
int  log_vsnprintf(char *buf, unsigned int sz, const char *fmt, __builtin_va_list ap);

/* ring 存储(弱默认=raw 直写;后端 override 为加锁窗内调 raw——
 * 4 CPU 多写者下环完整性由后端锁负责,参考实现单写者假设不成立) */
void log_ring_store(const char *buf, unsigned int len, unsigned int level,
                    const char *mod, unsigned long long ts_us);
void eshine_log_ring_raw(const char *buf, unsigned int len);

/* ============================================================
 * 4. 核心发射 API
 * ============================================================ */
void eshine_log_emit(eshine_log_module_t *mod, eshine_log_level_t lvl,
                     const char *file, int line, const char *func,
                     const char *fmt, ...)
    __attribute__((format(printf, 6, 7)));
void eshine_log_vemit(eshine_log_module_t *mod, eshine_log_level_t lvl,
                      const char *file, int line, const char *func,
                      const char *fmt, __builtin_va_list ap);

/* 运行时控制 */
void               log_set_global_level(eshine_log_level_t lvl);
eshine_log_level_t log_get_global_level(void);
int                log_level_set(const char *module_name, eshine_log_level_t lvl);

typedef void (*log_module_cb_t)(const char *name, unsigned int level, void *u);
void log_list_modules(log_module_cb_t cb, void *u);

/* ============================================================
 * 5. 调用点宏
 * ============================================================ */
#define ES_LOG_EMIT(lvl, fmt, ...) \
    do { \
        if (__eshine_log_mod.level >= (lvl)) \
            eshine_log_emit(ES_LOG_THIS_MODULE, (lvl), \
                            __FILE__, __LINE__, __func__, \
                            (fmt), ##__VA_ARGS__); \
    } while (0)

#define LOG_ERR(fmt, ...)  ES_LOG_EMIT(ES_LOG_ERR, (fmt), ##__VA_ARGS__)
#define LOG_WRN(fmt, ...)  ES_LOG_EMIT(ES_LOG_WRN, (fmt), ##__VA_ARGS__)
#define LOG_INF(fmt, ...)  ES_LOG_EMIT(ES_LOG_INF, (fmt), ##__VA_ARGS__)
#define LOG_DBG(fmt, ...)  ES_LOG_EMIT(ES_LOG_DBG, (fmt), ##__VA_ARGS__)
#define LOG_TRC(fmt, ...)  ES_LOG_EMIT(ES_LOG_TRC, (fmt), ##__VA_ARGS__)

/* ============================================================
 * 6. 限流(热路径防淹没)
 * ============================================================ */
typedef struct eshine_log_ratelimit {
    unsigned long long window_start_us;
    unsigned int      emitted;
    unsigned int      suppressed;
} eshine_log_ratelimit_t;

/* 非 0=允许发;0=丢弃(并累计 suppressed;汇总行 v0 不自动发,消费者自查) */
int eshine_log_ratelimit(eshine_log_ratelimit_t *rl,
                         unsigned int interval_us, unsigned int burst);

/* ============================================================
 * 7. panic 通道(走 log_emit_panic_char 无锁后端;末尾 log_halt)
 * ============================================================ */
void eshine_log_panic(const char *file, int line, const char *func,
                      const char *fmt, ...)
    __attribute__((format(printf, 4, 5), noreturn));
void log_halt(void) __attribute__((noreturn));

#define ESHINE_PANIC(fmt, ...) \
    eshine_log_panic(__FILE__, __LINE__, __func__, (fmt), ##__VA_ARGS__)

#define ESHINE_BUG_ON(cond) \
    do { if (cond) ESHINE_PANIC("BUG_ON failed: " #cond); } while (0)


/* ============================================================
 * 7. ring 倾倒(panic/logdump)+增量收割(站50 klog 双源)
 * ============================================================ */
void log_dump_ring(void (*emit)(char));

/* 环锁对(站50:收割读与存环写共锁,环读一致;弱默认无锁=无收割
 * 消费者的侧不 override 即可) */
void log_ring_lock(void);
void log_ring_unlock(void);

/* 增量收割:自 *cursor 起拷未收割字节到 dst(≤dstsz),*cursor 推进
 * 到当前 head;返回拷贝字节数(0=无新)。调用方持 log_ring_lock。 */
unsigned int eshine_log_ring_delta(char *dst, unsigned int dstsz,
                                   unsigned int *cursor);

#endif /* ESHINE_LOG_H */
