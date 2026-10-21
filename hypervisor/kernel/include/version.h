/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_VERSION_H
#define ESHINE_VERSION_H

/*
 * ESHINE_VERSION_STR is injected by the build from the repo-root
 * VERSION file (single source of truth, see docs/VERSIONING.md).
 * This fallback only builds when the macro is missing.
 */
#ifndef ESHINE_VERSION_STR
#define ESHINE_VERSION_STR "unknown"
#endif

#define ESHINE_BANNER "eshine-os " ESHINE_VERSION_STR " (EL2) @ QEMU virt"

#endif /* ESHINE_VERSION_H */
