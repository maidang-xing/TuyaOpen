#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "snt_contract.h"
#include "snt_text.h"
#include "snt_g2p.h"

static void print_json_string(FILE *f, const char *s) {
    fputc('"', f);
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c == '"') fputs("\\\"", f);
        else if (c == '\\') fputs("\\\\", f);
        else if (c == '\n') fputs("\\n", f);
        else if (c == '\r') fputs("\\r", f);
        else if (c == '\t') fputs("\\t", f);
        else if (c < 0x20) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

static void print_usage(const char *prog) {
    printf("Usage:\n");
    printf("  %s convert <text> [data_dir]\n", prog);
    printf("  %s foreach <text> [data_dir]\n", prog);
    printf("  %s golden <fixtures_dir> [data_dir]\n", prog);
    printf("  %s normalize <text> [mode:0|1|2]\n", prog);
    printf("  %s cancel-test <text> [data_dir]\n", prog);
}

static int do_convert(const char *text, const char *data_dir) {
    if (data_dir) snt_g2p_set_data_dir(data_dir);
    int rc = snt_g2p_init(SNT_VOICE_ZH);
    if (rc != SNT_OK) {
        fprintf(stderr, "error: snt_g2p_init failed with code %d\n", rc);
        return 1;
    }

    int32_t ids[1024];
    size_t used = 0;
    rc = snt_g2p_convert(text, strlen(text), ids, 1024, &used);
    if (rc != SNT_OK) {
        fprintf(stderr, "error: snt_g2p_convert failed with code %d\n", rc);
        return 1;
    }

    printf("{\n");
    printf("  \"text\": ");
    print_json_string(stdout, text);
    printf(",\n");
    printf("  \"n_ids\": %zu,\n", used);
    printf("  \"unknown_symbols\": %d,\n", snt_g2p_get_unknown_symbol_count());
    printf("  \"ids\": [");
    for (size_t i = 0; i < used; i++) {
        printf("%d%s", ids[i], (i == used - 1) ? "" : ", ");
    }
    printf("]\n");
    printf("}\n");

    snt_g2p_deinit();
    return 0;
}

typedef struct {
    size_t count;
    bool stop_at_first;
} foreach_user_ctx_t;

static int foreach_cb(const int32_t *ids, size_t n_ids, size_t segment_index, void *user) {
    foreach_user_ctx_t *ctx = (foreach_user_ctx_t *)user;
    if (ctx->count > 0) printf(",\n");
    printf("    {\n");
    printf("      \"segment_index\": %zu,\n", segment_index);
    printf("      \"n_ids\": %zu,\n", n_ids);
    printf("      \"ids\": [");
    for (size_t i = 0; i < n_ids; i++) {
        printf("%d%s", ids[i], (i == n_ids - 1) ? "" : ", ");
    }
    printf("]\n");
    printf("    }");
    ctx->count++;
    if (ctx->stop_at_first) return 1; /* Early stop */
    return 0;
}

static int do_foreach(const char *text, const char *data_dir, bool stop_at_first) {
    if (data_dir) snt_g2p_set_data_dir(data_dir);
    int rc = snt_g2p_init(SNT_VOICE_ZH);
    if (rc != SNT_OK) {
        fprintf(stderr, "error: snt_g2p_init failed with code %d\n", rc);
        return 1;
    }

    printf("{\n");
    printf("  \"text\": ");
    print_json_string(stdout, text);
    printf(",\n");
    printf("  \"segments\": [\n");

    foreach_user_ctx_t ctx = { .count = 0, .stop_at_first = stop_at_first };
    rc = snt_g2p_foreach_segment(text, strlen(text), foreach_cb, &ctx);

    printf("\n  ],\n");
    printf("  \"segment_count\": %zu,\n", ctx.count);
    printf("  \"result_code\": %d\n", rc);
    printf("}\n");

    snt_g2p_deinit();
    return (rc == SNT_OK || rc == SNT_ECANCELLED) ? 0 : 1;
}

