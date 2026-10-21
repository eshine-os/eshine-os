/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "hvc.h"

void guest_main(void)
{
    const char *msg = "eshine-guest @EL1 via stage-2 (HVC console)\n";
    const char *p;

    for (p = msg; *p; p++)
        hvc_putc(*p);
    hvc_exit();                            /* B 相:立即退出,先证往返 */
    for (;;)
        ;
}
