/**
 * @file snt_audio.c
 * @brief sanoTTS Audio Output Driver, Ring Buffer and Backpressure
 */

#include "snt_audio.h"
#include "snt_resampler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#if defined(SNT_HOST_TEST)

/* Host stub implementation for unit tests */
static bool s_host_aborted = false;
static uint32_t s_host_samples = 0;
static snt_resampler_t s_host_resampler;

int snt_audio_init(uint32_t output_rate)
{
    (void)output_rate;
    s_host_aborted = false;
    s_host_samples = 0;
    return 0;
}

int snt_audio_begin(uint32_t request_id, uint32_t source_rate)
{
    (void)request_id;
    s_host_aborted = false;
    s_host_samples = 0;
    snt_resampler_init(&s_host_resampler, source_rate, 16000);
    return 0;
}

int snt_audio_sink(const float *pcm, size_t n, uint32_t rate, void *user)
{
    (void)rate; (void)user;
    if (s_host_aborted) return SNT_ECANCELLED;
    int16_t out_buf[1024];
    size_t written = 0;
    size_t offset = 0;
    while (offset < n) {
        if (s_host_aborted) return SNT_ECANCELLED;
        size_t chunk = (n - offset > 256) ? 256 : (n - offset);
        snt_resampler_process(&s_host_resampler, pcm + offset, chunk, out_buf, 1024, &written);
        s_host_samples += written;
        offset += chunk;
    }
    return SNT_OK;
}

int snt_audio_finish(uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (s_host_aborted) return SNT_ECANCELLED;
    int16_t out_buf[128];
    size_t written = 0;
    snt_resampler_flush(&s_host_resampler, out_buf, 128, &written);
    s_host_samples += written;
    return SNT_OK;
}

void snt_audio_abort(void)
{
    s_host_aborted = true;
    snt_resampler_reset(&s_host_resampler);
}

bool snt_audio_is_aborted(void)
{
    return s_host_aborted;
}

int snt_audio_play_test_tone(uint32_t request_id)
{
    int ret = snt_audio_begin(request_id, 16000);
    if (ret != 0) return ret;
    float tone[320];
    for (int sec = 0; sec < 50; sec++) {
        if (s_host_aborted) return SNT_ECANCELLED;
        for (int i = 0; i < 320; i++) {
            int n = sec * 320 + i;
            tone[i] = 0.25f * sinf(2.0f * (float)M_PI * 440.0f * (float)n / 16000.0f);
        }
        ret = snt_audio_sink(tone, 320, 16000, NULL);
        if (ret != 0) return ret;
    }
    return snt_audio_finish(1000);
}

uint32_t snt_audio_get_samples_played(void) { return s_host_samples; }
uint32_t snt_audio_get_underruns(void) { return 0; }

#else

/* Target implementation using TuyaOpen OS and drivers */
#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tuya_ringbuf.h"
#include "tdl_audio_manage.h"

static TDL_AUDIO_HANDLE_T g_audio_handle = NULL;
static TDL_AUDIO_INFO_T   g_audio_info = {0};
static TUYA_RINGBUFF_T    g_pcm_ringbuf = NULL;
static MUTEX_HANDLE       g_audio_mutex = NULL;
static SEM_HANDLE         g_sem_data_ready = NULL;
static SEM_HANDLE         g_sem_space_ready = NULL;
static THREAD_HANDLE      g_play_thread = NULL;

static snt_resampler_t    g_resampler;
static volatile bool      g_audio_inited = false;
static volatile bool      g_audio_aborted = false;
static volatile bool      g_play_thread_running = false;
static volatile uint32_t  g_samples_played = 0;
static volatile uint32_t  g_underruns = 0;
static uint32_t           g_current_req_id = 0;

