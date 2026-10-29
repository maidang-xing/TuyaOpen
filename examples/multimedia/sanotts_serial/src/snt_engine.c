/* snt_engine.c -- sanoTTS inference engine wrapper.
 *
 * Implements the snt_contract.h engine interface for:
 *   - SNT_VOICE_EN_NANO: 24,000 Hz INT8 duration + acoustic + TinyVocos (Task C1)
 *   - SNT_VOICE_ZH:      22,050 Hz FP32 front + contextual chunked decoder (Task C2)
 */

#include "snt_engine.h"
#include "snt_nano.h"
#include "snt_port.h"
#include "snt_front_f32.h"
#include "snt_piperlite.h"
#include "snt_chinese_chunk.h"
#include "snt_assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "tuya_cloud_types.h"
#if defined(BEKEN_PLATFORM) || defined(__arm__) || (defined(OPERATING_SYSTEM) && OPERATING_SYSTEM != SYSTEM_LINUX)
#include "tal_api.h"
#define SNT_ARENA_ALLOC(sz) tal_psram_malloc(sz)
#define SNT_ARENA_FREE(p)   tal_psram_free(p)
#else
#define SNT_ARENA_ALLOC(sz) malloc(sz)
#define SNT_ARENA_FREE(p)   free(p)
#define PR_NOTICE(...) printf(__VA_ARGS__); printf("\n")
#define PR_ERR(...)    fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n")
#endif

#define ARENA_ALIGNMENT 16
#define EN_NANO_ARENA_BYTES (512 * 1024)
#define ZH_ARENA_BYTES      (3600 * 1024)

/* Arena structure preserving raw allocation pointer for leak-free deallocation */
typedef struct {
    void *raw_ptr;
    void *aligned_ptr;
    size_t size;
    size_t peak;
} snt_aligned_arena_t;

/* Engine singleton state */
static bool s_initialized = false;
static snt_voice_t s_active_voice = SNT_VOICE_EN_NANO;
static volatile int s_cancel_requested = 0;

static const void *s_front_blob = NULL;
static size_t s_front_size = 0;
static bool s_front_blob_owned = false;

static const void *s_dec_blob = NULL;
static size_t s_dec_size = 0;
static bool s_dec_blob_owned = false;

static snt_aligned_arena_t s_arena = {0};

/* Chinese front and decoder model structs */
static snt_front_model s_fm;
static snt_piperlite_model s_pm;
static bool s_zh_models_loaded = false;

/* Test hooks */
static char s_custom_model_dir[512] = {0};
static uint64_t s_test_seed = 0;
static bool s_has_test_seed = false;
static const int32_t *s_dur_override = NULL;
static size_t s_dur_override_n = 0;
static snt_chunk_config_t s_chunk_cfg = {
    .center_frames = SNT_CHUNK_DEFAULT_CENTER_FRAMES,
    .context_frames = SNT_CHUNK_DEFAULT_CONTEXT_FRAMES,
};

/* ---- Arena Helpers ---------------------------------------------------- */

static int arena_init(snt_aligned_arena_t *a, size_t size) {
    if (a->raw_ptr) {
        if (a->size >= size) {
            a->peak = 0;
            return 0;
        }
        if (a->raw_ptr) {
            SNT_ARENA_FREE(a->raw_ptr);
            a->raw_ptr = NULL;
            a->aligned_ptr = NULL;
        }
    }
    size_t alloc_sz = size + ARENA_ALIGNMENT;
    a->raw_ptr = SNT_ARENA_ALLOC(alloc_sz);
    if (!a->raw_ptr) return -1;

    uintptr_t addr = (uintptr_t)a->raw_ptr;
    uintptr_t aligned = (addr + (ARENA_ALIGNMENT - 1)) & ~(uintptr_t)(ARENA_ALIGNMENT - 1);
    a->aligned_ptr = (void *)aligned;
    a->size = size;
    a->peak = 0;
    return 0;
}

