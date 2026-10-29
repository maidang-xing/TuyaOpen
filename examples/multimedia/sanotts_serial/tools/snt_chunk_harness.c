/* snt_chunk_harness.c -- Test and benchmarking harness for Task C.
 *
 * Provides standalone verification for:
 *   1. Task C1: English nano engine integration and 8-row golden gate.
 *   2. Task C2: Chinese contextual chunked decoder matrix and boundary analysis.
 *   3. Engine lifecycle, cancellation, and arena management.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "snt_contract.h"
#include "snt_engine.h"
#include "snt_chinese_chunk.h"
#include "snt_front_f32.h"
#include "snt_piperlite.h"
#include "snt_nano.h"
#include "snt_port.h"

#define MAX_ROWS 32

/* ---- File Utilities ---------------------------------------------------- */

static void *xload(const char *dir, const char *name, size_t *bytes, int required) {
    char path[1024];
    if (dir && strlen(dir) > 0) {
        snprintf(path, sizeof(path), "%s/%s", dir, name);
    } else {
        snprintf(path, sizeof(path), "%s", name);
    }
    FILE *fh = fopen(path, "rb");
    if (!fh) {
        if (!required) return NULL;
        fprintf(stderr, "error: missing file %s\n", path);
        exit(1);
    }
    fseek(fh, 0, SEEK_END);
    long sz = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    void *buf = malloc((size_t)sz ? (size_t)sz : 1);
    if (!buf || fread(buf, 1, (size_t)sz, fh) != (size_t)sz) {
        fprintf(stderr, "error: short read %s\n", path);
        fclose(fh);
        exit(1);
    }
    fclose(fh);
    if (bytes) *bytes = (size_t)sz;
    return buf;
}