static void snt_audio_playback_task(void *arg)
{
    (void)arg;
    uint32_t frame_bytes = g_audio_info.frame_size ? g_audio_info.frame_size : (SNT_AUDIO_FRAME_SAMPLES * sizeof(int16_t));
    uint8_t *frame_buf = (uint8_t *)tal_malloc(frame_bytes);
    if (!frame_buf) {
        PR_ERR("snt_audio: failed to allocate frame buffer (%u bytes)", (unsigned)frame_bytes);
        return;
    }

    g_play_thread_running = true;

    while (g_play_thread_running) {
        if (g_audio_aborted) {
            tal_system_sleep(10);
            continue;
        }

        uint32_t used_bytes = 0;
        tal_mutex_lock(g_audio_mutex);
        if (g_pcm_ringbuf) {
            used_bytes = tuya_ring_buff_used_size_get(g_pcm_ringbuf);
        }
        tal_mutex_unlock(g_audio_mutex);

        if (used_bytes >= frame_bytes) {
            tal_mutex_lock(g_audio_mutex);
            uint32_t read_bytes = tuya_ring_buff_read(g_pcm_ringbuf, frame_buf, frame_bytes);
            tal_mutex_unlock(g_audio_mutex);

            if (read_bytes > 0) {
                tal_semaphore_post(g_sem_space_ready);

                if (g_audio_handle && !g_audio_aborted) {
                    tdl_audio_play(g_audio_handle, frame_buf, read_bytes);
                    g_samples_played += (read_bytes / sizeof(int16_t));
                }
            }
        } else if (used_bytes > 0 && g_audio_handle) {
            /* Partial frame (tail during drain) */
            memset(frame_buf, 0, frame_bytes);
            tal_mutex_lock(g_audio_mutex);
            uint32_t read_bytes = tuya_ring_buff_read(g_pcm_ringbuf, frame_buf, used_bytes);
            tal_mutex_unlock(g_audio_mutex);

            if (read_bytes > 0) {
                tal_semaphore_post(g_sem_space_ready);
                if (!g_audio_aborted) {
                    tdl_audio_play(g_audio_handle, frame_buf, frame_bytes);
                    g_samples_played += (read_bytes / sizeof(int16_t));
                }
            }
        } else {
            /* Ring buffer is empty, wait for producer or timeout */
            tal_semaphore_wait(g_sem_data_ready, 20);
        }
    }

    if (frame_buf) {
        tal_free(frame_buf);
    }
}

int snt_audio_init(uint32_t output_rate)
{
    if (g_audio_inited) {
        return SNT_OK;
    }

    OPERATE_RET rt = OPRT_OK;

#if defined(AUDIO_CODEC_NAME)
    rt = tdl_audio_find(AUDIO_CODEC_NAME, &g_audio_handle);
    if (rt != OPRT_OK) {
        PR_ERR("snt_audio: tdl_audio_find(%s) failed: %d", AUDIO_CODEC_NAME, rt);
        return SNT_EIO;
    }

    rt = tdl_audio_open(g_audio_handle, NULL);
    if (rt != OPRT_OK) {
        PR_ERR("snt_audio: tdl_audio_open failed: %d", rt);
        return SNT_EIO;
    }

    rt = tdl_audio_get_info(g_audio_handle, &g_audio_info);
    if (rt != OPRT_OK) {
        PR_ERR("snt_audio: tdl_audio_get_info failed: %d", rt);
        return SNT_EIO;
    }

    tdl_audio_volume_set(g_audio_handle, 75);
#endif

    rt = tuya_ring_buff_create(SNT_AUDIO_RINGBUF_SIZE, OVERFLOW_PSRAM_STOP_TYPE, &g_pcm_ringbuf);
    if (rt != OPRT_OK) {
        PR_ERR("snt_audio: tuya_ring_buff_create failed: %d", rt);
        return SNT_ENOMEM;
    }

    rt = tal_mutex_create_init(&g_audio_mutex);
    if (rt != OPRT_OK) return SNT_ENOMEM;

    rt = tal_semaphore_create_init(&g_sem_data_ready, 0, 64);
    if (rt != OPRT_OK) return SNT_ENOMEM;

    rt = tal_semaphore_create_init(&g_sem_space_ready, 0, 64);
    if (rt != OPRT_OK) return SNT_ENOMEM;

    snt_resampler_init(&g_resampler, output_rate, output_rate);

    THREAD_CFG_T thrd_param = {0};
    thrd_param.stackDepth = 1024 * 4;
    thrd_param.priority = THREAD_PRIO_2;
    thrd_param.thrdname = "snt_play";
    rt = tal_thread_create_and_start(&g_play_thread, NULL, NULL, snt_audio_playback_task, NULL, &thrd_param);
    if (rt != OPRT_OK) {
        PR_ERR("snt_audio: tal_thread_create_and_start failed: %d", rt);
        return SNT_ENOMEM;
    }

    g_audio_inited = true;
    PR_NOTICE("snt_audio: initialized successfully (out_rate=%u, ringbuf=%u)",
              (unsigned)output_rate, SNT_AUDIO_RINGBUF_SIZE);
    return SNT_OK;
}

