/**
 * @file snt_resampler.h
 * @brief Band-limited audio resampler with anti-aliasing and float to PCM16 conversion
 */

#ifndef __SNT_RESAMPLER_H__
#define __SNT_RESAMPLER_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SNT_RESAMPLER_HALF_WIDTH 10
#define SNT_RESAMPLER_HIST_MAX   (2 * SNT_RESAMPLER_HALF_WIDTH)

typedef struct {
    uint32_t in_rate;
    uint32_t out_rate;
    uint32_t ratio_num; /* Numerator of in_rate / out_rate (e.g. 441) */
    uint32_t ratio_den; /* Denominator of in_rate / out_rate (e.g. 320) */
    uint64_t phase_num; /* Exact timeline phase in units of (1 / ratio_den) */
    float fc;           /* Normalized cutoff frequency */
    float history[SNT_RESAMPLER_HIST_MAX];
    size_t history_len;
    bool is_passthrough;
} snt_resampler_t;

/* Initialize resampler. Returns 0 on success, negative on error */
int snt_resampler_init(snt_resampler_t *r, uint32_t in_rate, uint32_t out_rate);

/* Reset filter state (phase and history) */
void snt_resampler_reset(snt_resampler_t *r);

/* Process chunk of float samples into int16_t PCM.
 * out_written receives the number of int16_t samples produced.
 * Returns 0 on success, negative on error. */
int snt_resampler_process(snt_resampler_t *r,
                          const float *in, size_t in_count,
                          int16_t *out, size_t out_cap,
                          size_t *out_written);

/* Flush any pending samples in filter delay line */
int snt_resampler_flush(snt_resampler_t *r,
                        int16_t *out, size_t out_cap,
                        size_t *out_written);

/* Convert single float to int16_t with clamping and NaN rejection */
int16_t snt_float_to_pcm16(float val);

#ifdef __cplusplus
}
#endif

#endif /* __SNT_RESAMPLER_H__ */