/* 10 Golden Sentences from Task A zh_golden.json */
static const struct {
    const char *text;
    size_t n_ids;
    int32_t ids[256];
} ZH_10_GOLDEN[] = {
    {"你好", 19, {1,0,26,0,21,0,132,0,127,0,120,0,51,0,33,0,132,0,2}},
    {"欢迎使用本地语音", 91, {1,0,127,0,33,0,14,0,135,0,26,0,22,0,120,0,21,0,62,0,44,0,3,0,31,0,10,0,120,0,21,0,10,0,132,0,3,0,22,0,120,0,27,0,26,0,66,0,135,0,3,0,28,0,59,0,132,0,26,0,32,0,120,0,21,0,135,0,3,0,22,0,120,0,37,0,132,0,3,0,22,0,120,0,21,0,135,0,26,0,2}},
    {"今天天气怎么样", 101, {1,0,32,0,55,0,120,0,21,0,135,0,26,0,3,0,32,0,20,0,120,0,21,0,61,0,135,0,26,0,3,0,32,0,20,0,120,0,21,0,61,0,135,0,26,0,3,0,32,0,55,0,20,0,120,0,21,0,135,0,3,0,32,0,31,0,120,0,59,0,132,0,26,0,3,0,25,0,120,0,27,0,9,0,131,0,3,0,22,0,120,0,21,0,51,0,135,0,44,0,2}},
    {"请打开客厅的灯", 91, {1,0,32,0,55,0,20,0,120,0,21,0,132,0,44,0,3,0,32,0,120,0,51,0,132,0,3,0,23,0,20,0,120,0,14,0,21,0,135,0,3,0,23,0,20,0,120,0,27,0,9,0,135,0,3,0,32,0,20,0,120,0,21,0,135,0,44,0,3,0,32,0,120,0,59,0,131,0,3,0,32,0,120,0,59,0,135,0,44,0,2}},
    {"现在温度是二十三度", 113, {1,0,55,0,120,0,21,0,61,0,135,0,26,0,3,0,32,0,31,0,120,0,14,0,21,0,135,0,3,0,35,0,120,0,33,0,59,0,135,0,26,0,3,0,32,0,120,0,33,0,135,0,3,0,31,0,10,0,120,0,21,0,10,0,135,0,3,0,120,0,59,0,30,0,135,0,3,0,31,0,10,0,120,0,21,0,10,0,62,0,3,0,31,0,120,0,14,0,135,0,26,0,3,0,32,0,120,0,33,0,135,0,2}},
    {"重启后重新连接", 101, {1,0,32,0,31,0,10,0,120,0,27,0,26,0,66,0,135,0,3,0,32,0,55,0,20,0,120,0,21,0,132,0,3,0,127,0,120,0,27,0,33,0,135,0,3,0,32,0,31,0,10,0,20,0,27,0,26,0,66,0,62,0,55,0,120,0,21,0,135,0,26,0,3,0,24,0,120,0,21,0,61,0,62,0,26,0,3,0,32,0,55,0,120,0,21,0,61,0,135,0,2}},
    {"银行今天营业", 75, {1,0,22,0,21,0,62,0,26,0,127,0,120,0,51,0,62,0,44,0,3,0,32,0,55,0,120,0,21,0,135,0,26,0,3,0,32,0,20,0,120,0,21,0,61,0,135,0,26,0,3,0,22,0,120,0,21,0,62,0,44,0,3,0,22,0,120,0,21,0,61,0,135,0,2}},
    {"编号12345", 115, {1,0,28,0,120,0,21,0,61,0,135,0,26,0,3,0,127,0,120,0,51,0,33,0,135,0,3,0,31,0,10,0,121,0,21,0,10,0,62,0,59,0,30,0,135,0,3,0,32,0,55,0,20,0,120,0,21,0,61,0,135,0,26,0,3,0,31,0,121,0,14,0,135,0,26,0,28,0,14,0,21,0,132,0,3,0,31,0,120,0,21,0,142,0,135,0,31,0,10,0,21,0,10,0,62,0,35,0,121,0,33,0,132,0,2}},
    {"时间08:30", 63, {1,0,31,0,10,0,120,0,21,0,10,0,62,0,3,0,32,0,55,0,120,0,21,0,61,0,135,0,26,0,3,0,28,0,120,0,51,0,135,0,3,0,31,0,120,0,14,0,135,0,26,0,31,0,10,0,21,0,10,0,62,0,2}},
    {"日期2026年10月27日", 167, {1,0,106,0,120,0,21,0,10,0,135,0,3,0,32,0,55,0,20,0,120,0,21,0,135,0,3,0,59,0,30,0,135,0,3,0,32,0,55,0,20,0,120,0,21,0,61,0,135,0,26,0,3,0,59,0,30,0,135,0,31,0,10,0,121,0,21,0,10,0,62,0,24,0,21,0,27,0,33,0,135,0,3,0,26,0,120,0,21,0,61,0,62,0,26,0,3,0,31,0,10,0,120,0,21,0,10,0,62,0,3,0,22,0,120,0,37,0,61,0,135,0,3,0,121,0,59,0,30,0,135,0,31,0,10,0,21,0,10,0,62,0,32,0,55,0,20,0,120,0,21,0,135,0,3,0,106,0,120,0,21,0,10,0,135,0,2}}
};

