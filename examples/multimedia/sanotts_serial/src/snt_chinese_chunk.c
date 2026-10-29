/* snt_chinese_chunk.c -- Contextual chunked decoding for Chinese piperlite.
 *
 * Implements low-memory chunked neural audio synthesis for the Chinese FP32
 * piperlite decoder (Task C2).
 */

#include "snt_chinese_chunk.h"
#include "snt_port.h"

#if defined(BEKEN_PLATFORM) || defined(__arm__) || (defined(OPERATING_SYSTEM) && OPERATING_SYSTEM != SYSTEM_LINUX)
#include "tal_api.h"
#define CHUNK_LOG(fmt, ...) PR_NOTICE(fmt, ##__VA_ARGS__)
#else
#define CHUNK_LOG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

#include <string.h>
#include <stdio.h>

void snt_chunk_config_default(snt_chunk_config_t *cfg) {
    if (!cfg) return;
    cfg->center_frames = SNT_CHUNK_DEFAULT_CENTER_FRAMES;
    cfg->context_frames = SNT_CHUNK_DEFAULT_CONTEXT_FRAMES;
}

size_t snt_chinese_chunk_arena_floats(const snt_piperlite_model *m,
                                      const snt_chunk_config_t *cfg) {
    if (!m) return 0;
    snt_chunk_config_t c;
    if (cfg) {
        c = *cfg;
    } else {
        snt_chunk_config_default(&c);
    }
    if (c.center_frames <= 0) c.center_frames = SNT_CHUNK_DEFAULT_CENTER_FRAMES;
    if (c.context_frames < 0) c.context_frames = SNT_CHUNK_DEFAULT_CONTEXT_FRAMES;

    int max_chunk_frames = c.center_frames + 2 * c.context_frames;
    size_t sub_z_floats = (size_t)m->in_ch * (size_t)max_chunk_frames;
    size_t chunk_audio_floats = (size_t)max_chunk_frames * SNT_PIPERLITE_HOP;
    size_t dec_arena_floats = snt_piperlite_arena_floats(m, max_chunk_frames);

    return sub_z_floats + chunk_audio_floats + dec_arena_floats;
}

int snt_chinese_chunk_synthesize(const snt_piperlite_model *m,
                                 const float *latent,
                                 int total_frames,
                                 const snt_chunk_config_t *cfg,
                                 snt_float_sink_t sink,
                                 void *user,
                                 float *audio_out,
                                 float *arena,
                                 size_t arena_floats,
                                 const volatile int *cancel_flag,
                                 uint64_t *first_pcm_us) {
    if (!m || !latent || total_frames <= 0 || !arena) {
        return SNT_EINVAL;
    }

    snt_chunk_config_t c;
    if (cfg) {
        c = *cfg;
    } else {
        snt_chunk_config_default(&c);
    }
    if (c.center_frames <= 0) c.center_frames = SNT_CHUNK_DEFAULT_CENTER_FRAMES;
    if (c.context_frames < 0) c.context_frames = SNT_CHUNK_DEFAULT_CONTEXT_FRAMES;

    int max_chunk_frames = c.center_frames + 2 * c.context_frames;
    if (max_chunk_frames > total_frames + 2 * c.context_frames) {
        max_chunk_frames = total_frames + 2 * c.context_frames;
    }

    size_t sub_z_floats = (size_t)m->in_ch * (size_t)max_chunk_frames;
    size_t chunk_audio_floats = (size_t)max_chunk_frames * SNT_PIPERLITE_HOP;
    size_t dec_arena_floats = snt_piperlite_arena_floats(m, max_chunk_frames);
    size_t total_needed = sub_z_floats + chunk_audio_floats + dec_arena_floats;

    if (arena_floats < total_needed) {
        return SNT_ENOMEM;
    }

    float *sub_z = arena;
    float *chunk_audio = sub_z + sub_z_floats;
    float *dec_arena = chunk_audio + chunk_audio_floats;
    size_t dec_arena_avail = arena_floats - (sub_z_floats + chunk_audio_floats);

    int64_t t_start = snt_now_us();
    bool first_recorded = false;

    for (int f_start = 0; f_start < total_frames; f_start += c.center_frames) {
        if (cancel_flag && *cancel_flag) {
            return SNT_ECANCELLED;
        }

        int f_end = f_start + c.center_frames;
        if (f_end > total_frames) f_end = total_frames;
        int center_frames = f_end - f_start;

        int t0 = (f_start > c.context_frames) ? (f_start - c.context_frames) : 0;
        int t1 = (f_end + c.context_frames < total_frames) ? (f_end + c.context_frames) : total_frames;
        int chunk_frames = t1 - t0;

        /* Extract sub-latent [in_ch, chunk_frames] from channel-major latent */
        for (int ch = 0; ch < m->in_ch; ch++) {
            const float *src = latent + (size_t)ch * total_frames + t0;
            float *dst = sub_z + (size_t)ch * chunk_frames;
            memcpy(dst, src, (size_t)chunk_frames * sizeof(float));
        }

        CHUNK_LOG("snt_chunk: running [%d..%d]/%d (chunk_frames=%d)", f_start, f_end, total_frames, chunk_frames);
        int64_t t_chunk0 = snt_now_us();
        int rc = snt_piperlite_synthesize(m, sub_z, chunk_frames, chunk_audio,
                                         dec_arena, dec_arena_avail);
        int64_t t_chunk1 = snt_now_us();
        CHUNK_LOG("snt_chunk: synthesized chunk in %lld ms (rc=%d)", (long long)((t_chunk1 - t_chunk0) / 1000), rc);
        if (rc != 0) {
            return SNT_EIO;
        }

        if (cancel_flag && *cancel_flag) {
            return SNT_ECANCELLED;
        }

        int center_offset_frames = f_start - t0;
        size_t center_sample_offset = (size_t)center_offset_frames * SNT_PIPERLITE_HOP;
        size_t center_samples = (size_t)center_frames * SNT_PIPERLITE_HOP;
        const float *center_audio = chunk_audio + center_sample_offset;

        if (!first_recorded) {
            if (first_pcm_us) {
                int64_t now = snt_now_us();
                *first_pcm_us = (now >= t_start) ? (uint64_t)(now - t_start) : 0;
            }
            first_recorded = true;
        }

        if (audio_out) {
            memcpy(audio_out + (size_t)f_start * SNT_PIPERLITE_HOP,
                   center_audio,
                   center_samples * sizeof(float));
        }

        if (sink) {
            int sink_rc = sink(center_audio, center_samples, 22050, user);
            if (sink_rc != 0) {
                return SNT_ECANCELLED;
            }
        }
    }

    return SNT_OK;
}
