/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* 站五十七:AF_UNIX sock demo(musl 真号直连=socket/bind/listen/
 * connect/accept/sendto/recvfrom/shutdown 八门零 patch 判据)。
 * 单进程回环序(connect 先于 accept=队列非空;send 先于 recv=
 * 环内有字)——零重试环零时序=冒烟确定性优先。 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

int main(void)
{
    struct sockaddr_un a;
    int s, c, cs, n;
    char buf[32];

    s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) {
        printf("sockdemo: socket failed\n");
        return 1;
    }
    memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    strcpy(a.sun_path, "/tmp/sockdemo");
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) != 0) {
        printf("sockdemo: bind failed\n");
        return 1;
    }
    if (listen(s, 4) != 0) {
        printf("sockdemo: listen failed\n");
        return 1;
    }
    c = socket(AF_UNIX, SOCK_STREAM, 0);
    if (connect(c, (struct sockaddr *)&a, sizeof(a)) != 0) {
        printf("sockdemo: connect failed\n");
        return 1;
    }
    cs = accept(s, 0, 0);
    if (cs < 0) {
        printf("sockdemo: accept failed\n");
        return 1;
    }
    if (send(c, "ESHINE57", 8, 0) != 8) {
        printf("sockdemo: send failed\n");
        return 1;
    }
    memset(buf, 0, sizeof(buf));
    n = recv(cs, buf, sizeof(buf) - 1, 0);
    if (n != 8 || memcmp(buf, "ESHINE57", 8) != 0) {
        printf("sockdemo: recv mismatch\n");
        return 1;
    }
    shutdown(c, 2);
    printf("sockdemo: echo ok <%s>\n", buf);
    return 0;
}
