/**
 * @file snt_resampler.c
 * @brief Band-limited audio resampler with anti-aliasing and float to PCM16 conversion
 */

#include "snt_resampler.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static uint32_t gcd(uint32_t a, uint32_t b)
{
    while (b != 0) {
        uint32_t t = b;
        b = a % b;
        a = t;
    }
    return a;
}

int16_t snt_float_to_pcm16(float val)
{
    /* NaN / Inf check */
    if (val != val) {
        return 0;
    }
    if (val >= 1.0f) {
        return 32767;
    }
    if (val <= -1.0f) {
        return -32768;
    }

    float s = val * 32767.0f;
    if (s >= 32767.0f) {
        return 32767;
    }
    if (s <= -32768.0f) {
        return -32768;
    }

    return (int16_t)(s >= 0.0f ? (s + 0.5f) : (s - 0.5f));
}

int snt_resampler_init(snt_resampler_t *r, uint32_t in_rate, uint32_t out_rate)
{
    if (!r || in_rate == 0 || out_rate == 0) {
        return -1;
    }

    memset(r, 0, sizeof(snt_resampler_t));
    r->in_rate = in_rate;
    r->out_rate = out_rate;

    uint32_t g = gcd(in_rate, out_rate);
    r->ratio_num = in_rate / g;
    r->ratio_den = out_rate / g;
    r->phase_num = 0;

    if (in_rate == out_rate) {
        r->is_passthrough = true;
        r->fc = 0.5f;
    } else {
        r->is_passthrough = false;
        /* Downsampling cutoff frequency with 0.90 headroom */
        float ratio = (float)r->ratio_num / (float)r->ratio_den;
        float base_fc = 0.5f / ratio;
        if (base_fc > 0.5f) {
            base_fc = 0.5f;
        }
        r->fc = base_fc * 0.90f;
    }

    r->history_len = 0;
    return 0;
}

void snt_resampler_reset(snt_resampler_t *r)
{
    if (!r) return;
    r->phase_num = 0;
    r->history_len = 0;
    memset(r->history, 0, sizeof(r->history));
}

static inline float get_sample(const snt_resampler_t *r,
                               const float *in,
                               size_t idx)
{
    if (idx < r->history_len) {
        return r->history[idx];
    }
    return in[idx - r->history_len];
}

int snt_resampler_process(snt_resampler_t *r,
                          const float *in, size_t in_count,
                          int16_t *out, size_t out_cap,
                          size_t *out_written)
{
    if (!r || !out || !out_written) {
        return -1;
    }
    *out_written = 0;

    /* 1:1 Passthrough optimization */
    if (r->is_passthrough) {
        size_t count = in_count < out_cap ? in_count : out_cap;
        for (size_t i = 0; i < count; i++) {
            out[i] = snt_float_to_pcm16(in[i]);
        }
        *out_written = count;
        return 0;
    }

    if (!in || in_count == 0) {
        return 0;
    }

    const float W = (float)SNT_RESAMPLER_HALF_WIDTH;
    const float fc = r->fc;
    const float two_pi_fc = (float)(2.0 * M_PI * fc);
    const float pi_over_w = (float)(M_PI / W);

    size_t total_in = r->history_len + in_count;
    uint32_t den = r->ratio_den;
    uint32_t num = r->ratio_num;
    uint64_t phase = r->phase_num;
    size_t written = 0;

    while (written < out_cap) {
        float tm = (float)phase / (float)den;
        if (tm + W >= (float)total_in) {
            /* Need more future input samples for the filter support window */
            break;
        }

        int k_min = (int)ceilf(tm - W);
        int k_max = (int)floorf(tm + W);
        if (k_min < 0) k_min = 0;
        if (k_max >= (int)total_in) k_max = (int)total_in - 1;

        float acc = 0.0f;
        for (int k = k_min; k <= k_max; k++) {
            float tau = tm - (float)k;
            float abs_tau = fabsf(tau);
            if (abs_tau < W) {
                float w = 0.5f * (1.0f + cosf(pi_over_w * tau));
                float sinc_val;
                if (abs_tau < 1e-6f) {
                    sinc_val = 2.0f * fc;
                } else {
                    sinc_val = sinf(two_pi_fc * tau) / ((float)M_PI * tau);
                }
                acc += get_sample(r, in, (size_t)k) * sinc_val * w;
            }
        }

        out[written++] = snt_float_to_pcm16(acc);
        phase += num;
    }

    *out_written = written;

    /* Save history for next chunk */
    size_t keep_count = (size_t)(2 * SNT_RESAMPLER_HALF_WIDTH);
    size_t keep_start = (total_in > keep_count) ? (total_in - keep_count) : 0;
    size_t new_hist_len = total_in - keep_start;

    float new_history[SNT_RESAMPLER_HIST_MAX];
    for (size_t i = 0; i < new_hist_len; i++) {
        new_history[i] = get_sample(r, in, keep_start + i);
    }

    memcpy(r->history, new_history, new_hist_len * sizeof(float));
    r->history_len = new_hist_len;
    r->phase_num = phase - ((uint64_t)keep_start * den);

    return 0;
}

int snt_resampler_flush(snt_resampler_t *r,
                        int16_t *out, size_t out_cap,
                        size_t *out_written)
{
    if (!r || !out || !out_written) {
        return -1;
    }
    *out_written = 0;

    if (r->is_passthrough || r->history_len == 0) {
        return 0;
    }

    /* Feed zeros to run out the delay line */
    float zeros[SNT_RESAMPLER_HIST_MAX] = {0};
    return snt_resampler_process(r, zeros, SNT_RESAMPLER_HALF_WIDTH + 1, out, out_cap, out_written);
}
