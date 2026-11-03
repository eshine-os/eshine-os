/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* 站五十九:zlib 消费者 demo(slog 压缩落盘):deflate /var/log/slog
 * → /tmp/slog.z(落盘)→ 读回 inflate → 逐字节校验。缺 slog(未
 * flush)则回退解析 /etc/services.json——demo 双数据源,判据面恒
 * "inflate match"。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zlib.h"

static char *slurp(const char *path, long *out)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *b;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) {
        fclose(f);
        return NULL;
    }
    b = malloc((size_t)n + 1);
    if (!b) {
        fclose(f);
        return NULL;
    }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(b);
        return NULL;
    }
    b[n] = '\0';
    fclose(f);
    *out = n;
    return b;
}

int main(void)
{
    const char *src_path = "/var/log/slog";
    long n = 0;
    char *src = slurp(src_path, &n);

    if (!src) {
        src_path = "/etc/services.json";
        src = slurp(src_path, &n);
        if (!src) {
            printf("ztest: no input\n");
            return 1;
        }
        printf("ztest: fallback %s\n", src_path);
    }
    {
        uLongf clen = compressBound((uLong)n);
        unsigned char *cbuf = malloc(clen);
        unsigned char *dbuf = malloc((size_t)n + 1);
        FILE *z;
        long zn;
        unsigned char *zb;

        if (!cbuf || !dbuf) {
            printf("ztest: oom\n");
            return 1;
        }
        if (compress2(cbuf, &clen, (const Bytef *)src, (uLong)n, 6) != Z_OK) {
            printf("ztest: deflate failed\n");
            return 1;
        }
        z = fopen("/tmp/slog.z", "wb");
        if (!z) {
            printf("ztest: create failed\n");
            return 1;
        }
        if (fwrite(cbuf, 1, (size_t)clen, z) != (size_t)clen) {
            printf("ztest: write failed\n");
            return 1;
        }
        fclose(z);
        zb = (unsigned char *)slurp("/tmp/slog.z", &zn);
        if (!zb || zn != (long)clen) {
            printf("ztest: readback failed\n");
            return 1;
        }
        {
            uLongf dl = (uLongf)n;

            if (uncompress(dbuf, &dl, zb, (uLong)zn) != Z_OK ||
                dl != (uLong)n ||
                memcmp(dbuf, src, (size_t)n) != 0) {
                printf("ztest: inflate MISMATCH\n");
                return 1;
            }
        }
        printf("ztest: %s %ld -> %ld bytes\n", src_path, n, (long)clen);
        printf("ztest: inflate match\n");
        return 0;
    }
}
