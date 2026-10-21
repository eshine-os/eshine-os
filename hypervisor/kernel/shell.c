/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "kernel/include/console.h"
#include "kernel/include/shell.h"
#include "kernel/include/types.h"
#include "kernel/include/util.h"
#include "kernel/include/version.h"
#include "drivers/include/pl011.h"

#define SHELL_BUF_MAX 64

static char linebuf[SHELL_BUF_MAX];

static void cmd_help(void)
{
    console_puts("commands: help version echo <text> state\n");
}

static void cmd_version(void)
{
    console_puts(ESHINE_BANNER "\n");
}

static void cmd_echo(const char *args)
{
    console_puts(args);
    console_putc('\n');
}

static void cmd_state(void)
{
    console_puts("state machine demo lands in the next commit\n");
}

static void dispatch(char *line)
{
    /* 就地拆出第一个词与参数 */
    char *args = line;

    while (*args && *args != ' ')
        args++;
    if (*args == ' ') {
        *args++ = '\0';
        while (*args == ' ')
            args++;
    } else {
        *args = '\0';
    }

    if (strncmp(line, "help", 64) == 0) {
        cmd_help();
    } else if (strncmp(line, "version", 64) == 0) {
        cmd_version();
    } else if (strncmp(line, "echo", 64) == 0) {
        cmd_echo(args);
    } else if (strncmp(line, "state", 64) == 0) {
        cmd_state();
    } else if (*line) {
        console_puts("unknown command: ");
        console_puts(line);
        console_puts(" (try help)\n");
    }
}

void shell_run(void)
{
    u32 idx = 0;

    console_puts("eshine> ");
    for (;;) {
        char c = pl011_getc();

        if (c == '\r' || c == '\n') {
            console_putc('\n');
            linebuf[idx] = '\0';
            dispatch(linebuf);
            idx = 0;
            console_puts("eshine> ");
        } else if ((c == 0x7f || c == '\b') && idx > 0) {
            idx--;
            console_puts("\b \b");
        } else if (idx < SHELL_BUF_MAX - 1) {
            linebuf[idx++] = c;
            console_putc(c);
        }
    }
}
