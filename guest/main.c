/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "hvc.h"
#include "miniuart.h"

#define GBUF_MAX 128

static char linebuf[GBUF_MAX];

static void puts_uart(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int starts(const char *s, const char *pfx)
{
    while (*pfx) {
        if (*s++ != *pfx++)
            return 0;
    }
    return 1;
}

/* guest 侧三件套:HVC banner(超调用)、直访 UART banner(stage-2 设备页)、
 * 行编辑交互(echo/exit)。exit 经 HVC #1 退回 EL2。 */
void guest_main(void)
{
    const char *msg = "eshine-guest @EL1 via stage-2 (HVC console)\n";
    const char *p;
    unsigned int idx = 0;

    for (p = msg; *p; p++)
        hvc_putc(*p);

    puts_uart("eshine-guest UART @IPA 0x09000000 (stage-2 device page)\n");
    puts_uart("guest> ");

    for (;;) {
        char c = uart_getc();

        if (c == '\r' || c == '\n') {
            uart_putc('\n');
            linebuf[idx] = '\0';
            if (streq(linebuf, "exit")) {
                hvc_exit();                 /* noreturn,宿主接手打印 VM_DEAD */
            } else if (starts(linebuf, "echo ")) {
                puts_uart(linebuf + 5);
                puts_uart("\n");
            } else if (linebuf[0]) {
                puts_uart("guest: try echo <text> | exit\n");
            }
            idx = 0;
            puts_uart("guest> ");
        } else if ((c == 0x7f || c == '\b') && idx > 0) {
            idx--;
            puts_uart("\b \b");
        } else if (idx < GBUF_MAX - 1 && c >= 0x20 && c < 0x7f) {
            linebuf[idx++] = c;
            uart_putc(c);
        }
    }
}
