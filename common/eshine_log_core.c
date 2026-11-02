/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/*
 * eshine_log_core.c — eshine 统一日志核心(双树共享,方案 A)
 *
 * formatter + registry(linker section)+ ring + ratelimit + panic。
 * 各侧经弱符号钩子接入(log_clock_us/log_emit_str/
 * log_emit_panic_char/log_ring_store/log_halt;log_vsnprintf 用本核
 * 自带最小实现——两侧本无 snprintf 面)。不依赖 libc:纯内建类型+
 * __builtin_va_list;bare-metal 可用;-mgeneral-regs-only 无浮点。
 * 参考实现 log/eshine_log_core.c 机制白描,clean-room 自写。
 */
#include "eshine_log.h"

/* ============================================================
 * 弱默认后端(各侧覆盖;emit_str 默认=逐字符)
 * ============================================================ */
__attribute__((weak)) unsigned long long log_clock_us(void) { return 0; }
__attribute__((weak)) void log_emit_char(char c) { (void)c; }
__attribute__((weak)) void log_emit_str(const char *s, unsigned int len)
{
    unsigned int i;

    if (!s)
        return;
    for (i = 0; i < len; i++)
        log_emit_char(s[i]);
}
__attribute__((weak)) void log_emit_panic_char(char c) { log_emit_char(c); }
__attribute__((weak)) void log_halt(void) { for (;;) __asm__ volatile("wfi"); }

/* 未注册 TU 兜底模块 */
eshine_log_module_t eshine_log_default_mod = {
    .name = "default", .level = ES_LOG_LEVEL_DEFAULT,
};

/* ============================================================
 * registry:遍历 linker section "eshine_log_mods"
 * (GNU ld 对合法 C 标识符 section 名自动给 __start_/__stop_;
 *  两侧 linker.ld 均 KEEP;weak 兼容未加段的环境)
 * ============================================================ */
__attribute__((weak)) extern eshine_log_module_t __start_eshine_log_mods[];
__attribute__((weak)) extern eshine_log_module_t __stop_eshine_log_mods[];

static int mod_eq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

int log_level_set(const char *module_name, eshine_log_level_t lvl)
{
    eshine_log_module_t *m = __start_eshine_log_mods;
    eshine_log_module_t *end = __stop_eshine_log_mods;

    if (!module_name) {
        eshine_log_default_mod.level = (unsigned char)lvl;
        return 0;
    }
    if (!m || !end || (unsigned long)end <= (unsigned long)m)
        return -1;
    for (; m < end; m++)
        if (m->name && mod_eq(m->name, module_name)) {
            m->level = (unsigned char)lvl;
            return 0;
        }
    return -1;
}

void log_list_modules(log_module_cb_t cb, void *u)
{
    eshine_log_module_t *m = __start_eshine_log_mods;
    eshine_log_module_t *end = __stop_eshine_log_mods;

    if (!cb || !m || !end || (unsigned long)end <= (unsigned long)m)
        return;
    for (; m < end; m++)
        if (m->name)
            cb(m->name, m->level, u);
}

/* ============================================================
 * 运行时控制 + 级别串
 * ============================================================ */
static unsigned char g_global_level = ES_LOG_LEVEL_DEFAULT;

void log_set_global_level(eshine_log_level_t lvl) { g_global_level = (unsigned char)lvl; }
eshine_log_level_t log_get_global_level(void) { return (eshine_log_level_t)g_global_level; }

const char *eshine_log_level_str(eshine_log_level_t lvl)
{
    switch (lvl) {
    case ES_LOG_ERR: return "err";
    case ES_LOG_WRN: return "wrn";
    case ES_LOG_INF: return "inf";
    case ES_LOG_DBG: return "dbg";
    case ES_LOG_TRC: return "trc";
    default:         return "off";
    }
}

/* ============================================================
 * 最小 vsnprintf(契约格式化面;两侧本无 snprintf 面)
 *
 * 支持:%d %i %u %x %X %p %s %c %%;长度修饰 l/ll/z(按 long long
 * 处理);不支持宽度/精度/标志(house 手搓最小面;未识 spec 原样
 * 输出 '%' 与字符)。返回写入字节数(不含 NUL),恒 NUL 结。
 * ============================================================ */
