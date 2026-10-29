/**
 * @file tuya_app_main.c
 * @brief sanoTTS Serial Application Entry Point for TUYA_T5AI_CORE
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tkl_output.h"
#include "tkl_memory.h"
#include "board_com_api.h"
#include "snt_contract.h"
#include "snt_serial.h"
#include "snt_audio.h"
#include "snt_engine.h"
#include "snt_model_en_nano.h"
#include "snt_fixture_en.h"
#include "snt_g2p.h"
#include "snt_text.h"
#include "snt_assets.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define SNT_APP_UART_NUM TUYA_UART_NUM_0

static snt_serial_t  g_serial;
static MUTEX_HANDLE  g_uart_tx_mutex = NULL;

#define SNT_REQ_RING_CAP 4
static snt_request_t g_req_ring[SNT_REQ_RING_CAP];
static size_t        g_ring_head = 0;
static size_t        g_ring_tail = 0;
static MUTEX_HANDLE  g_ring_mutex = NULL;
static SEM_HANDLE    g_ring_sem = NULL;

static THREAD_HANDLE g_rx_thread = NULL;
static THREAD_HANDLE g_worker_thread = NULL;

#if OPERATING_SYSTEM != SYSTEM_LINUX
static THREAD_HANDLE g_app_thread = NULL;
#endif

typedef struct {
    uint32_t request_id;
    uint32_t source_rate;
    size_t sample_offset;
    double sum_ref;
    double sum_cand;
    double sum_sq_ref;
    double sum_sq_cand;
    double sum_prod;
    double max_err;
} m1_eval_ctx_t;

static int m1_eval_sink(const float *pcm, size_t n, uint32_t rate, void *user)
{
    m1_eval_ctx_t *ctx = (m1_eval_ctx_t *)user;

    /* Feed audio pipeline so sound plays out of the speaker */
    int audio_ret = snt_audio_sink(pcm, n, rate, NULL);
    if (audio_ret != SNT_OK) {
        return audio_ret;
    }

    /* Accumulate Pearson statistics against golden reference snt_r00_audio */
    for (size_t i = 0; i < n; i++) {
        size_t idx = ctx->sample_offset + i;
        double ref = (snt_r00_audio && idx < SNT_R00_N_SAMPLES) ? (double)snt_r00_audio[idx] : 0.0;
        double cand = (double)pcm[i];

        ctx->sum_ref += ref;
        ctx->sum_cand += cand;
        ctx->sum_sq_ref += ref * ref;
        ctx->sum_sq_cand += cand * cand;
        ctx->sum_prod += ref * cand;

        double err = fabs(cand - ref);
        if (err > ctx->max_err) {
            ctx->max_err = err;
        }
    }
    ctx->sample_offset += n;
    return SNT_OK;
}

static void snt_app_tx_callback(const char *data, size_t len, void *user)
{
    (void)user;
    if (g_uart_tx_mutex) {
        tal_mutex_lock(g_uart_tx_mutex);
    }
    tal_uart_write(SNT_APP_UART_NUM, (const uint8_t *)data, (uint16_t)len);
    if (g_uart_tx_mutex) {
        tal_mutex_unlock(g_uart_tx_mutex);
    }
}

static void snt_app_exec_callback(const snt_request_t *req, void *user)
{
    (void)user;
    if (!req) return;

    if (g_ring_mutex) {
        tal_mutex_lock(g_ring_mutex);
    }
    if ((g_ring_head + 1) % SNT_REQ_RING_CAP != g_ring_tail) {
        g_req_ring[g_ring_head] = *req;
        g_ring_head = (g_ring_head + 1) % SNT_REQ_RING_CAP;
        if (g_ring_sem) {
            tal_semaphore_post(g_ring_sem);
        }
    } else {
        PR_ERR("snt_app: request ring overflow, dropping req %u", (unsigned)req->id);
    }
    if (g_ring_mutex) {
        tal_mutex_unlock(g_ring_mutex);
    }
}

static void snt_app_cancel_callback(void *user)
{
    (void)user;
    snt_audio_abort();
    snt_engine_request_cancel();
    snt_g2p_request_cancel();

    /* Flush unstarted requests from ring */
    if (g_ring_mutex) {
        tal_mutex_lock(g_ring_mutex);
        g_ring_tail = g_ring_head;
        tal_mutex_unlock(g_ring_mutex);
    }
}

