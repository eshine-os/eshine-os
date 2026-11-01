/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* 站40 respawn 活证:打印即退 exit=42——壳探询(WNOHANG)收尸
 * 再拉,≥2 拍=respawn 链活。 */
#include <unistd.h>

int main(void)
{
    static const char m[] = "respawn beat\n";

    write(1, m, sizeof(m) - 1);
    return 42;
}
