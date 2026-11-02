/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_ELOG_H
#define ESHINE_ELOG_H

/* 站49:eshine_log 契约的 EL2 侧控制面。
 * 契约本体见 common/eshine_log.h(双树共享冻结 ABI);
 * 后端钩子+命令体在 kernel/elog_be.c。 */

void elog_logdemo(void);
void elog_loglevel_args(const char *args); /* "<lvl>" | "<mod> <lvl>" */
void elog_logmods(void);
void elog_logdump(void);
unsigned int elog_mod_count(void);        /* 注册模块计数(boot 自证) */

#endif /* ESHINE_ELOG_H */