static int do_golden(const char *data_dir) {
    if (data_dir) snt_g2p_set_data_dir(data_dir);
    int rc = snt_g2p_init(SNT_VOICE_ZH);
    if (rc != SNT_OK) {
        fprintf(stderr, "error: snt_g2p_init failed with code %d\n", rc);
        return 1;
    }

    printf("=== sanoTTS Chinese G2P Golden Gate Verification ===\n");
    int failed = 0;

    for (int s = 0; s < 10; s++) {
        const char *text = ZH_10_GOLDEN[s].text;
        size_t expected_n = ZH_10_GOLDEN[s].n_ids;
        const int32_t *expected_ids = ZH_10_GOLDEN[s].ids;

        int32_t act_ids[256];
        size_t act_n = 0;
        rc = snt_g2p_convert(text, strlen(text), act_ids, 256, &act_n);

        if (rc != SNT_OK) {
            printf("[%02d] FAIL '%s': snt_g2p_convert error %d\n", s, text, rc);
            failed++;
            continue;
        }

        if (act_n != expected_n) {
            printf("[%02d] FAIL '%s': len mismatch: expected %zu, got %zu\n",
                   s, text, expected_n, act_n);
            failed++;
            continue;
        }

        bool match = true;
        for (size_t i = 0; i < act_n; i++) {
            if (act_ids[i] != expected_ids[i]) {
                printf("[%02d] FAIL '%s': mismatch at id %zu: expected %d, got %d\n",
                       s, text, i, expected_ids[i], act_ids[i]);
                match = false;
                failed++;
                break;
            }
        }

        if (match) {
            printf("[%02d] PASS '%s': %zu IDs (100.0%% bit-exact)\n", s, text, act_n);
        }
    }

    snt_g2p_deinit();
    if (failed == 0) {
        printf("RESULT: PASS (10/10 golden sentences bit-exact)\n");
        return 0;
    } else {
        printf("RESULT: FAIL (%d/10 golden sentences failed)\n", failed);
        return 1;
    }
}

static int do_cancel_test(const char *text, const char *data_dir) {
    if (data_dir) snt_g2p_set_data_dir(data_dir);
    int rc = snt_g2p_init(SNT_VOICE_ZH);
    if (rc != SNT_OK) return 1;

    /* Request cancel immediately */
    snt_g2p_request_cancel();

    int32_t ids[256];
    size_t used = 0;
    rc = snt_g2p_convert(text, strlen(text), ids, 256, &used);
    printf("Cancel test snt_g2p_convert: rc=%d (expected %d)\n", rc, SNT_ECANCELLED);

    snt_g2p_deinit();
    return (rc == SNT_ECANCELLED) ? 0 : 1;
}

static int do_bench(const char *data_dir, int iters) {
    if (iters <= 0) iters = 10;
    if (data_dir) snt_g2p_set_data_dir(data_dir);

    struct timespec ts0, ts1;
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    int rc = snt_g2p_init(SNT_VOICE_ZH);
    clock_gettime(CLOCK_MONOTONIC, &ts1);
    if (rc != SNT_OK) {
        fprintf(stderr, "bench: init failed %d\n", rc);
        return 1;
    }
    double init_ms = (ts1.tv_sec - ts0.tv_sec) * 1000.0 + (ts1.tv_nsec - ts0.tv_nsec) / 1000000.0;
    printf("Init time: %.2f ms\n", init_ms);

    int32_t ids[512];
    size_t used = 0;
    double total_us = 0;
    size_t total_calls = 0;

    for (int iter = 0; iter < iters; iter++) {
        for (int s = 0; s < 10; s++) {
            const char *t = ZH_10_GOLDEN[s].text;
            clock_gettime(CLOCK_MONOTONIC, &ts0);
            rc = snt_g2p_convert(t, strlen(t), ids, 512, &used);
            clock_gettime(CLOCK_MONOTONIC, &ts1);
            if (rc == SNT_OK) {
                double us = (ts1.tv_sec - ts0.tv_sec) * 1000000.0 + (ts1.tv_nsec - ts0.tv_nsec) / 1000.0;
                total_us += us;
                total_calls++;
            }
        }
    }

    double avg_us = total_us / total_calls;
    printf("Evaluated %zu sentences (%d runs x 10 golden), avg convert latency: %.2f us (%.3f ms)\n",
           total_calls, iters, avg_us, avg_us / 1000.0);

    snt_g2p_deinit();
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];
    const char *text = (argc > 2) ? argv[2] : "";
    const char *data_dir = (argc > 3) ? argv[3] : NULL;

    if (strcmp(cmd, "convert") == 0) {
        return do_convert(text, data_dir);
    } else if (strcmp(cmd, "foreach") == 0) {
        return do_foreach(text, data_dir, false);
    } else if (strcmp(cmd, "bench") == 0) {
        int iters = (argc > 3) ? atoi(argv[3]) : 20;
        return do_bench(text, iters);
    } else if (strcmp(cmd, "golden") == 0) {
        return do_golden(text /* here text is data_dir if passed */);
    } else if (strcmp(cmd, "cancel-test") == 0) {
        return do_cancel_test(text, data_dir);
    } else if (strcmp(cmd, "normalize") == 0) {
        int mode = (argc > 3) ? atoi(argv[3]) : 0;
        char out[1024];
        size_t out_len = 0;
        int rc = snt_text_normalize(text, strlen(text), out, sizeof(out), &out_len, (snt_text_norm_mode_t)mode);
        if (rc == SNT_OK) {
            printf("%s\n", out);
            return 0;
        } else {
            fprintf(stderr, "normalize error %d\n", rc);
            return 1;
        }
    } else {
        print_usage(argv[0]);
        return 1;
    }
}
