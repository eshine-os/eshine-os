/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    printf("musl: hello from /bin/hello pid=%d\n", getpid());
    printf("musl: fmt proof %d*%d=%d\n", 6, 7, 6 * 7);
    return 0;
}