static void snt_worker_task(void *arg)
{
    (void)arg;
    PR_NOTICE("snt_worker_task started");

    while (1) {
        tal_semaphore_wait(g_ring_sem, SEM_WAIT_FOREVER);

        snt_request_t req;
        bool have_req = false;

        if (g_ring_mutex) {
            tal_mutex_lock(g_ring_mutex);
        }
        if (g_ring_tail != g_ring_head) {
            req = g_req_ring[g_ring_tail];
            g_ring_tail = (g_ring_tail + 1) % SNT_REQ_RING_CAP;
            have_req = true;
        }
        if (g_ring_mutex) {
            tal_mutex_unlock(g_ring_mutex);
        }

        if (!have_req) {
            continue;
        }

        /* Check if request was cancelled while waiting in ring */
        if (snt_serial_is_active_cancelled(&g_serial, req.id)) {
            PR_NOTICE("Worker dropping cancelled req %u", (unsigned)req.id);
            continue;
        }

        uint32_t id = req.id;
        snt_cmd_type_t type = req.type;
        uint32_t t0 = (uint32_t)tal_system_get_millisecond();

        if (type == SNT_CMD_GOLDEN) {
            if (req.text_len == 0 || strcmp(req.text, "440") == 0) {
                /* M0 Hardware test tone */
                snt_serial_notify_synthesizing(&g_serial, id);
                snt_serial_notify_playing(&g_serial, id);

                int play_ret = snt_audio_play_test_tone(id);

                uint32_t t1 = (uint32_t)tal_system_get_millisecond();
                uint64_t wall_us = (uint64_t)(t1 - t0) * 1000ULL;
                uint64_t compute_us = 10000ULL;

                if (play_ret == SNT_ECANCELLED || snt_serial_is_active_cancelled(&g_serial, id)) {
                    PR_NOTICE("Worker: req %u cancelled", (unsigned)id);
                } else if (play_ret != SNT_OK) {
                    snt_serial_notify_error(&g_serial, id, play_ret, "audio playback failed");
                } else {
                    uint32_t samples = snt_audio_get_samples_played();
                    uint32_t underruns = snt_audio_get_underruns();
                    snt_serial_notify_done(&g_serial, id, samples, 16000, compute_us, wall_us, underruns);
                }
            } else {
                /* M1 English nano golden benchmark (r00 fixture) */
                snt_engine_set_test_seed(SNT_R00_SEED);
                snt_engine_set_dur_override(snt_r00_durs, SNT_R00_N_TOKENS);

                int begin_ret = snt_audio_begin(id, 24000);
                if (begin_ret != SNT_OK) {
                    snt_serial_notify_error(&g_serial, id, begin_ret, "audio begin failed");
                    continue;
                }

                snt_serial_notify_synthesizing(&g_serial, id);
                snt_serial_notify_playing(&g_serial, id);

                m1_eval_ctx_t eval_ctx;
                memset(&eval_ctx, 0, sizeof(eval_ctx));
                eval_ctx.request_id = id;
                eval_ctx.source_rate = 24000;

                snt_metrics_t metrics;
                memset(&metrics, 0, sizeof(metrics));

                int synth_ret = snt_engine_synthesize(snt_r00_ids, SNT_R00_N_TOKENS,
                                                      m1_eval_sink, &eval_ctx, &metrics);

                snt_engine_set_dur_override(NULL, 0);

                int finish_ret = snt_audio_finish(10000);

                uint32_t t1 = (uint32_t)tal_system_get_millisecond();
                uint64_t wall_us = (uint64_t)(t1 - t0) * 1000ULL;

                if (synth_ret == SNT_ECANCELLED || finish_ret == SNT_ECANCELLED ||
                    snt_serial_is_active_cancelled(&g_serial, id)) {
                    PR_NOTICE("Worker: req %u cancelled", (unsigned)id);
                } else if (synth_ret != SNT_OK) {
                    snt_serial_notify_error(&g_serial, id, synth_ret, "synthesis failed");
                } else {
                    size_t N = eval_ctx.sample_offset;
                    double corr = 0.0, rms_ratio = 0.0;
                    if (N > 0) {
                        double mean_ref = eval_ctx.sum_ref / N;
                        double mean_cand = eval_ctx.sum_cand / N;
                        double var_ref = (eval_ctx.sum_sq_ref / N) - (mean_ref * mean_ref);
                        double var_cand = (eval_ctx.sum_sq_cand / N) - (mean_cand * mean_cand);
                        double cov = (eval_ctx.sum_prod / N) - (mean_ref * mean_cand);
                        if (var_ref > 0.0 && var_cand > 0.0) {
                            corr = cov / sqrt(var_ref * var_cand);
                        }
                        double rms_ref = sqrt(eval_ctx.sum_sq_ref / N);
                        double rms_cand = sqrt(eval_ctx.sum_sq_cand / N);
                        if (rms_ref > 0.0) {
                            rms_ratio = rms_cand / rms_ref;
                        }
                    }
                    bool gate_pass = (corr > 0.980 && rms_ratio > 0.80 && rms_ratio < 1.25);
                    snt_serial_send_json(&g_serial,
                        "{\"id\":%u,\"event\":\"eval\",\"voice\":\"en-nano\","
                        "\"samples\":%u,\"corr\":%.6f,\"rms_ratio\":%.6f,"
                        "\"max_err\":%.6e,\"status\":\"%s\"}",
                        id, (unsigned)N, corr, rms_ratio, eval_ctx.max_err,
                        gate_pass ? "PASS" : "FAIL");

                    uint32_t underruns = snt_audio_get_underruns();
                    snt_serial_notify_done(&g_serial, id, metrics.samples, metrics.source_rate,
                                           metrics.compute_us, wall_us, underruns);
                }
            }
        } else if (type == SNT_CMD_PHONEMES) {
            /* Direct phoneme synthesis */
            uint32_t sr = snt_engine_get_sample_rate();
            int begin_ret = snt_audio_begin(id, sr);
            if (begin_ret != SNT_OK) {
                snt_serial_notify_error(&g_serial, id, begin_ret, "audio begin failed");
                continue;
            }

            snt_serial_notify_synthesizing(&g_serial, id);
            snt_serial_notify_playing(&g_serial, id);

            snt_metrics_t metrics;
            memset(&metrics, 0, sizeof(metrics));

            int synth_ret = snt_engine_synthesize(req.phonemes, req.n_phonemes,
                                                  snt_audio_sink, NULL, &metrics);

            int finish_ret = snt_audio_finish(10000);

            uint32_t t1 = (uint32_t)tal_system_get_millisecond();
            uint64_t wall_us = (uint64_t)(t1 - t0) * 1000ULL;

            if (synth_ret == SNT_ECANCELLED || finish_ret == SNT_ECANCELLED ||
                snt_serial_is_active_cancelled(&g_serial, id)) {
                PR_NOTICE("Worker: req %u cancelled", (unsigned)id);
            } else if (synth_ret != SNT_OK) {
                snt_serial_notify_error(&g_serial, id, synth_ret, "phoneme synthesis failed");
            } else {
                uint32_t underruns = snt_audio_get_underruns();
                snt_serial_notify_done(&g_serial, id, metrics.samples, metrics.source_rate,
                                       metrics.compute_us, wall_us, underruns);
            }
        } else if (type == SNT_CMD_SAY) {
            /* Chinese G2P + sanoTTS inference */
            int32_t phoneme_ids[SNT_SERIAL_MAX_PHONEMES];
            size_t n_ids = 0;
            int g2p_ret = snt_g2p_convert(req.text, req.text_len, phoneme_ids, SNT_SERIAL_MAX_PHONEMES, &n_ids);
            if (g2p_ret != SNT_OK || n_ids == 0) {
                snt_serial_notify_error(&g_serial, id, g2p_ret, "g2p conversion failed");
                continue;
            }

            snt_serial_send_phonemes(&g_serial, id, phoneme_ids, n_ids);

            uint32_t sr = snt_engine_get_sample_rate();
            int begin_ret = snt_audio_begin(id, sr);
            if (begin_ret != SNT_OK) {
                snt_serial_notify_error(&g_serial, id, begin_ret, "audio begin failed");
                continue;
            }

            snt_serial_notify_synthesizing(&g_serial, id);
            snt_serial_notify_playing(&g_serial, id);

            snt_metrics_t metrics;
            memset(&metrics, 0, sizeof(metrics));

            int synth_ret = snt_engine_synthesize(phoneme_ids, n_ids,
                                                  snt_audio_sink, NULL, &metrics);

            int finish_ret = snt_audio_finish(10000);

            uint32_t t1 = (uint32_t)tal_system_get_millisecond();
            uint64_t wall_us = (uint64_t)(t1 - t0) * 1000ULL;

            if (synth_ret == SNT_ECANCELLED || finish_ret == SNT_ECANCELLED ||
                snt_serial_is_active_cancelled(&g_serial, id)) {
                PR_NOTICE("Worker: req %u cancelled", (unsigned)id);
            } else if (synth_ret != SNT_OK) {
                snt_serial_notify_error(&g_serial, id, synth_ret, "synthesis failed");
            } else {
                uint32_t underruns = snt_audio_get_underruns();
                snt_serial_notify_done(&g_serial, id, metrics.samples, metrics.source_rate,
                                       metrics.compute_us, wall_us, underruns);
            }
        } else if (type == SNT_CMD_G2P) {
            /* Chinese G2P diagnostic conversion */
            int32_t phoneme_ids[SNT_SERIAL_MAX_PHONEMES];
            size_t n_ids = 0;
            int g2p_ret = snt_g2p_convert(req.text, req.text_len, phoneme_ids, SNT_SERIAL_MAX_PHONEMES, &n_ids);
            uint32_t t1 = (uint32_t)tal_system_get_millisecond();
            uint64_t wall_us = (uint64_t)(t1 - t0) * 1000ULL;
            if (g2p_ret != SNT_OK || n_ids == 0) {
                snt_serial_notify_error(&g_serial, id, g2p_ret, "g2p conversion failed");
            } else {
                snt_serial_send_phonemes(&g_serial, id, phoneme_ids, n_ids);
                snt_serial_notify_done(&g_serial, id, 0, 0, wall_us, wall_us, 0);
            }
        }
    }
}