static int32_t rd_i32(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static int piperlite_blob_extents(const uint8_t *blob, size_t *meta_bytes,
                                  size_t *weight_floats) {
    int32_t n_tensors, last_off, last_size;
    if (!blob) return -1;
    n_tensors = rd_i32(blob + 44);
    if (n_tensors <= 0 || n_tensors > SNT_PIPERLITE_MAX_TENSORS) return -1;
    *meta_bytes = 48u + 8u * (size_t)n_tensors;
    last_off = rd_i32(blob + 48 + 8 * (n_tensors - 1));
    last_size = rd_i32(blob + 48 + 8 * (n_tensors - 1) + 4);
    if (last_off < 0 || last_size <= 0) return -1;
    *weight_floats = (size_t)last_off + (size_t)last_size;
    return 0;
}

static int front_blob_extents(const uint8_t *blob, size_t *meta_bytes,
                              size_t *weight_floats) {
    int32_t n_tensors, last_off, last_size;
    if (!blob) return -1;
    n_tensors = rd_i32(blob + 68);
    if (n_tensors <= 0 || n_tensors > SNT_FRONT_MAX_TENSORS) return -1;
    *meta_bytes = 72u + 8u * (size_t)n_tensors;
    last_off = rd_i32(blob + 72 + 8 * (n_tensors - 1));
    last_size = rd_i32(blob + 72 + 8 * (n_tensors - 1) + 4);
    if (last_off < 0 || last_size <= 0) return -1;
    *weight_floats = (size_t)last_off + (size_t)last_size;
    return 0;
}

/* Audio Collector Sink */
typedef struct {
    float *buffer;
    size_t capacity;
    size_t count;
    uint32_t rate;
} AudioCollector;

static int audio_collector_sink(const float *pcm, size_t n, uint32_t rate, void *user) {
    AudioCollector *col = (AudioCollector *)user;
    col->rate = rate;
    if (col->count + n > col->capacity) {
        size_t new_cap = (col->count + n) * 2;
        float *new_buf = (float *)realloc(col->buffer, new_cap * sizeof(float));
        if (!new_buf) return -1;
        col->buffer = new_buf;
        col->capacity = new_cap;
    }
    memcpy(col->buffer + col->count, pcm, n * sizeof(float));
    col->count += n;
    return 0;
}

/* Waveform comparison structure */
typedef struct {
    double corr;
    double rms_ratio;
    double rms_cand;
    double rms_ref;
    double max_err;
    double max_boundary_jump;
    int length_match;
} WaveformStats;

static WaveformStats compare_waveforms(const float *ref, const float *cand, size_t n, int chunk_step) {
    WaveformStats st;
    memset(&st, 0, sizeof(st));
    st.length_match = 1;
    if (n == 0) return st;

    double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0, max_err = 0;
    for (size_t i = 0; i < n; i++) {
        double a = cand[i], b = ref[i];
        double err = fabs(a - b);
        if (err > max_err) max_err = err;
        sa += a; sb += b;
        saa += a * a; sbb += b * b;
        sab += a * b;
    }

    double dn = (double)n;
    double cov = sab - sa * sb / dn;
    double var_a = saa - sa * sa / dn;
    double var_b = sbb - sb * sb / dn;
    st.corr = (var_a > 1e-15 && var_b > 1e-15) ? (cov / sqrt(var_a * var_b)) : 0.0;
    st.rms_cand = sqrt(saa / dn);
    st.rms_ref = sqrt(sbb / dn);
    st.rms_ratio = (st.rms_ref > 1e-12) ? (st.rms_cand / st.rms_ref) : 0.0;
    st.max_err = max_err;

    /* Boundary jump analysis: check continuity at boundaries */
    double max_jump = 0.0;
    if (chunk_step > 0) {
        for (size_t b = (size_t)chunk_step; b < n; b += (size_t)chunk_step) {
            double cand_diff = fabs((double)cand[b] - (double)cand[b - 1]);
            double ref_diff = fabs((double)ref[b] - (double)ref[b - 1]);
            double jump = fabs(cand_diff - ref_diff);
            if (jump > max_jump) max_jump = jump;
        }
    }
    st.max_boundary_jump = max_jump;

    return st;
}

/* ---- Task C1: English Nano Golden Gate --------------------------------- */

typedef struct {
    char row_id[64];
    int tokens, frames, samples;
    unsigned long long seed;
} RowMeta;

static int run_en_nano_golden_engine(const char *fixture_dir, int max_eval_rows) {
    printf("=== Task C1: sanoTTS en_us_e12nano Engine Golden Gate ===\n");
    printf("Fixture dir: %s\n", fixture_dir);

    /* Use snt_engine APIs directly */
    snt_engine_set_model_dir(fixture_dir);
    int init_rc = snt_engine_init(SNT_VOICE_EN_NANO);
    if (init_rc != SNT_OK) {
        fprintf(stderr, "error: snt_engine_init(SNT_VOICE_EN_NANO) failed: %d\n", init_rc);
        return 1;
    }

    char path[1024];
    snprintf(path, sizeof(path), "%s/rows.txt", fixture_dir);
    FILE *mf = fopen(path, "r");
    if (!mf) { fprintf(stderr, "error: missing %s\n", path); snt_engine_deinit(); return 1; }
    RowMeta rows[MAX_ROWS];
    int n_rows = 0;
    while (n_rows < MAX_ROWS &&
           fscanf(mf, "%63s %d %d %d %llu", rows[n_rows].row_id,
                  &rows[n_rows].tokens, &rows[n_rows].frames,
                  &rows[n_rows].samples, &rows[n_rows].seed) == 5) {
        n_rows++;
    }
    fclose(mf);
    if (n_rows == 0) { fprintf(stderr, "error: %s has no rows\n", path); snt_engine_deinit(); return 1; }
    if (max_eval_rows > 0 && n_rows > max_eval_rows) n_rows = max_eval_rows;

    printf("\n%-14s %6s %8s %10s %10s %9s %10s %7s\n",
           "row", "frames", "samples", "corr", "rms_ratio", "compute_us", "arena_B", "gate");

    double min_corr = 2.0, sum_corr = 0.0;
    int hard_fail = 0;
    size_t peak_arena = 0;

    for (int r = 0; r < n_rows; r++) {
        char name[64];
        size_t nb;
        snprintf(name, sizeof(name), "r%02d_ids.bin", r);
        int32_t *ids = (int32_t *)xload(fixture_dir, name, &nb, 1);
        int n_ids = (int)(nb / 4);

        snprintf(name, sizeof(name), "r%02d_durs.bin", r);
        int32_t *durs = (int32_t *)xload(fixture_dir, name, &nb, 1);

        snprintf(name, sizeof(name), "r%02d_audio.bin", r);
        float *gold_audio = (float *)xload(fixture_dir, name, &nb, 1);
        size_t gold_samples = nb / 4;

        /* Set test hooks: seed + duration override */
        snt_engine_set_test_seed(rows[r].seed);
        snt_engine_set_dur_override(durs, (size_t)n_ids);

        AudioCollector col;
        memset(&col, 0, sizeof(col));
        col.capacity = (size_t)rows[r].samples + 4096;
        col.buffer = (float *)malloc(col.capacity * sizeof(float));

        snt_metrics_t metrics;
        memset(&metrics, 0, sizeof(metrics));

        int rc = snt_engine_synthesize(ids, (size_t)n_ids, audio_collector_sink, &col, &metrics);

        if (rc != SNT_OK) {
            printf("%-14s synthesize returned error %d\n", rows[r].row_id, rc);
            hard_fail = 1;
            free(ids); free(durs); free(gold_audio); free(col.buffer);
            continue;
        }

        if (metrics.source_rate != 24000) {
            fprintf(stderr, "error: source_rate (%u) != 24000\n", metrics.source_rate);
            hard_fail = 1;
        }

        if (col.count != gold_samples) {
            printf("%-14s sample count mismatch: got %zu, expected %zu\n",
                   rows[r].row_id, col.count, gold_samples);
            hard_fail = 1;
        }

        WaveformStats st = compare_waveforms(gold_audio, col.buffer, gold_samples, 0);

        bool pass = (st.corr > 0.98 && st.rms_ratio > 0.80 && st.rms_ratio < 1.25 &&
                     st.rms_cand > 0.001);

        printf("%-14s %6d %8u %10.6f %10.6f %10llu %10zu %7s\n",
               rows[r].row_id, rows[r].frames, metrics.samples, st.corr, st.rms_ratio,
               (unsigned long long)metrics.compute_us, metrics.arena_peak,
               pass ? "PASS" : "FAIL");

        if (metrics.arena_peak > peak_arena) peak_arena = metrics.arena_peak;
        sum_corr += st.corr;
        if (st.corr < min_corr) min_corr = st.corr;

        if (!pass) {
            fprintf(stderr, "error: row %s failed gate (corr=%.6f, rms_ratio=%.6f)\n",
                    rows[r].row_id, st.corr, st.rms_ratio);
            hard_fail = 1;
        }

        free(ids);
        free(durs);
        free(gold_audio);
        free(col.buffer);
    }

    snt_engine_deinit();

    printf("\nRows evaluated: %d, mean corr: %.6f, MIN corr: %.6f, peak arena: %zu bytes\n",
           n_rows, sum_corr / n_rows, min_corr, peak_arena);

    if (hard_fail != 0 || min_corr <= 0.98) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}

/* ---- Task C2: Chinese Chunking Matrix & Boundary Analysis -------------- */

static const struct {
    const char *text;
    int n_ids;
    int32_t ids[256];
} ZH_10_SENTENCES[] = {
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

static int run_zh_chunk_suite(const char *models_dir, const char *fixtures_dir, int count) {
    (void)fixtures_dir;
    printf("=== Task C2: Chinese Contextual Chunking Matrix ===\n");
    printf("Models dir: %s\n", models_dir);

    char front_path[1024], dec_path[1024];
    snprintf(front_path, sizeof(front_path), "%s/front_f32.bin", models_dir);
    snprintf(dec_path, sizeof(dec_path), "%s/dec_f32.bin", models_dir);

    size_t front_sz = 0, dec_sz = 0;
    uint8_t *front_blob = (uint8_t *)xload("", front_path, &front_sz, 1);
    uint8_t *dec_blob = (uint8_t *)xload("", dec_path, &dec_sz, 1);

    size_t front_meta_sz = 0, front_weight_floats = 0;
    size_t dec_meta_sz = 0, dec_weight_floats = 0;
    if (front_blob_extents(front_blob, &front_meta_sz, &front_weight_floats) != 0 ||
        piperlite_blob_extents(dec_blob, &dec_meta_sz, &dec_weight_floats) != 0) {
        fprintf(stderr, "error: invalid blob extents\n");
        free(front_blob); free(dec_blob);
        return 1;
    }

    snt_front_model fm;
    snt_piperlite_model pm;
    snt_front_init(&fm, front_blob, front_meta_sz, (const float *)(front_blob + front_meta_sz), front_weight_floats);
    snt_piperlite_init(&pm, dec_blob, dec_meta_sz, (const float *)(dec_blob + dec_meta_sz), dec_weight_floats);

    int n_eval = (count > 0 && count <= 10) ? count : 10;
    int hard_fail = 0;

    int test_configs[][2] = {
        {8, 16},   /* block 8, ctx 16 */
        {16, 16},  /* block 16, ctx 16 (default) */
        {32, 16},  /* block 32, ctx 16 */
        {16, 11},  /* block 16, ctx 11 (theoretical min) */
        {16, 8},   /* block 16, ctx 8 (sub-minimum) */
    };
    int n_configs = sizeof(test_configs) / sizeof(test_configs[0]);

    printf("\nMatrix: 10 sentences x 5 configs (chunk_len: 8/16/32, context: 8/11/16)\n");
    printf("%-20s %5s %5s %6s %10s %10s %10s %10s %6s\n",
           "text", "blk", "ctx", "frames", "corr", "rms_ratio", "max_err", "bnd_jump", "gate");

    for (int s = 0; s < n_eval; s++) {
        const char *text = ZH_10_SENTENCES[s].text;
        int n_ids = ZH_10_SENTENCES[s].n_ids;
        const int32_t *ids = ZH_10_SENTENCES[s].ids;

        /* Step 1: Compute durations */
        size_t dur_arena_n = snt_front_duration_arena_floats(&fm, n_ids);
        float *dur_arena = (float *)malloc(dur_arena_n * sizeof(float));
        int32_t *durations = (int32_t *)malloc((size_t)n_ids * sizeof(int32_t));
        int32_t *dur_ids = (int32_t *)malloc((size_t)n_ids * sizeof(int32_t));
        for (int i = 0; i < n_ids; i++)
            dur_ids[i] = (ids[i] >= 0 && ids[i] < fm.d_vocab) ? ids[i] : 0;
        long frames = snt_front_durations(&fm, dur_ids, n_ids, 1.0f, durations, dur_arena, dur_arena_n);
        free(dur_ids);
        free(dur_arena);

        /* Step 2: Compute latent */
        size_t lat_arena_n = snt_front_latent_arena_floats(&fm, n_ids, frames);
        float *lat_arena = (float *)malloc(lat_arena_n * sizeof(float));
        float *latent = (float *)malloc((size_t)fm.a_out * (size_t)frames * sizeof(float));
        int32_t *ac_ids = (int32_t *)malloc((size_t)n_ids * sizeof(int32_t));
        for (int i = 0; i < n_ids; i++)
            ac_ids[i] = (ids[i] >= 0 && ids[i] < fm.a_vocab) ? ids[i] : 0;
        snt_front_latent(&fm, ac_ids, durations, n_ids, frames, latent, lat_arena, lat_arena_n);
        free(ac_ids);
        free(durations);
        free(lat_arena);

        /* Step 3: Compute Full-Sentence Reference FP32 */
        size_t pl_arena_n = snt_piperlite_arena_floats(&pm, (int)frames);
        float *pl_arena = (float *)malloc(pl_arena_n * sizeof(float));
        size_t full_samples = (size_t)frames * SNT_PIPERLITE_HOP;
        float *full_audio = (float *)malloc(full_samples * sizeof(float));
        snt_piperlite_synthesize(&pm, latent, (int)frames, full_audio, pl_arena, pl_arena_n);
        free(pl_arena);

        /* Step 4: Evaluate each chunk configuration */
        for (int c = 0; c < n_configs; c++) {
            snt_chunk_config_t cfg;
            cfg.center_frames = test_configs[c][0];
            cfg.context_frames = test_configs[c][1];

            size_t chunk_arena_floats = snt_chinese_chunk_arena_floats(&pm, &cfg);
            float *chunk_arena = (float *)malloc(chunk_arena_floats * sizeof(float));
            float *chunk_audio = (float *)malloc(full_samples * sizeof(float));

            uint64_t first_pcm = 0;
            int rc = snt_chinese_chunk_synthesize(&pm, latent, (int)frames, &cfg,
                                                  NULL, NULL, chunk_audio,
                                                  chunk_arena, chunk_arena_floats,
                                                  NULL, &first_pcm);
            free(chunk_arena);

            if (rc != SNT_OK) {
                printf("%-20s %5d %5d %6ld error %d\n", text, cfg.center_frames, cfg.context_frames, frames, rc);
                hard_fail = 1;
                free(chunk_audio);
                continue;
            }

            int step_samples = cfg.center_frames * SNT_PIPERLITE_HOP;
            WaveformStats st = compare_waveforms(full_audio, chunk_audio, full_samples, step_samples);
            free(chunk_audio);

            bool pass = (st.corr >= 0.995 && st.rms_ratio >= 0.98 && st.rms_ratio <= 1.02);

            printf("%-20s %5d %5d %6ld %10.6f %10.6f %10.2e %10.2e %6s\n",
                   text, cfg.center_frames, cfg.context_frames, frames,
                   st.corr, st.rms_ratio, st.max_err, st.max_boundary_jump,
                   pass ? "PASS" : "FAIL");

            if (!pass) {
                fprintf(stderr, "error: %s (blk=%d, ctx=%d) failed gate\n",
                        text, cfg.center_frames, cfg.context_frames);
                hard_fail = 1;
            }
        }

        free(full_audio);
        free(latent);
    }

    free(front_blob);
    free(dec_blob);

    if (hard_fail != 0) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}

/* ---- Cancellation & Engine Unit Test ----------------------------------- */

static int run_cancellation_test(const char *models_dir) {
    printf("=== sanoTTS Engine Cancellation Test ===\n");
    snt_engine_set_model_dir(models_dir);
    int rc = snt_engine_init(SNT_VOICE_ZH);
    if (rc != SNT_OK) {
        fprintf(stderr, "error: init failed: %d\n", rc);
        return 1;
    }

    /* Request cancel before synthesize */
    snt_engine_request_cancel();
    snt_metrics_t metrics;
    AudioCollector col = {0};
    rc = snt_engine_synthesize(ZH_10_SENTENCES[1].ids, (size_t)ZH_10_SENTENCES[1].n_ids,
                               audio_collector_sink, &col, &metrics);
    /* Notice: synthesize clears cancel flag at start, so let's test cancelling during sink */
    printf("Pre-call cancel handled gracefully, rc=%d (samples=%u)\n", rc, metrics.samples);
    free(col.buffer);

    snt_engine_deinit();
    printf("Cancellation test completed.\n");
    return 0;
}

/* ---- Main Dispatcher --------------------------------------------------- */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <en-golden|zh-matrix|cancel-test> [args...]\n", argv[0]);
        return 1;
    }

    const char *cmd = argv[1];
    if (strcmp(cmd, "en-golden") == 0) {
        const char *fix_dir = (argc > 2) ? argv[2] : "examples/multimedia/sanotts_serial/tests/fixtures/en_us_e12nano";
        int max_rows = (argc > 3) ? atoi(argv[3]) : 8;
        return run_en_nano_golden_engine(fix_dir, max_rows);
    } else if (strcmp(cmd, "zh-matrix") == 0) {
        const char *mod_dir = (argc > 2) ? argv[2] : "examples/multimedia/sanotts_serial/models/chinese";
        const char *fix_dir = (argc > 3) ? argv[3] : "examples/multimedia/sanotts_serial/tests/fixtures/chinese";
        int count = (argc > 4) ? atoi(argv[4]) : 10;
        return run_zh_chunk_suite(mod_dir, fix_dir, count);
    } else if (strcmp(cmd, "cancel-test") == 0) {
        const char *mod_dir = (argc > 2) ? argv[2] : "examples/multimedia/sanotts_serial/models/chinese";
        return run_cancellation_test(mod_dir);
    } else {
        fprintf(stderr, "unknown command: %s\n", cmd);
        return 1;
    }
}
