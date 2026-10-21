/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_UTIL_H
#define ESHINE_UTIL_H

#include "kernel/include/types.h"

void *memset(void *dst, int c, u64 n);
void *memcpy(void *dst, const void *src, u64 n);
int strncmp(const char *a, const char *b, u64 n);

#endif /* ESHINE_UTIL_H */