static int put_str(char *p, unsigned int *off, unsigned int sz, const char *s)
{
    while (s && *s && *off + 1 < sz)
        p[(*off)++] = *s++;
    return 0;
}

static void put_ch(char *p, unsigned int *off, unsigned int sz, char c)
{
    if (*off + 1 < sz)
        p[(*off)++] = c;
}

static void put_u64(char *p, unsigned int *off, unsigned int sz,
                    unsigned long long v, unsigned int base, int upper, int neg)
{
    char tmp[24];
    int n = 0;
    static const char lo[] = "0123456789abcdef";
    static const char up[] = "0123456789ABCDEF";
    const char *dig = upper ? up : lo;

    if (v == 0)
        tmp[n++] = '0';
    while (v) { tmp[n++] = dig[v % base]; v /= base; }
    if (neg && *off + 1 < sz)
        p[(*off)++] = '-';
    while (n > 0 && *off + 1 < sz)
        p[(*off)++] = tmp[--n];
}

__attribute__((weak)) int log_vsnprintf(char *buf, unsigned int sz,
                                        const char *fmt, __builtin_va_list ap)
{
    unsigned int off = 0;

    if (!buf || sz == 0)
        return 0;
    buf[0] = 0;
    while (*fmt && off + 1 < sz) {
        if (*fmt != '%') { buf[off++] = *fmt++; continue; }
        fmt++;
        if (*fmt == '%') { buf[off++] = *fmt++; continue; }
        /* 长度修饰:l/ll/z 皆按 long long 收纳(arm64 lp64 一致) */
        int lcount = 0;
        while (*fmt == 'l' || *fmt == 'z') {
            if (*fmt == 'l') lcount++;
            fmt++;
        }
        (void)lcount;
        switch (*fmt) {
        case 'd': case 'i': {
            long long v = __builtin_va_arg(ap, long long);
            if (v < 0) put_u64(buf, &off, sz, (unsigned long long)(-v), 10, 0, 1);
            else       put_u64(buf, &off, sz, (unsigned long long)v, 10, 0, 0);
            fmt++;
            break;
        }
        case 'u':
            put_u64(buf, &off, sz, __builtin_va_arg(ap, unsigned long long), 10, 0, 0);
            fmt++;
            break;
        case 'x':
            put_u64(buf, &off, sz, __builtin_va_arg(ap, unsigned long long), 16, 0, 0);
            fmt++;
            break;
        case 'X':
            put_u64(buf, &off, sz, __builtin_va_arg(ap, unsigned long long), 16, 1, 0);
            fmt++;
            break;
        case 'p':
            put_str(buf, &off, sz, "0x");
            put_u64(buf, &off, sz, (unsigned long long)__builtin_va_arg(ap, void *), 16, 0, 0);
            fmt++;
            break;
        case 's':
            put_str(buf, &off, sz, __builtin_va_arg(ap, const char *));
            fmt++;
            break;
        case 'c':
            put_ch(buf, &off, sz, (char)__builtin_va_arg(ap, int));
            fmt++;
            break;
        case 0:
            continue;
        default:                       /* 未识 spec:原样两字符 */
            put_ch(buf, &off, sz, '%');
            put_ch(buf, &off, sz, *fmt++);
            break;
        }
    }
    buf[off] = 0;
    return (int)off;
}

/* ============================================================
 * 行组装:[HH:MM:SS.mmm,uuu] <lvl> mod     : msg
 * (ring 与 UART 同一 plain 行;颜色面 v0 不落,记档非目标)
 * ============================================================ */
#define ES_LINE_MAX 256
#define ES_MOD_W    8

