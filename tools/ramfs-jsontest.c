/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* 站五十九:cJSON 消费者 demo(生态件门禁件)。解析治理面真清单
 * /etc/services.json(站 51 自研最小 JSON 的生产数据),cJSON 全功能
 * 走查:parse/遍历/取串/取整。输出确定性(清单固定)。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

static const char *slurp_err;

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *b;

    if (!f) {
        slurp_err = "fopen";
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        slurp_err = "seek-end";
        fclose(f);
        return NULL;
    }
    n = ftell(f);
    if (n < 0) {
        slurp_err = "ftell";
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        slurp_err = "seek-set";
        fclose(f);
        return NULL;
    }
    b = malloc((size_t)n + 1);
    if (!b) {
        slurp_err = "malloc";
        fclose(f);
        return NULL;
    }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) {
        slurp_err = "fread";
        fclose(f);
        free(b);
        return NULL;
    }
    b[n] = '\0';
    fclose(f);
    return b;
}

int main(void)
{
    char *j = slurp("/etc/services.json");
    cJSON *root, *svcs, *c;
    int n;

    if (!j) {
        printf("jsontest: slurp failed at %s\n", slurp_err);
        return 1;
    }
    root = cJSON_Parse(j);
    if (!root) {
        printf("jsontest: parse failed\n");
        return 1;
    }
    svcs = cJSON_GetObjectItem(root, "services");
    n = cJSON_GetArraySize(svcs);
    printf("jsontest: services=%d\n", n);
    cJSON_ArrayForEach(c, svcs) {
        cJSON *nm = cJSON_GetObjectItem(c, "name");
        cJSON *md = cJSON_GetObjectItem(c, "mode");
        cJSON *wt = cJSON_GetObjectItem(c, "wait");
        cJSON *mr = cJSON_GetObjectItem(c, "max_restarts");

        if (!nm || !md)
            continue;
        printf("jsontest: %s mode=%s", nm->valuestring, md->valuestring);
        if (wt && wt->valuestring)
            printf(" wait=%s", wt->valuestring);
        if (mr)
            printf(" max=%d", mr->valueint);
        printf("\n");
    }
    cJSON_Delete(root);
    free(j);
    printf("jsontest: ok\n");
    return 0;
}
