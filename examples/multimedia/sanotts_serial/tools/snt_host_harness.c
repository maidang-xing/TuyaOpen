/* snt_host_harness.c -- Portable C host reference runner for sanoTTS.
 *
 * Provides standalone C reference execution for:
 *   1. en_us_e12nano: INT8 duration + acoustic + TinyVocos decoder (snt_nano.c)
 *   2. chinese: FP32 front (DurationStudent + LatentStudent) + piperlite decoder (snt_front_f32.c + snt_piperlite.c)
 *
 * Conforms to snt_contract.h expectations and TuyaOpen portable C standards.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "snt_port.h"
#include "snt_nano.h"
#include "snt_front_f32.h"
#include "snt_piperlite.h"

#define MAX_ROWS 32
#define NOISE_TOL 1e-5
#define SINCOS_TOL 1e-4

/* ---- Helper utilities -------------------------------------------------- */

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

static void wr_u32(FILE *fh, unsigned v) {
    fputc(v & 255, fh); fputc((v >> 8) & 255, fh);
    fputc((v >> 16) & 255, fh); fputc((v >> 24) & 255, fh);
}

static void wr_u16(FILE *fh, unsigned v) {
    fputc(v & 255, fh); fputc((v >> 8) & 255, fh);
}

static int write_wav(const char *path, const float *pcm, long n, int sample_rate) {
    FILE *fh = fopen(path, "wb");
    if (!fh) return -1;
    fwrite("RIFF", 1, 4, fh);
    wr_u32(fh, (unsigned)(36 + 2 * n));
    fwrite("WAVEfmt ", 1, 8, fh);
    wr_u32(fh, 16);
    wr_u16(fh, 1);
    wr_u16(fh, 1);
    wr_u32(fh, (unsigned)sample_rate);
    wr_u32(fh, (unsigned)(sample_rate * 2));
    wr_u16(fh, 2);
    wr_u16(fh, 16);
    fwrite("data", 1, 4, fh);
    wr_u32(fh, (unsigned)(2 * n));
    for (long i = 0; i < n; i++) {
        float v = pcm[i];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        long s = lrintf(v * 32767.0f);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        wr_u16(fh, (unsigned)(s & 0xFFFF));
    }
    fclose(fh);
    return 0;
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

/* Correlation / metrics calculator */
typedef struct {
    const float *gold;
    size_t n_gold, pos;
    double sa, sb, saa, sbb, sab;
    double max_err;
} MetricSink;

static int metric_sink_cb(const float *pcm, int n, void *user) {
    MetricSink *c = (MetricSink *)user;
    for (int i = 0; i < n && c->pos < c->n_gold; i++, c->pos++) {
        double a = pcm[i], b = c->gold[c->pos];
        double err = fabs(a - b);
        if (err > c->max_err) c->max_err = err;
        c->sa += a; c->sb += b;
        c->saa += a * a; c->sbb += b * b; c->sab += a * b;
    }
    return 0;
}

/* ---- English nano suite ------------------------------------------------ */

typedef struct {
    char row_id[64];
    int tokens, frames, samples;
    unsigned long long seed;
} RowMeta;

static int check_uniform(const char *dir, uint64_t seed) {
    size_t nb;
    float *ref = (float *)xload(dir, "e2e_uniform.bin", &nb, 1);
    int n = (int)(nb / sizeof(float));
    float *got = (float *)malloc(nb);
    if (!got) return 1;
    snt_nano_uniform_stream(seed, n, got);
    int bad = 0;
    for (int i = 0; i < n; i++) if (got[i] != ref[i]) bad++;
    printf("uniform : %d values, %d differ %s\n", n, bad,
           bad == 0 ? "-- BIT-EXACT vs torch.rand" : "-- MT19937 MISMATCH");
    free(ref);
    free(got);
    return bad != 0;
}

static int check_noise(const char *dir, uint64_t seed, int channels) {
    size_t nb;
    float *ref = (float *)xload(dir, "e2e_noise.bin", &nb, 1);
    int n = (int)(nb / sizeof(float));
    float *got = (float *)malloc(nb);
    if (!got) return 1;
    if (snt_nano_seeded_noise(seed, channels, n / channels, got) != 0) {
        printf("noise   : generator refused %d values\n", n);
        free(ref); free(got); return 1;
    }
    int bad = 0;
    double worst = 0.0;
    for (int i = 0; i < n; i++) {
        double d = fabs((double)got[i] - (double)ref[i]);
        if (d > worst) worst = d;
        if (got[i] != ref[i]) bad++;
    }
    printf("noise   : %d values, %d differ, max |delta| %.3g (tol %.0e) %s\n",
           n, bad, worst, NOISE_TOL, worst <= NOISE_TOL ? "OK" : "OUT OF BOUND");
    free(ref);
    free(got);
    return worst > NOISE_TOL;
}

static int check_sincos(float phi_max) {
    double worst = 0.0;
    float worst_phi = 0.0f;
    for (int i = 0; i <= 400000; i++) {
        float phi = -phi_max + 2.0f * phi_max * (float)i / 400000.0f;
        float c, s;
        snt_nano_sincos(phi, &c, &s);
        double dc = fabs((double)c - cos((double)phi));
        double ds = fabs((double)s - sin((double)phi));
        double d = dc > ds ? dc : ds;
        if (d > worst) { worst = d; worst_phi = phi; }
    }
    printf("sincos  : max |error| %.3g over |phi| <= %.2f (worst at %.4f, tol %.0e) %s\n",
           worst, phi_max, worst_phi, SINCOS_TOL, worst <= SINCOS_TOL ? "OK" : "OUT OF BOUND");
    return worst > SINCOS_TOL;
}

static int run_en_nano_golden(const char *fixture_dir, int max_eval_rows) {
    printf("=== sanoTTS en_us_e12nano Host Reference ===\n");
    printf("Fixture dir: %s\n", fixture_dir);

    void *front = xload(fixture_dir, "front_q8.bin", NULL, 1);
    void *dec = xload(fixture_dir, "model_q8.bin", NULL, 1);

    char path[1024];
    snprintf(path, sizeof(path), "%s/rows.txt", fixture_dir);
    FILE *mf = fopen(path, "r");
    if (!mf) { fprintf(stderr, "error: missing %s\n", path); return 1; }
    RowMeta rows[MAX_ROWS];
    int n_rows = 0;
    while (n_rows < MAX_ROWS &&
           fscanf(mf, "%63s %d %d %d %llu", rows[n_rows].row_id,
                  &rows[n_rows].tokens, &rows[n_rows].frames,
                  &rows[n_rows].samples, &rows[n_rows].seed) == 5) {
        n_rows++;
    }
    fclose(mf);
    if (n_rows == 0) { fprintf(stderr, "error: %s has no rows\n", path); return 1; }
    if (max_eval_rows > 0 && n_rows > max_eval_rows) {
        n_rows = max_eval_rows;
    }

    uint64_t derived = 0;
    snt_nano_sha256_seed(rows[0].row_id, &derived);
    printf("seed    : sha256(\"%s\")[:8] = %llu, fixture says %llu %s\n",
           rows[0].row_id, (unsigned long long)derived, rows[0].seed,
           derived == rows[0].seed ? "OK" : "-- SHA-256 MISMATCH");

    int bad = (derived != rows[0].seed);
    bad |= check_uniform(fixture_dir, rows[0].seed);
    bad |= check_noise(fixture_dir, rows[0].seed, 4);
    bad |= check_sincos(32.0f);

    static unsigned char arena[768 * 1024] __attribute__((aligned(16)));
    double min_corr = 2.0, sum_corr = 0.0;
    int hard_fail = 0;
    size_t peak_bytes = 0;

    printf("\n%-14s %6s %8s %10s %10s %9s %10s\n",
           "row", "frames", "samples", "corr", "rms_ratio", "us", "arena");

    for (int r = 0; r < n_rows; r++) {
        char name[64];
        size_t nb;
        snprintf(name, sizeof(name), "r%02d_ids.bin", r);
        int32_t *ids = (int32_t *)xload(fixture_dir, name, &nb, 1);
        int n_ids = (int)(nb / 4);

        snprintf(name, sizeof(name), "r%02d_durs.bin", r);
        int32_t *durs = (int32_t *)xload(fixture_dir, name, NULL, 1);

        snprintf(name, sizeof(name), "r%02d_audio.bin", r);
        MetricSink sink;
        memset(&sink, 0, sizeof(sink));
        sink.gold = (const float *)xload(fixture_dir, name, &nb, 1);
        sink.n_gold = nb / 4;

        snt_nano_config cfg;
        cfg.front_blob = front;
        cfg.dec_blob = dec;
        cfg.arena = arena;
        cfg.arena_size = sizeof(arena);
        cfg.dur_override = durs;
        cfg.noise_seed = (uint64_t)rows[r].seed;

        snt_nano_stats st;
        memset(&st, 0, sizeof(st));
        int rc = snt_nano_synthesize(&cfg, ids, n_ids, metric_sink_cb, &sink, &st);
        if (rc != 0) {
            printf("%-14s synthesize returned error %d\n", rows[r].row_id, rc);
            hard_fail = 1;
            free(ids); free(durs); free((void *)sink.gold);
            continue;
        }
        if (st.frames != rows[r].frames || st.samples != rows[r].samples) {
            printf("%-14s length mismatch: got %d / %d, expected %d / %d\n",
                   rows[r].row_id, st.frames, st.samples, rows[r].frames, rows[r].samples);
            hard_fail = 1;
        }

        double n = (double)sink.pos;
        double cov = sink.sab - sink.sa * sink.sb / n;
        double cr = cov / sqrt((sink.saa - sink.sa * sink.sa / n) *
                               (sink.sbb - sink.sb * sink.sb / n) + 1e-30);
        double rms_c = sqrt(sink.saa / n), rms_g = sqrt(sink.sbb / n);
        double rms_ratio = (rms_g > 0.0) ? (rms_c / rms_g) : 0.0;

        printf("%-14s %6d %8d %10.6f %10.6f %9lld %10zu\n",
               rows[r].row_id, st.frames, st.samples, cr, rms_ratio,
               (long long)st.elapsed_us, st.arena_peak);

        if (st.arena_peak > peak_bytes) peak_bytes = st.arena_peak;
        sum_corr += cr;
        if (cr < min_corr) min_corr = cr;

        if (cr < 0.98 || rms_ratio < 0.80 || rms_ratio > 1.25) {
            fprintf(stderr, "error: row %s failed threshold gate (corr=%.6f, rms_ratio=%.6f)\n",
                    rows[r].row_id, cr, rms_ratio);
            hard_fail = 1;
        }

        free(ids);
        free(durs);
        free((void *)sink.gold);
    }

    free(front);
    free(dec);

    printf("\nRows evaluated: %d, mean corr: %.6f, MIN corr: %.6f, arena peak: %zu bytes\n",
           n_rows, sum_corr / n_rows, min_corr, peak_bytes);

    if (bad != 0 || hard_fail != 0 || min_corr < 0.98) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}

/* ---- Chinese baseline suite -------------------------------------------- */

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

static int run_zh_suite(const char *models_dir, const char *fixtures_dir, int count) {
    printf("=== sanoTTS Chinese web/voices Host Reference ===\n");
    printf("Models dir: %s\n", models_dir);
    printf("Fixtures dir: %s\n", fixtures_dir ? fixtures_dir : "(in-memory only)");

    char front_path[1024], dec_path[1024];
    snprintf(front_path, sizeof(front_path), "%s/front_f32.bin", models_dir);
    snprintf(dec_path, sizeof(dec_path), "%s/dec_f32.bin", models_dir);

    size_t front_sz = 0, dec_sz = 0;
    uint8_t *front_blob = (uint8_t *)xload("", front_path, &front_sz, 1);
    uint8_t *dec_blob = (uint8_t *)xload("", dec_path, &dec_sz, 1);

    size_t front_meta_sz, front_weight_floats;
    size_t dec_meta_sz, dec_weight_floats;
    if (front_blob_extents(front_blob, &front_meta_sz, &front_weight_floats) != 0 ||
        piperlite_blob_extents(dec_blob, &dec_meta_sz, &dec_weight_floats) != 0) {
        fprintf(stderr, "error: invalid front or decoder blob extents\n");
        free(front_blob); free(dec_blob);
        return 1;
    }

    snt_front_model fm;
    snt_piperlite_model pm;
    const float *front_weights = (const float *)(front_blob + front_meta_sz);
    const float *dec_weights = (const float *)(dec_blob + dec_meta_sz);
    snt_front_init(&fm, front_blob, front_meta_sz, front_weights, front_weight_floats);
    snt_piperlite_init(&pm, dec_blob, dec_meta_sz, dec_weights, dec_weight_floats);

    int n_eval = (count > 0 && count < 10) ? count : 10;
    int hard_fail = 0;

    printf("\n%-22s %6s %8s %10s %10s %10s %10s\n",
           "text", "frames", "samples", "rms", "peak", "ms", "arena_B");

    for (int s = 0; s < n_eval; s++) {
        const char *text = ZH_10_SENTENCES[s].text;
        int n_ids = ZH_10_SENTENCES[s].n_ids;
        const int32_t *ids = ZH_10_SENTENCES[s].ids;

        clock_t t0 = clock();

        size_t dur_arena_n = snt_front_duration_arena_floats(&fm, n_ids);
        float *dur_arena = (float *)malloc(dur_arena_n * sizeof(float));
        int32_t *durations = (int32_t *)malloc((size_t)n_ids * sizeof(int32_t));
        int32_t *dur_ids = (int32_t *)malloc((size_t)n_ids * sizeof(int32_t));
        for (int i = 0; i < n_ids; i++)
            dur_ids[i] = (ids[i] >= 0 && ids[i] < fm.d_vocab) ? ids[i] : (59 < fm.d_vocab ? 59 : 0);
        long frames = snt_front_durations(&fm, dur_ids, n_ids, 1.0f, durations, dur_arena, dur_arena_n);
        free(dur_ids);
        free(dur_arena);

        if (frames <= 0) {
            fprintf(stderr, "error: front durations failed for '%s'\n", text);
            hard_fail = 1;
            free(durations);
            continue;
        }

        size_t lat_arena_n = snt_front_latent_arena_floats(&fm, n_ids, frames);
        size_t latent_n = (size_t)fm.a_out * (size_t)frames;
        float *lat_arena = (float *)malloc(lat_arena_n * sizeof(float));
        float *latent = (float *)malloc(latent_n * sizeof(float));
        int32_t *ac_ids = (int32_t *)malloc((size_t)n_ids * sizeof(int32_t));
        for (int i = 0; i < n_ids; i++)
            ac_ids[i] = (ids[i] >= 0 && ids[i] < fm.a_vocab) ? ids[i] : (59 < fm.a_vocab ? 59 : 0);
        int rc = snt_front_latent(&fm, ac_ids, durations, n_ids, frames, latent, lat_arena, lat_arena_n);
        free(ac_ids);
        free(durations);
        free(lat_arena);
        if (rc != 0) {
            fprintf(stderr, "error: front latent failed for '%s'\n", text);
            hard_fail = 1;
            free(latent);
            continue;
        }

        size_t pl_arena_n = snt_piperlite_arena_floats(&pm, (int)frames);
        float *pl_arena = (float *)malloc(pl_arena_n * sizeof(float));
        long n_samples = frames * SNT_PIPERLITE_HOP;
        float *out = (float *)malloc((size_t)n_samples * sizeof(float));
        rc = snt_piperlite_synthesize(&pm, latent, (int)frames, out, pl_arena, pl_arena_n);
        free(latent);
        free(pl_arena);

        clock_t t1 = clock();
        double elapsed_ms = (double)(t1 - t0) * 1000.0 / CLOCKS_PER_SEC;

        if (rc != 0) {
            fprintf(stderr, "error: piperlite synthesize failed for '%s'\n", text);
            hard_fail = 1;
            free(out);
            continue;
        }

        // Metrics
        double rms = 0, peak = 0;
        int has_nan = 0;
        for (long i = 0; i < n_samples; i++) {
            float v = out[i];
            if (isnan(v) || isinf(v)) has_nan = 1;
            rms += (double)v * v;
            if (fabs(v) > peak) peak = fabs(v);
        }
        rms = sqrt(rms / (double)n_samples);

        printf("%-22s %6ld %8ld %10.4f %10.4f %10.1f %10zu\n",
               text, frames, n_samples, rms, peak, elapsed_ms, pl_arena_n * sizeof(float));

        if (has_nan) {
            fprintf(stderr, "error: audio contains NaN or Inf\n");
            hard_fail = 1;
        }
        if (rms < 0.001 || peak < 0.01) {
            fprintf(stderr, "error: audio is near silence (rms=%.4f, peak=%.4f)\n", rms, peak);
            hard_fail = 1;
        }

        // If fixture directory is provided, verify against golden WAV / audio
        if (fixtures_dir && strlen(fixtures_dir) > 0) {
            char gold_fn[64];
            snprintf(gold_fn, sizeof(gold_fn), "s%02d_audio.bin", s);
            size_t gold_nb = 0;
            float *gold = (float *)xload(fixtures_dir, gold_fn, &gold_nb, 0);
            if (gold) {
                long gold_samples = (long)(gold_nb / sizeof(float));
                if (gold_samples != n_samples) {
                    fprintf(stderr, "error: samples mismatch with fixture: got %ld, gold %ld\n", n_samples, gold_samples);
                    hard_fail = 1;
                } else {
                    double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0, max_err = 0;
                    for (long i = 0; i < n_samples; i++) {
                        double a = out[i], b = gold[i];
                        double err = fabs(a - b);
                        if (err > max_err) max_err = err;
                        sa += a; sb += b;
                        saa += a * a; sbb += b * b;
                        sab += a * b;
                    }
                    double cov = sab - sa * sb / (double)n_samples;
                    double cr = cov / sqrt((saa - sa * sa / (double)n_samples) *
                                           (sbb - sb * sb / (double)n_samples) + 1e-30);
                    if (cr < 0.9999 || max_err > 1e-4) {
                        fprintf(stderr, "error: mismatch with golden fixture: corr=%.6f, max_err=%.6e\n", cr, max_err);
                        hard_fail = 1;
                    }
                }
                free(gold);
            }
        }

        free(out);
    }

    free(front_blob);
    free(dec_blob);

    if (hard_fail) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}

/* ---- Generate fixtures tool -------------------------------------------- */

static int generate_zh_fixtures(const char *models_dir, const char *out_dir) {
    char front_path[1024], dec_path[1024];
    snprintf(front_path, sizeof(front_path), "%s/front_f32.bin", models_dir);
    snprintf(dec_path, sizeof(dec_path), "%s/dec_f32.bin", models_dir);

    size_t front_sz = 0, dec_sz = 0;
    uint8_t *front_blob = (uint8_t *)xload("", front_path, &front_sz, 1);
    uint8_t *dec_blob = (uint8_t *)xload("", dec_path, &dec_sz, 1);

    size_t front_meta_sz, front_weight_floats;
    size_t dec_meta_sz, dec_weight_floats;
    front_blob_extents(front_blob, &front_meta_sz, &front_weight_floats);
    piperlite_blob_extents(dec_blob, &dec_meta_sz, &dec_weight_floats);

    snt_front_model fm;
    snt_piperlite_model pm;
    const float *front_weights = (const float *)(front_blob + front_meta_sz);
    const float *dec_weights = (const float *)(dec_blob + dec_meta_sz);
    snt_front_init(&fm, front_blob, front_meta_sz, front_weights, front_weight_floats);
    snt_piperlite_init(&pm, dec_blob, dec_meta_sz, dec_weights, dec_weight_floats);

    char json_path[1024];
    snprintf(json_path, sizeof(json_path), "%s/zh_golden.json", out_dir);
    FILE *jf = fopen(json_path, "w");
    if (!jf) { fprintf(stderr, "error: cannot open %s for writing\n", json_path); return 1; }
    fprintf(jf, "[\n");

    for (int s = 0; s < 10; s++) {
        const char *text = ZH_10_SENTENCES[s].text;
        int n_ids = ZH_10_SENTENCES[s].n_ids;
        const int32_t *ids = ZH_10_SENTENCES[s].ids;

        size_t dur_arena_n = snt_front_duration_arena_floats(&fm, n_ids);
        float *dur_arena = (float *)malloc(dur_arena_n * sizeof(float));
        int32_t *durations = (int32_t *)malloc((size_t)n_ids * sizeof(int32_t));
        int32_t *dur_ids = (int32_t *)malloc((size_t)n_ids * sizeof(int32_t));
        for (int i = 0; i < n_ids; i++)
            dur_ids[i] = (ids[i] >= 0 && ids[i] < fm.d_vocab) ? ids[i] : (59 < fm.d_vocab ? 59 : 0);
        long frames = snt_front_durations(&fm, dur_ids, n_ids, 1.0f, durations, dur_arena, dur_arena_n);
        free(dur_ids);
        free(dur_arena);

        size_t lat_arena_n = snt_front_latent_arena_floats(&fm, n_ids, frames);
        size_t latent_n = (size_t)fm.a_out * (size_t)frames;
        float *lat_arena = (float *)malloc(lat_arena_n * sizeof(float));
        float *latent = (float *)malloc(latent_n * sizeof(float));
        int32_t *ac_ids = (int32_t *)malloc((size_t)n_ids * sizeof(int32_t));
        for (int i = 0; i < n_ids; i++)
            ac_ids[i] = (ids[i] >= 0 && ids[i] < fm.a_vocab) ? ids[i] : (59 < fm.a_vocab ? 59 : 0);
        snt_front_latent(&fm, ac_ids, durations, n_ids, frames, latent, lat_arena, lat_arena_n);
        free(ac_ids);
        free(lat_arena);

        size_t pl_arena_n = snt_piperlite_arena_floats(&pm, (int)frames);
        float *pl_arena = (float *)malloc(pl_arena_n * sizeof(float));
        long n_samples = frames * SNT_PIPERLITE_HOP;
        float *out = (float *)malloc((size_t)n_samples * sizeof(float));
        snt_piperlite_synthesize(&pm, latent, (int)frames, out, pl_arena, pl_arena_n);
        free(latent);
        free(pl_arena);

        char fn[1024];
        snprintf(fn, sizeof(fn), "%s/s%02d_ids.bin", out_dir, s);
        FILE *f = fopen(fn, "wb");
        fwrite(ids, sizeof(int32_t), (size_t)n_ids, f);
        fclose(f);

        snprintf(fn, sizeof(fn), "%s/s%02d_durs.bin", out_dir, s);
        f = fopen(fn, "wb");
        fwrite(durations, sizeof(int32_t), (size_t)n_ids, f);
        fclose(f);

        snprintf(fn, sizeof(fn), "%s/s%02d_audio.bin", out_dir, s);
        f = fopen(fn, "wb");
        fwrite(out, sizeof(float), (size_t)n_samples, f);
        fclose(f);

        snprintf(fn, sizeof(fn), "%s/s%02d.wav", out_dir, s);
        write_wav(fn, out, n_samples, 22050);

        double rms = 0, peak = 0;
        for (long i = 0; i < n_samples; i++) {
            float v = out[i];
            rms += (double)v * v;
            if (fabs(v) > peak) peak = fabs(v);
        }
        rms = sqrt(rms / (double)n_samples);

        printf("Generated s%02d (%s): frames=%ld, samples=%ld (%.2fs), rms=%.4f, peak=%.4f, arena=%zu B\n",
               s, text, frames, n_samples, (double)n_samples / 22050.0, rms, peak, pl_arena_n * sizeof(float));

        fprintf(jf, "  {\n");
        fprintf(jf, "    \"index\": %d,\n", s);
        fprintf(jf, "    \"text\": \"%s\",\n", text);
        fprintf(jf, "    \"n_ids\": %d,\n", n_ids);
        fprintf(jf, "    \"frames\": %ld,\n", frames);
        fprintf(jf, "    \"samples\": %ld,\n", n_samples);
        fprintf(jf, "    \"sample_rate\": 22050,\n");
        fprintf(jf, "    \"duration_s\": %.4f,\n", (double)n_samples / 22050.0);
        fprintf(jf, "    \"rms\": %.6f,\n", rms);
        fprintf(jf, "    \"peak\": %.6f,\n", peak);
        fprintf(jf, "    \"pl_arena_bytes\": %zu,\n", pl_arena_n * sizeof(float));
        fprintf(jf, "    \"ids\": [");
        for (int i = 0; i < n_ids; i++) {
            fprintf(jf, "%d%s", ids[i], (i == n_ids - 1) ? "" : ", ");
        }
        fprintf(jf, "]\n");
        fprintf(jf, "  }%s\n", (s == 9) ? "" : ",");

        free(durations);
        free(out);
    }

    fprintf(jf, "]\n");
    fclose(jf);

    free(front_blob);
    free(dec_blob);
    printf("Successfully wrote 10 Chinese fixtures into %s\n", out_dir);
    return 0;
}

/* ---- Entry point ------------------------------------------------------- */

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage:\n");
        printf("  %s en-nano-golden <fixture_dir> [max_rows]\n", argv[0]);
        printf("  %s zh-suite <models_dir> [fixtures_dir] [count]\n", argv[0]);
        printf("  %s gen-zh-fixtures <models_dir> <out_dir>\n", argv[0]);
        return 1;
    }

    const char *cmd = argv[1];
    if (strcmp(cmd, "en-nano-golden") == 0) {
        const char *dir = (argc > 2) ? argv[2] : "examples/multimedia/sanotts_serial/tests/fixtures/en_us_e12nano";
        int max_rows = (argc > 3) ? atoi(argv[3]) : 0;
        return run_en_nano_golden(dir, max_rows);
    } else if (strcmp(cmd, "zh-suite") == 0) {
        const char *models_dir = (argc > 2) ? argv[2] : "examples/multimedia/sanotts_serial/models/chinese";
        const char *fixtures_dir = (argc > 3 && strlen(argv[3]) > 0) ? argv[3] : NULL;
        int count = (argc > 4) ? atoi(argv[4]) : 0;
        return run_zh_suite(models_dir, fixtures_dir, count);
    } else if (strcmp(cmd, "gen-zh-fixtures") == 0) {
        const char *models_dir = (argc > 2) ? argv[2] : "examples/multimedia/sanotts_serial/models/chinese";
        const char *out_dir = (argc > 3) ? argv[3] : "examples/multimedia/sanotts_serial/tests/fixtures/chinese";
        return generate_zh_fixtures(models_dir, out_dir);
    } else {
        fprintf(stderr, "unknown command: %s\n", cmd);
        return 1;
    }
}