static int fmt_ts(char *buf, unsigned int sz, unsigned long long us)
{
    static const char dig[] = "0123456789";
    unsigned int h  = (unsigned int)((us / 3600000000ULL) % 24);
    unsigned int m  = (unsigned int)((us /   60000000ULL) % 60);
    unsigned int s  = (unsigned int)((us /    1000000ULL) % 60);
    unsigned int ms = (unsigned int)((us /       1000ULL) % 1000);
    unsigned int uu = (unsigned int)(us % 1000ULL);
    unsigned int o = 0;

    buf[o++] = '[';
    buf[o++] = dig[h / 10];  buf[o++] = dig[h % 10];  buf[o++] = ':';
    buf[o++] = dig[m / 10];  buf[o++] = dig[m % 10];  buf[o++] = ':';
    buf[o++] = dig[s / 10];  buf[o++] = dig[s % 10];  buf[o++] = '.';
    buf[o++] = dig[(ms / 100) % 10]; buf[o++] = dig[(ms / 10) % 10]; buf[o++] = dig[ms % 10];
    buf[o++] = ',';
    buf[o++] = dig[(uu / 100) % 10]; buf[o++] = dig[(uu / 10) % 10]; buf[o++] = dig[uu % 10];
    buf[o++] = ']'; buf[o++] = ' ';
    buf[o] = 0;
    (void)sz;
    return (int)o;
}

static int fmt_mod(char *buf, unsigned int sz, const char *name)
{
    unsigned int o = 0;

    if (name)
        while (name[o] && o + 1 < sz)
            { buf[o] = name[o]; o++; }
    while (o < ES_MOD_W && o + 1 < sz)
        buf[o++] = ' ';
    buf[o] = 0;
    return (int)o;
}

/* ============================================================
 * in-core ring(32KiB 字节环覆盖最旧;raw=无锁,后端 override
 * log_ring_store 在锁窗内调它)
 * ============================================================ */
#define ES_RING_SIZE (32 * 1024)
static char         g_ring[ES_RING_SIZE];
static unsigned int g_ring_head;
static unsigned int g_ring_len;
static unsigned int g_ring_total;   /* 单调写入总量(站50 收割游标基准) */

void eshine_log_ring_raw(const char *buf, unsigned int len)
{
    unsigned int i;

    if (!buf || len == 0)
        return;
    for (i = 0; i < len; i++) {
        g_ring[g_ring_head] = buf[i];
        g_ring_head = (g_ring_head + 1) % ES_RING_SIZE;
        if (g_ring_len < ES_RING_SIZE)
            g_ring_len++;
    }
    g_ring_total += len;
}

__attribute__((weak)) void log_ring_store(const char *buf, unsigned int len,
                                          unsigned int level, const char *mod,
                                          unsigned long long ts_us)
{
    (void)level; (void)mod; (void)ts_us;
    eshine_log_ring_raw(buf, len);
}

void log_dump_ring(void (*emit)(char))
{
    unsigned int start = (g_ring_len < ES_RING_SIZE) ? 0 : g_ring_head;
    unsigned int i;

    if (!emit)
        return;
    for (i = 0; i < g_ring_len; i++)
        emit(g_ring[(start + i) % ES_RING_SIZE]);
}

/* ---- 站50:增量收割(锁窗内读,与存环同锁保一致) ----
 * 绝对索引模型:total=单调写入量;stored=min(total,SIZE);最旧存字节
 * 绝对位=oldest=total-stored(回绕后恒=total-SIZE,物理位=head);
 * 游标<oldest=历史已被覆写,重锚最旧续收(收割滞后世代的诚实脸)。 */
__attribute__((weak)) void log_ring_lock(void) { }
__attribute__((weak)) void log_ring_unlock(void) { }

unsigned int eshine_log_ring_delta(char *dst, unsigned int dstsz,
                                   unsigned int *cursor)
{
    unsigned int total = g_ring_total;
    unsigned int oldest = total - g_ring_len;
    unsigned int avail, n, i;

    if (!dst || !dstsz || !cursor)
        return 0;
    if (*cursor < oldest)
        *cursor = oldest;                 /* 历史已失:重锚 */
    if (*cursor > total)
        *cursor = total;                   /* 未来游标:夹回 */
    avail = total - *cursor;
    n = avail < dstsz ? avail : dstsz;
    /* head = total mod SIZE 恒成立(逐字节同进),环=线性流取模:
     * phys(K) = K % SIZE,无需存区偏移换算(首版 off 换算=错位雷) */
    for (i = 0; i < n; i++)
        dst[i] = g_ring[(*cursor + i) % ES_RING_SIZE];
    *cursor += n;
    return n;
}

/* ============================================================
 * 发射
 * ============================================================ */
