/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* 站六十:cyclictest 形延迟基线 demo(EL0 消费者)。周期唤起任务
 * 实测唤醒-运行延迟:每拍 clock_gettime→nanosleep(10ms=1 tick)→
 * clock_gettime,超额延迟(实测-名义)入桶。直方图+max/avg 落控制台,
 * 基线数值入 docs/benchmark/(趋势门:数值非硬门,记档对照)。 */
#include <stdio.h>
#include <time.h>

#define N_LOOPS 200
#define PERIOD_NS 10000000L          /* 10ms=1 tick(100Hz 量子) */
#define NB 11                        /* 桶:0-1..9-10/>10ms(超额面;
                                     * 拍超睡分布落 9-10ms 桶) */

int main(void)
{
    struct timespec ts, t0, t1;
    long buckets[NB] = {0};
    long max_us = 0, total = 0;
    int n = 0;
    int i;

    for (i = 0; i < N_LOOPS; i++) {
        long lat_us, over_us;
        int b;

        clock_gettime(CLOCK_MONOTONIC, &t0);
        ts.tv_sec = 0;
        ts.tv_nsec = PERIOD_NS;
        nanosleep(&ts, 0);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        lat_us = (t1.tv_sec - t0.tv_sec) * 1000000L +
                 (t1.tv_nsec - t0.tv_nsec) / 1000L;
        over_us = lat_us - PERIOD_NS / 1000L;
        if (over_us < 0)
            over_us = 0;
        if (over_us > max_us)
            max_us = over_us;
        total += over_us;
        b = (int)(over_us / 1000L);
        if (b > NB - 1)
            b = NB - 1;
        buckets[b]++;
        n++;
    }
    printf("cyclictest: %d loops period=10ms\n", n);
    for (i = 0; i < NB; i++) {
        if (i < NB - 1)
            printf("cyclictest: %d-%dms: %ld\n", i, i + 1, buckets[i]);
        else
            printf("cyclictest: >%dms: %ld\n", NB - 1, buckets[i]);
    }
    printf("cyclictest: max=%ldus avg=%ldus\n", max_us, total / n);
    return 0;
}
