/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "kernel/include/util.h"

void *memset(void *dst, int c, u64 n)
{
    u8 *p = dst;

    while (n--)
        *p++ = (u8)c;
    return dst;
}

void *memcpy(void *dst, const void *src, u64 n)
{
    u8 *d = dst;
    const u8 *s = src;

    while (n--)
        *d++ = *s++;
    return dst;
}

int strncmp(const char *a, const char *b, u64 n)
{
    while (n && *a && (*a == *b)) {
        a++; b++; n--;
    }
    return n ? (u8)*a - (u8)*b : 0;
}