void eshine_log_vemit(eshine_log_module_t *mod, eshine_log_level_t lvl,
                      const char *file, int line, const char *func,
                      const char *fmt, __builtin_va_list ap)
{
    char msg[ES_LINE_MAX];
    char ln[ES_LINE_MAX];
    unsigned int off = 0;
    unsigned long long ts;
    unsigned int rem = sizeof(ln);

    if (!mod)
        mod = &eshine_log_default_mod;
    /* 二次过滤(宏已查一次;兜底 compat/直调路径) */
    if (mod->level < (unsigned char)lvl && g_global_level < (unsigned char)lvl)
        return;
    /* file/line/func v0 不入行(ABI 保位,首消费者召唤再落) */
    (void)file; (void)line; (void)func;

    ts = log_clock_us();
    log_vsnprintf(msg, sizeof(msg), fmt, ap);
    msg[sizeof(msg) - 1] = 0;

    {
        int n = fmt_ts(ln, rem, ts);
        off += (unsigned int)n; rem -= (unsigned int)n;
    }
    if (rem > 6) {
        ln[off++] = '<';
        ln[off++] = eshine_log_level_str(lvl)[0];
        ln[off++] = eshine_log_level_str(lvl)[1];
        ln[off++] = eshine_log_level_str(lvl)[2];
        ln[off++] = '>';
        ln[off++] = ' ';
        rem -= 6;
    }
    {
        int n = fmt_mod(ln + off, rem, mod->name ? mod->name : "?");
        off += (unsigned int)n; rem -= (unsigned int)n;
    }
    if (rem > 2) { ln[off++] = ':'; ln[off++] = ' '; rem -= 2; }
    {
        unsigned int i = 0;
        while (msg[i] && rem > 1) { ln[off++] = msg[i++]; rem--; }
    }
    ln[off] = 0;

    log_ring_store(ln, off, (unsigned int)lvl, mod->name, ts);
    log_emit_str(ln, off);
}

void eshine_log_emit(eshine_log_module_t *mod, eshine_log_level_t lvl,
                     const char *file, int line, const char *func,
                     const char *fmt, ...)
{
    __builtin_va_list ap;

    __builtin_va_start(ap, fmt);
    eshine_log_vemit(mod, lvl, file, line, func, fmt, ap);
    __builtin_va_end(ap);
}

/* ============================================================
 * 限流
 * ============================================================ */
int eshine_log_ratelimit(eshine_log_ratelimit_t *rl,
                         unsigned int interval_us, unsigned int burst)
{
    unsigned long long now;

    if (!rl)
        return 1;
    now = log_clock_us();
    if (rl->window_start_us == 0)
        rl->window_start_us = now;
    if (now - rl->window_start_us >= interval_us) {
        rl->window_start_us = now;
        rl->emitted = 0;
    }
    if (rl->emitted < burst) {
        rl->emitted++;
        return 1;
    }
    rl->suppressed++;
    return 0;
}

/* ============================================================
 * panic 通道(无锁后端;banner+ring dump+halt)
 * ============================================================ */
static void panic_str(const char *s)
{
    while (s && *s)
        log_emit_panic_char(*s++);
}

static void panic_u64(unsigned long long v)
{
    char tmp[24];
    int n = 0;

    if (v == 0)
        tmp[n++] = '0';
    while (v) { tmp[n++] = "0123456789"[v % 10]; v /= 10; }
    while (n > 0)
        log_emit_panic_char(tmp[--n]);
}

void eshine_log_panic(const char *file, int line, const char *func,
                      const char *fmt, ...)
{
    char msg[ES_LINE_MAX];
    __builtin_va_list ap;

    __builtin_va_start(ap, fmt);
    log_vsnprintf(msg, sizeof(msg), fmt, ap);
    __builtin_va_end(ap);
    msg[sizeof(msg) - 1] = 0;

    panic_str("\n=== ESHINE PANIC ===\n");
    panic_str("panic: ");
    panic_str(msg);
    panic_str(" at ");
    panic_str(file ? file : "?");
    panic_str(":");
    panic_u64((unsigned long long)line);
    panic_str(" (");
    panic_str(func ? func : "?");
    panic_str(")\n--- log ring ---\n");
    log_dump_ring(log_emit_panic_char);
    panic_str("--- ring end ---\n");

    log_halt();
}
