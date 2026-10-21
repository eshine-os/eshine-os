/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "kernel/include/console.h"
#include "kernel/include/shell.h"
#include "kernel/include/types.h"
#include "arch/arm64/include/mm.h"
#include "kernel/include/util.h"
#include "kernel/include/vcpu.h"
#include "kernel/include/version.h"
#include "kernel/include/vm.h"
#include "drivers/include/pl011.h"

#define SHELL_BUF_MAX 64

static char linebuf[SHELL_BUF_MAX];

static void cmd_help(void)
{
    console_puts("commands: help version echo <text> state s2dump run_guest\n");
}

static void cmd_version(void)
{
    console_puts(ESHINE_BANNER "\n");
    console_puts("  hypervisor:  " ESHINE_VERSION_STR "\n");
    console_puts("  rtos:        not-yet (VM0 management domain, planned)\n");
    console_puts("  guest-linux: not-yet (upstream LTS pinned per release)\n");
}

static void cmd_echo(const char *args)
{
    console_puts(args);
    console_putc('\n');
}

static void cmd_state(void)
{
    vm_state_demo();
}

static void cmd_run_guest(void)
{
    /* 实体内嵌 vm_space 后 eshine_vm ~20KB > 宿主栈 16KB,必须静态 */
    static struct eshine_vm vm;
    struct vcpu v;

    if (vm.state == VM_DEAD)
        vm_transition(&vm, VM_INIT);       /* 槽位复用,防二次 run panic */
    v.entry_pc = 0x40000000UL;             /* guest IPA(物理在 0x50000000) */
    v.entry_sp  = 0x40200000UL;            /* 安全初值,guest 自会重设 */
    v.spsr      = 0x3C5UL;                 /* EL1h(0x5) + DAIF 全掩(0xF<<6) */

    vm_transition(&vm, VM_CREATED);
    console_puts("run_guest: entering EL1 via stage-2 (non-identity map)\n");
    vm_transition(&vm, VM_RUNNING);
    vcpu_run(&v, (u64)stage2_pgd());       /* 过渡:旧静态表|VMID0;Task 4 切 vm 层 */
    vm_transition(&vm, VM_TEARDOWN);
    vm_transition(&vm, VM_DEAD);
    console_puts("run_guest: VM_DEAD, back at EL2\n");
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
    } else if (strncmp(line, "s2dump", 64) == 0) {
        stage2_dump();
    } else if (strncmp(line, "run_guest", 64) == 0) {
        cmd_run_guest();
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