static void snt_rx_task(void *arg)
{
    (void)arg;
    uint8_t rx_buf[256];
    PR_NOTICE("snt_rx_task started on UART%d", (int)SNT_APP_UART_NUM);

    while (1) {
        int read_len = tal_uart_read(SNT_APP_UART_NUM, rx_buf, sizeof(rx_buf));
        if (read_len > 0) {
            snt_serial_feed(&g_serial, rx_buf, (size_t)read_len);
        } else {
            tal_system_sleep(10);
        }
    }
}

static size_t probe_max_continuous_block(bool is_psram)
{
    size_t low = 1024;
    size_t high = is_psram ? (16 * 1024 * 1024) : (512 * 1024);
    size_t best = 0;

    while (low <= high) {
        size_t mid = low + (high - low) / 2;
        void *ptr = NULL;
        if (is_psram) {
            ptr = tkl_system_psram_malloc(mid);
        } else {
            ptr = tal_malloc(mid);
        }

        if (ptr) {
            best = mid;
            if (is_psram) {
                tkl_system_psram_free(ptr);
            } else {
                tal_free(ptr);
            }
            low = mid + 1024;
        } else {
            if (mid <= 1024) break;
            high = mid - 1024;
        }
    }
    return best;
}

void user_main(void)
{
    tal_log_init(TAL_LOG_LEVEL_INFO, 1024, (TAL_LOG_OUTPUT_CB)tkl_log_output);

    PR_NOTICE("========================================");
    PR_NOTICE(" sanoTTS Serial - TUYA_T5AI_CORE (M0)");
    PR_NOTICE(" Project: %s, Version: %s", PROJECT_NAME, PROJECT_VERSION);
    PR_NOTICE(" Board: %s, Chip: %s", PLATFORM_BOARD, PLATFORM_CHIP);
    PR_NOTICE(" Compile: %s %s", __DATE__, __TIME__);
    PR_NOTICE("========================================");

    /* Register board hardware peripherals */
    board_register_hardware();

    /* Memory auditing for M0 report */
    int sram_free = tal_system_get_free_heap_size();
    int psram_free = tal_psram_get_free_heap_size();
    size_t sram_max_block = probe_max_continuous_block(false);
    size_t psram_max_block = probe_max_continuous_block(true);

    PR_NOTICE("Memory Status:");
    PR_NOTICE("  SRAM Free Heap:        %d bytes", sram_free);
    PR_NOTICE("  SRAM Max Block:        %zu bytes", sram_max_block);
    PR_NOTICE("  PSRAM Free Heap:       %d bytes", psram_free);
    PR_NOTICE("  PSRAM Max Block:       %zu bytes", psram_max_block);

    /* Initialize UART */
    TAL_UART_CFG_T uart_cfg = {0};
    uart_cfg.base_cfg.baudrate = 115200;
    uart_cfg.base_cfg.databits = TUYA_UART_DATA_LEN_8BIT;
    uart_cfg.base_cfg.stopbits = TUYA_UART_STOP_LEN_1BIT;
    uart_cfg.base_cfg.parity   = TUYA_UART_PARITY_TYPE_NONE;
    uart_cfg.rx_buffer_size    = 1024;
    uart_cfg.open_mode         = O_BLOCK;
    tal_uart_init(SNT_APP_UART_NUM, &uart_cfg);

    tal_mutex_create_init(&g_uart_tx_mutex);
    tal_mutex_create_init(&g_ring_mutex);
    tal_semaphore_create_init(&g_ring_sem, 0, 16);

    /* Initialize Protocol Engine */
    snt_serial_init(&g_serial,
                    snt_app_tx_callback, NULL,
                    snt_app_exec_callback, NULL,
                    snt_app_cancel_callback, NULL);
    /* Initialize Audio Driver & 32KB Ring Buffer */
    snt_audio_init(16000);

    /* Check and load Chinese assets from dedicated Flash partition */
    PR_NOTICE("Checking for sanoTTS Chinese assets in dedicated Flash partition (0x%08X)...",
              (unsigned)SNT_FLASH_MODEL_ADDR);

    /* Pre-allocate continuous PSRAM arena for inference before asset loading to eliminate fragmentation */
    snt_engine_preallocate_arena(SNT_VOICE_ZH);
    size_t arena_sz = 0;
    void *arena_buf = snt_engine_get_arena(&arena_sz);
    PR_NOTICE("snt_engine: pre-allocated PSRAM arena at %p (%zu bytes)", arena_buf, arena_sz);

    int asset_ret = snt_assets_load_chinese(arena_buf, arena_sz);
    if (asset_ret == SNT_OK && snt_assets_is_chinese_ready()) {
        snt_serial_set_voice(&g_serial, "zh");
        int g2p_ret = snt_g2p_init(SNT_VOICE_ZH);
        int eng_ret = snt_engine_init(SNT_VOICE_ZH);
        PR_NOTICE("sanoTTS Chinese mode initialized: g2p_ret=%d, eng_ret=%d", g2p_ret, eng_ret);
    } else {
#ifdef SNT_ENABLE_EN_NANO
        PR_NOTICE("snt_assets_load_chinese returned %d, falling back to en-nano", asset_ret);
        snt_serial_set_voice(&g_serial, "en-nano");
        snt_engine_set_model_blobs(SNT_VOICE_EN_NANO,
                                   snt_en_nano_front_q8, snt_en_nano_front_q8_size,
                                   snt_en_nano_model_q8, snt_en_nano_model_q8_size);
        int eng_ret = snt_engine_init(SNT_VOICE_EN_NANO);
        PR_NOTICE("snt_engine_init(EN_NANO) returned %d", eng_ret);
#else
        PR_ERR("snt_assets_load_chinese failed: %d! Chinese assets required in Flash partition.", asset_ret);
#endif
    }

    /* Start Worker Task (64KB stack in PSRAM to accommodate eSpeak-ng + inference) */
    THREAD_CFG_T worker_cfg = {0};
    worker_cfg.stackDepth = 1024 * 64;
    worker_cfg.priority   = THREAD_PRIO_3;
    worker_cfg.thrdname   = "snt_worker";
    worker_cfg.psram_mode = 1;
    tal_thread_create_and_start(&g_worker_thread, NULL, NULL, snt_worker_task, NULL, &worker_cfg);

    /* Start UART RX Task */
    THREAD_CFG_T rx_cfg = {0};
    rx_cfg.stackDepth = 1024 * 16;
    rx_cfg.priority   = THREAD_PRIO_3;
    rx_cfg.thrdname   = "snt_rx";
    rx_cfg.psram_mode = 1;
    tal_thread_create_and_start(&g_rx_thread, NULL, NULL, snt_rx_task, NULL, &rx_cfg);

    PR_NOTICE("sanoTTS M0/M1 initialization complete. Listening on UART%d...", (int)SNT_APP_UART_NUM);
}

#if OPERATING_SYSTEM == SYSTEM_LINUX
int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    user_main();
    while (1) {
        tal_system_sleep(500);
    }
    return 0;
}
#else
static void tuya_app_thread_entry(void *arg)
{
    (void)arg;
    user_main();
    tal_thread_delete(g_app_thread);
    g_app_thread = NULL;
}

void tuya_app_main(void)
{
    THREAD_CFG_T thrd_param = {0};
    thrd_param.stackDepth = 1024 * 4;
    thrd_param.priority   = THREAD_PRIO_1;
    thrd_param.thrdname   = "tuya_app_main";
    tal_thread_create_and_start(&g_app_thread, NULL, NULL, tuya_app_thread_entry, NULL, &thrd_param);
}
#endif
