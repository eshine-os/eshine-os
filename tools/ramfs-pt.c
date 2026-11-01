/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* 站35 pthread demo:4 线程互斥累加+joint 自证。mutex 争用路径即
 * futex WAIT/WAKE 的真线程对;join=detach_state futex+CLEARTID。 */
#include <pthread.h>
#include <stdio.h>

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static volatile int counter;

static void *work(void *arg)
{
    pthread_mutex_lock(&mu);
    counter += (int)(long)arg;
    pthread_mutex_unlock(&mu);
    return 0;
}

int main(void)
{
    pthread_t th[4];
    int i, bad = 0;

    for (i = 0; i < 4; i++) {
        int r = pthread_create(&th[i], 0, work, (void *)(long)(i + 1));
        if (r) {
            printf("pthread: create failed at %d errno=%d\n", i, r);
            bad = 1;
        }
    }
    if (!bad) {
        for (i = 0; i < 4; i++)
            pthread_join(th[i], 0);
        printf("pthread: 4 threads joined, counter=%d (want 10)\n",
               counter);
        printf("pthread: self=%p\n", (void *)pthread_self());
    }
    return (!bad && counter == 10) ? 0 : 1;
}