int snt_audio_begin(uint32_t request_id, uint32_t source_rate)
{
    if (!g_audio_inited) {
        int init_ret = snt_audio_init(16000);
        if (init_ret != SNT_OK) return init_ret;
    }

    g_current_req_id = request_id;
    g_audio_aborted = false;
    g_samples_played = 0;
    g_underruns = 0;

    tal_mutex_lock(g_audio_mutex);
    if (g_pcm_ringbuf) {
        tuya_ring_buff_reset(g_pcm_ringbuf);
    }
    tal_mutex_unlock(g_audio_mutex);

    snt_resampler_init(&g_resampler, source_rate, 16000);
    return SNT_OK;
}

int snt_audio_sink(const float *pcm, size_t n, uint32_t rate, void *user)
{
    (void)rate;
    (void)user;

    if (!pcm || n == 0) {
        return SNT_OK;
    }
    if (g_audio_aborted) {
        return SNT_ECANCELLED;
    }

    int16_t out_chunk[512];
    size_t in_offset = 0;

    while (in_offset < n) {
        if (g_audio_aborted) {
            return SNT_ECANCELLED;
        }

        size_t to_process = (n - in_offset > 256) ? 256 : (n - in_offset);
        size_t written = 0;

        int ret = snt_resampler_process(&g_resampler,
                                        pcm + in_offset, to_process,
                                        out_chunk, 512,
                                        &written);
        if (ret != 0) {
            return SNT_EINVAL;
        }

        in_offset += to_process;

        if (written > 0) {
            uint32_t bytes_to_write = (uint32_t)(written * sizeof(int16_t));
            uint32_t wait_start = (uint32_t)tal_system_get_millisecond();

            while (1) {
                if (g_audio_aborted) {
                    return SNT_ECANCELLED;
                }

                tal_mutex_lock(g_audio_mutex);
                uint32_t free_bytes = tuya_ring_buff_free_size_get(g_pcm_ringbuf);
                if (free_bytes >= bytes_to_write) {
                    tuya_ring_buff_write(g_pcm_ringbuf, (uint8_t *)out_chunk, bytes_to_write);
                    tal_mutex_unlock(g_audio_mutex);
                    tal_semaphore_post(g_sem_data_ready);
                    break;
                }
                tal_mutex_unlock(g_audio_mutex);

                /* Backpressure: ring buffer full, wait for playback task to consume */
                tal_semaphore_wait(g_sem_space_ready, 20);

                uint32_t now = (uint32_t)tal_system_get_millisecond();
                if (now - wait_start > 5000) {
                    PR_ERR("snt_audio: backpressure timeout (%u ms)", (unsigned)(now - wait_start));
                    return SNT_ETIMEOUT;
                }
            }
        }
    }

    return SNT_OK;
}