static void arena_free(snt_aligned_arena_t *a) {
    if (a && a->raw_ptr) {
        SNT_ARENA_FREE(a->raw_ptr);
        a->raw_ptr = NULL;
        a->aligned_ptr = NULL;
        a->size = 0;
        a->peak = 0;
    }
}

/* ---- Binary Helpers --------------------------------------------------- */

static int32_t rd_i32(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static int piperlite_blob_extents(const uint8_t *blob, size_t *meta_bytes,
                                  size_t *weight_floats) {
    if (!blob) return -1;
    int32_t n_tensors = rd_i32(blob + 44);
    if (n_tensors <= 0 || n_tensors > SNT_PIPERLITE_MAX_TENSORS) return -1;
    *meta_bytes = 48u + 8u * (size_t)n_tensors;
    int32_t last_off = rd_i32(blob + 48 + 8 * (n_tensors - 1));
    int32_t last_size = rd_i32(blob + 48 + 8 * (n_tensors - 1) + 4);
    if (last_off < 0 || last_size <= 0) return -1;
    *weight_floats = (size_t)last_off + (size_t)last_size;
    return 0;
}

static int front_blob_extents(const uint8_t *blob, size_t *meta_bytes,
                              size_t *weight_floats) {
    if (!blob) return -1;
    int32_t n_tensors = rd_i32(blob + 68);
    if (n_tensors <= 0 || n_tensors > SNT_FRONT_MAX_TENSORS) return -1;
    *meta_bytes = 72u + 8u * (size_t)n_tensors;
    int32_t last_off = rd_i32(blob + 72 + 8 * (n_tensors - 1));
    int32_t last_size = rd_i32(blob + 72 + 8 * (n_tensors - 1) + 4);
    if (last_off < 0 || last_size <= 0) return -1;
    *weight_floats = (size_t)last_off + (size_t)last_size;
    return 0;
}

static void *load_file(const char *path, size_t *bytes) {
    FILE *fh = fopen(path, "rb");
    if (!fh) return NULL;
    fseek(fh, 0, SEEK_END);
    long sz = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    if (sz <= 0) { fclose(fh); return NULL; }
    void *buf = malloc((size_t)sz);
    if (!buf) { fclose(fh); return NULL; }
    if (fread(buf, 1, (size_t)sz, fh) != (size_t)sz) {
        free(buf);
        fclose(fh);
        return NULL;
    }
    fclose(fh);
    if (bytes) *bytes = (size_t)sz;
    return buf;
}

static void *try_load_model_file(const char *subpath, size_t *bytes) {
    char fullpath[1024];

    if (s_custom_model_dir[0] != '\0') {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", s_custom_model_dir, subpath);
        void *p = load_file(fullpath, bytes);
        if (p) return p;
    }

    const char *env_dir = getenv("SNT_MODEL_DIR");
    if (env_dir && env_dir[0] != '\0') {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", env_dir, subpath);
        void *p = load_file(fullpath, bytes);
        if (p) return p;
    }

    static const char *prefixes[] = {
        "examples/multimedia/sanotts_serial/models",
        "models",
        "../models",
        "../../models",
        "../../../models"
    };
    for (size_t i = 0; i < sizeof(prefixes)/sizeof(prefixes[0]); i++) {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", prefixes[i], subpath);
        void *p = load_file(fullpath, bytes);
        if (p) return p;
    }

    return NULL;
}

/* ---- Public Config Hooks ---------------------------------------------- */

void snt_engine_set_model_dir(const char *dir) {
    if (dir) {
        strncpy(s_custom_model_dir, dir, sizeof(s_custom_model_dir) - 1);
        s_custom_model_dir[sizeof(s_custom_model_dir) - 1] = '\0';
    } else {
        s_custom_model_dir[0] = '\0';
    }
}

void snt_engine_set_model_blobs(snt_voice_t voice,
                                const void *front_blob, size_t front_size,
                                const void *dec_blob, size_t dec_size) {
    if (s_front_blob_owned && s_front_blob) {
        free((void *)s_front_blob);
    }
    if (s_dec_blob_owned && s_dec_blob) {
        free((void *)s_dec_blob);
    }
    s_front_blob = front_blob;
    s_front_size = front_size;
    s_front_blob_owned = false;

    s_dec_blob = dec_blob;
    s_dec_size = dec_size;
    s_dec_blob_owned = false;
    s_active_voice = voice;
}

void snt_engine_set_test_seed(uint64_t seed) {
    s_test_seed = seed;
    s_has_test_seed = true;
}

void snt_engine_set_dur_override(const int32_t *durs, size_t n_durs) {
    s_dur_override = durs;
    s_dur_override_n = n_durs;
}

void snt_engine_set_chunk_config(const snt_chunk_config_t *cfg) {
    if (cfg) {
        s_chunk_cfg = *cfg;
    } else {
        snt_chunk_config_default(&s_chunk_cfg);
    }
}

int snt_engine_preallocate_arena(snt_voice_t voice) {
    size_t sz = (voice == SNT_VOICE_ZH) ? ZH_ARENA_BYTES : EN_NANO_ARENA_BYTES;
    return arena_init(&s_arena, sz);
}

void *snt_engine_get_arena(size_t *size) {
    if (size) *size = s_arena.size;
    return s_arena.aligned_ptr;
}

size_t snt_engine_get_arena_peak(void) {
    return s_arena.peak;
}

size_t snt_engine_get_arena_size(void) {
    return s_arena.size;
}

uint32_t snt_engine_get_sample_rate(void) {
    return (s_active_voice == SNT_VOICE_ZH) ? 22050 : 24000;
}

snt_voice_t snt_engine_get_active_voice(void) {
    return s_active_voice;
}

void snt_engine_request_cancel(void) {
    s_cancel_requested = 1;
}

/* ---- Engine Lifecycle ------------------------------------------------- */

int snt_engine_init(snt_voice_t voice) {
    s_cancel_requested = 0;

    if (s_initialized && s_active_voice == voice) {
        return SNT_OK;
    }

    if (s_initialized && s_active_voice != voice) {
        snt_engine_deinit();
    }

    s_active_voice = voice;

    /* Load blobs if not set externally */
    if (!s_front_blob || !s_dec_blob) {
        if (voice == SNT_VOICE_EN_NANO) {
            s_front_blob = try_load_model_file("en_us_e12nano/front_q8.bin", &s_front_size);
            s_dec_blob = try_load_model_file("en_us_e12nano/model_q8.bin", &s_dec_size);
            s_front_blob_owned = true;
            s_dec_blob_owned = true;
        } else if (voice == SNT_VOICE_ZH) {
            size_t f_sz = 0, d_sz = 0;
            const void *f_data = snt_assets_get("front_f32.bin", &f_sz);
            const void *d_data = snt_assets_get("dec_f32.bin", &d_sz);
            if (f_data && d_data) {
                s_front_blob = f_data;
                s_front_size = f_sz;
                s_dec_blob = d_data;
                s_dec_size = d_sz;
                s_front_blob_owned = false;
                s_dec_blob_owned = false;
            } else {
                s_front_blob = try_load_model_file("chinese/front_f32.bin", &s_front_size);
                s_dec_blob = try_load_model_file("chinese/dec_f32.bin", &s_dec_size);
                s_front_blob_owned = true;
                s_dec_blob_owned = true;
            }
        } else {
            return SNT_EUNSUPPORTED;
        }
    }

    if (!s_front_blob || !s_dec_blob) {
        return SNT_EIO;
    }

    /* Initialize voice-specific structures and arena */
    if (voice == SNT_VOICE_EN_NANO) {
        if (arena_init(&s_arena, EN_NANO_ARENA_BYTES) != 0) {
            return SNT_ENOMEM;
        }
    } else if (voice == SNT_VOICE_ZH) {
        size_t front_meta_sz = 0, front_weight_floats = 0;
        size_t dec_meta_sz = 0, dec_weight_floats = 0;
        int r1 = front_blob_extents((const uint8_t *)s_front_blob, &front_meta_sz, &front_weight_floats);
        int r2 = piperlite_blob_extents((const uint8_t *)s_dec_blob, &dec_meta_sz, &dec_weight_floats);
        if (r1 != 0 || r2 != 0) {
            PR_ERR("snt_engine: blob_extents failed: r1=%d r2=%d f_blob=%p d_blob=%p",
                   r1, r2, s_front_blob, s_dec_blob);
            return SNT_EINVAL;
        }

        const float *front_weights = (const float *)((const uint8_t *)s_front_blob + front_meta_sz);
        const float *dec_weights = (const float *)((const uint8_t *)s_dec_blob + dec_meta_sz);

        int r3 = snt_front_init(&s_fm, s_front_blob, front_meta_sz, front_weights, front_weight_floats);
        if (r3 != 0) {
            PR_ERR("snt_engine: snt_front_init failed: %d", r3);
            return SNT_EINVAL;
        }
        int r4 = snt_piperlite_init(&s_pm, s_dec_blob, dec_meta_sz, dec_weights, dec_weight_floats);
        if (r4 != 0) {
            PR_ERR("snt_engine: snt_piperlite_init failed: %d", r4);
            return SNT_EINVAL;
        }
        s_zh_models_loaded = true;

        if (arena_init(&s_arena, ZH_ARENA_BYTES) != 0) {
            PR_ERR("snt_engine: arena_init failed for %u bytes", (unsigned)ZH_ARENA_BYTES);
            return SNT_ENOMEM;
        }
        PR_NOTICE("snt_engine: Chinese models initialized successfully (arena %u B)", (unsigned)ZH_ARENA_BYTES);
    } else {
        return SNT_EUNSUPPORTED;
    }

    s_initialized = true;
    return SNT_OK;
}

void snt_engine_deinit(void) {
    s_cancel_requested = 1;

    arena_free(&s_arena);

    if (s_front_blob_owned && s_front_blob) {
        free((void *)s_front_blob);
    }
    s_front_blob = NULL;
    s_front_size = 0;
    s_front_blob_owned = false;

    if (s_dec_blob_owned && s_dec_blob) {
        free((void *)s_dec_blob);
    }
    s_dec_blob = NULL;
    s_dec_size = 0;
    s_dec_blob_owned = false;

    s_zh_models_loaded = false;
    s_initialized = false;
    s_has_test_seed = false;
    s_dur_override = NULL;
    s_dur_override_n = 0;
}

/* ---- Synthesis Adapters ----------------------------------------------- */

typedef struct {
    snt_float_sink_t sink;
    void *user;
    uint32_t rate;
    int64_t t0;
    int64_t first_pcm_us;
    bool first_recorded;
    int64_t sink_blocked_us;
    size_t total_samples;
    const volatile int *cancel_flag;
} NanoSinkAdapter;

static int nano_pcm_adapter_cb(const float *pcm, int n, void *user) {
    NanoSinkAdapter *ad = (NanoSinkAdapter *)user;
    if (ad->cancel_flag && *(ad->cancel_flag)) {
        return -1;
    }

    if (!ad->first_recorded) {
        int64_t now = snt_now_us();
        ad->first_pcm_us = (now >= ad->t0) ? (now - ad->t0) : 0;
        ad->first_recorded = true;
    }

    int64_t t_sink0 = snt_now_us();
    int rc = ad->sink(pcm, (size_t)n, ad->rate, ad->user);
    int64_t t_sink1 = snt_now_us();

    if (t_sink1 >= t_sink0) {
        ad->sink_blocked_us += (t_sink1 - t_sink0);
    }

    if (rc != 0) {
        return -1;
    }

    ad->total_samples += (size_t)n;
    return 0;
}

/* ---- Synthesize Entry Point ------------------------------------------- */

int snt_engine_synthesize(const int32_t *ids, size_t n_ids,
                          snt_float_sink_t sink, void *user,
                          snt_metrics_t *metrics) {
    if (!s_initialized) {
        return SNT_EINVAL;
    }
    if (!ids || n_ids == 0 || !sink) {
        return SNT_EINVAL;
    }

    s_cancel_requested = 0;
    int64_t t0 = snt_now_us();

    if (s_active_voice == SNT_VOICE_EN_NANO) {
        snt_nano_config cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.front_blob = s_front_blob;
        cfg.dec_blob = s_dec_blob;
        cfg.arena = s_arena.aligned_ptr;
        cfg.arena_size = s_arena.size;
        cfg.dur_override = s_dur_override;
        cfg.noise_seed = s_has_test_seed ? s_test_seed : 0;

        NanoSinkAdapter ad;
        memset(&ad, 0, sizeof(ad));
        ad.sink = sink;
        ad.user = user;
        ad.rate = 24000;
        ad.t0 = t0;
        ad.cancel_flag = &s_cancel_requested;

        snt_nano_stats stats;
        memset(&stats, 0, sizeof(stats));

        int rc = snt_nano_synthesize(&cfg, ids, (int)n_ids,
                                     nano_pcm_adapter_cb, &ad,
                                     &stats);

        int64_t t1 = snt_now_us();
        uint64_t wall_us = (t1 >= t0) ? (uint64_t)(t1 - t0) : 0;
        uint64_t compute_us = (wall_us >= (uint64_t)ad.sink_blocked_us) ?
                              (wall_us - (uint64_t)ad.sink_blocked_us) : wall_us;

        s_arena.peak = stats.arena_peak;

        if (metrics) {
            metrics->source_rate = 24000;
            metrics->samples = (uint32_t)ad.total_samples;
            metrics->compute_us = compute_us;
            metrics->first_pcm_us = (uint64_t)ad.first_pcm_us;
            metrics->arena_peak = stats.arena_peak;
        }

        if (s_cancel_requested) {
            return SNT_ECANCELLED;
        }
        if (rc != 0) {
            return SNT_EIO;
        }
        return SNT_OK;
    }

    if (s_active_voice == SNT_VOICE_ZH) {
        if (!s_zh_models_loaded) {
            return SNT_EINVAL;
        }

        if (s_cancel_requested) {
            return SNT_ECANCELLED;
        }

        /* 1. Duration calculation */
        int32_t dur_stack[256];
        int32_t *durations = (n_ids <= 256) ? dur_stack : (int32_t *)malloc(n_ids * sizeof(int32_t));
        if (!durations) return SNT_ENOMEM;

        long frames = 0;
        if (s_dur_override && s_dur_override_n >= n_ids) {
            memcpy(durations, s_dur_override, n_ids * sizeof(int32_t));
            for (size_t i = 0; i < n_ids; i++) frames += durations[i];
        } else {
            size_t dur_arena_n = snt_front_duration_arena_floats(&s_fm, (int)n_ids);
            float *dur_arena = (float *)s_arena.aligned_ptr;
            int32_t dur_ids_stack[256];
            int32_t *dur_ids = (n_ids <= 256) ? dur_ids_stack : (int32_t *)malloc(n_ids * sizeof(int32_t));
            if (!dur_ids) {
                if (durations != dur_stack) free(durations);
                return SNT_ENOMEM;
            }

            for (size_t i = 0; i < n_ids; i++) {
                dur_ids[i] = (ids[i] >= 0 && ids[i] < s_fm.d_vocab) ? ids[i] : (59 < s_fm.d_vocab ? 59 : 0);
            }
            frames = snt_front_durations(&s_fm, dur_ids, (int)n_ids, 1.0f,
                                        durations, dur_arena, dur_arena_n);
            if (dur_ids != dur_ids_stack) free(dur_ids);
        }

        PR_NOTICE("snt_engine: ZH durations calculated: %ld frames", frames);

        if (frames <= 0) {
            if (durations != dur_stack) free(durations);
            return SNT_EINVAL;
        }

        if (s_cancel_requested) {
            if (durations != dur_stack) free(durations);
            return SNT_ECANCELLED;
        }

        /* 2. Latent calculation:
         * Partition arena:
         *   latent buffer at start: in_ch * frames floats
         *   lat_arena follows latent buffer
         */
        size_t latent_n = (size_t)s_fm.a_out * (size_t)frames;
        size_t lat_arena_n = snt_front_latent_arena_floats(&s_fm, (int)n_ids, frames);
        float *latent = (float *)s_arena.aligned_ptr;
        float *lat_arena = latent + latent_n;
        size_t total_lat_needed = latent_n + lat_arena_n;

        if (total_lat_needed * sizeof(float) > s_arena.size) {
            if (durations != dur_stack) free(durations);
            return SNT_ENOMEM;
        }

        int32_t ac_ids_stack[256];
        int32_t *ac_ids = (n_ids <= 256) ? ac_ids_stack : (int32_t *)malloc(n_ids * sizeof(int32_t));
        if (!ac_ids) {
            if (durations != dur_stack) free(durations);
            return SNT_ENOMEM;
        }
        for (size_t i = 0; i < n_ids; i++) {
            ac_ids[i] = (ids[i] >= 0 && ids[i] < s_fm.a_vocab) ? ids[i] : (59 < s_fm.a_vocab ? 59 : 0);
        }

        int rc = snt_front_latent(&s_fm, ac_ids, durations, (int)n_ids, frames,
                                  latent, lat_arena, lat_arena_n);
        if (ac_ids != ac_ids_stack) free(ac_ids);
        if (durations != dur_stack) free(durations);

        PR_NOTICE("snt_engine: ZH latent calculated: rc=%d", rc);

        if (rc != 0) {
            return SNT_EIO;
        }

        if (s_cancel_requested) {
            return SNT_ECANCELLED;
        }

        /* 3. Chunked decoder synthesis:
         * Decoder arena uses space immediately following latent tensor.
         */
        float *dec_arena = latent + latent_n;
        size_t dec_arena_floats = (s_arena.size / sizeof(float)) - latent_n;

        PR_NOTICE("snt_engine: ZH starting chunked synthesis (frames=%ld, dec_arena_floats=%zu)...",
                  frames, dec_arena_floats);

        uint64_t first_pcm_us = 0;
        rc = snt_chinese_chunk_synthesize(&s_pm, latent, (int)frames,
                                          &s_chunk_cfg, sink, user,
                                          NULL, dec_arena, dec_arena_floats,
                                          &s_cancel_requested,
                                          &first_pcm_us);

        int64_t t1 = snt_now_us();
        uint64_t wall_us = (t1 >= t0) ? (uint64_t)(t1 - t0) : 0;

        size_t chunk_arena_needed = snt_chinese_chunk_arena_floats(&s_pm, &s_chunk_cfg);
        size_t peak_bytes = (latent_n + chunk_arena_needed) * sizeof(float);
        s_arena.peak = peak_bytes;

        if (metrics) {
            metrics->source_rate = 22050;
            metrics->samples = (uint32_t)(frames * SNT_PIPERLITE_HOP);
            metrics->compute_us = wall_us;
            metrics->first_pcm_us = first_pcm_us;
            metrics->arena_peak = peak_bytes;
        }

        if (s_cancel_requested || rc == SNT_ECANCELLED) {
            return SNT_ECANCELLED;
        }
        if (rc != 0) {
            return SNT_EIO;
        }
        return SNT_OK;
    }

    return SNT_EUNSUPPORTED;
}
