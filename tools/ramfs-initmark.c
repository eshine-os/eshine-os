/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* 站40 init+inittab 形态的 sysinit 活证:建 /tmp/init40 写标记
 * (fdcreate 554/fdwrite 553/真号 close 57)。musl syscall() 直连
 * 我们扩展号——用户态消费者=站 31 fd 机器。 */
#include <unistd.h>
#include <sys/syscall.h>

int main(void)
{
    static const char mark[] = "ESHINE40-INIT\n";
    static const char ok[] = "initmark: wrote /tmp/init40\n";
    static const char bad[] = "initmark: create failed\n";
    long fd = syscall(554, "/tmp/init40");   /* ES_NR_fdcreate */

    if (fd < 0) {
        write(1, bad, sizeof(bad) - 1);
        return 1;
    }
    syscall(553, fd, mark, sizeof(mark) - 1); /* ES_NR_fdwrite */
    syscall(57, fd);                          /* close(真号) */
    write(1, ok, sizeof(ok) - 1);
    return 0;
}