int snt_audio_finish(uint32_t timeout_ms)
{
    if (g_audio_aborted) {
        return SNT_ECANCELLED;
    }

    /* Flush resampler filter tail */
    int16_t flush_buf[128];
    size_t flushed = 0;
    snt_resampler_flush(&g_resampler, flush_buf, 128, &flushed);
    if (flushed > 0) {
        uint32_t bytes_to_write = (uint32_t)(flushed * sizeof(int16_t));
        tal_mutex_lock(g_audio_mutex);
        uint32_t free_bytes = tuya_ring_buff_free_size_get(g_pcm_ringbuf);
        if (free_bytes >= bytes_to_write) {
            tuya_ring_buff_write(g_pcm_ringbuf, (uint8_t *)flush_buf, bytes_to_write);
            tal_mutex_unlock(g_audio_mutex);
            tal_semaphore_post(g_sem_data_ready);
        } else {
            tal_mutex_unlock(g_audio_mutex);
        }
    }

    uint32_t start_ms = (uint32_t)tal_system_get_millisecond();

    /* 1. Wait for 32KB ring buffer to become completely empty */
    while (1) {
        if (g_audio_aborted) {
            return SNT_ECANCELLED;
        }

        tal_mutex_lock(g_audio_mutex);
        uint32_t used = tuya_ring_buff_used_size_get(g_pcm_ringbuf);
        tal_mutex_unlock(g_audio_mutex);

        if (used == 0) {
            break;
        }

        tal_system_sleep(10);

        uint32_t now = (uint32_t)tal_system_get_millisecond();
        if (now - start_ms >= timeout_ms) {
            PR_WARN("snt_audio: finish timeout waiting for ringbuffer drain");
            return SNT_ETIMEOUT;
        }
    }

    /* 2. Wait for hardware speaker FIFO to drain (~160ms for T5AI voice FIFO) */
    tal_system_sleep(160);

    return SNT_OK;
}

void snt_audio_abort(void)
{
    g_audio_aborted = true;

    tal_mutex_lock(g_audio_mutex);
    if (g_pcm_ringbuf) {
        tuya_ring_buff_reset(g_pcm_ringbuf);
    }
    tal_mutex_unlock(g_audio_mutex);

#if defined(AUDIO_CODEC_NAME)
    if (g_audio_handle) {
        tdl_audio_play_stop(g_audio_handle);
    }
#endif

    snt_resampler_reset(&g_resampler);

    /* Unblock any waiting threads */
    tal_semaphore_post(g_sem_data_ready);
    tal_semaphore_post(g_sem_space_ready);
}

bool snt_audio_is_aborted(void)
{
    return g_audio_aborted;
}

int snt_audio_play_test_tone(uint32_t request_id)
{
    int ret = snt_audio_begin(request_id, 16000);
    if (ret != SNT_OK) {
        return ret;
    }

    /* Generate 1 second (16000 samples) of 440 Hz sine wave, amplitude 0.25 (-12 dBFS) */
    float tone[320];
    const float two_pi_f = (float)(2.0 * M_PI * 440.0 / 16000.0);

    for (int sec = 0; sec < 50; sec++) { /* 50 chunks * 320 samples = 16000 samples = 1.0s */
        if (g_audio_aborted) {
            return SNT_ECANCELLED;
        }

        for (int i = 0; i < 320; i++) {
            int n = sec * 320 + i;
            tone[i] = 0.25f * sinf(two_pi_f * (float)n);
        }

        ret = snt_audio_sink(tone, 320, 16000, NULL);
        if (ret != SNT_OK) {
            return ret;
        }
    }

    return snt_audio_finish(2000);
}

uint32_t snt_audio_get_samples_played(void)
{
    return g_samples_played;
}

uint32_t snt_audio_get_underruns(void)
{
    return g_underruns;
}

#endif
