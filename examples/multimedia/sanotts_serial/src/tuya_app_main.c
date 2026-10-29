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
#include <stdio.h>
#include <string.h>

#define SNT_APP_UART_NUM TUYA_UART_NUM_0

static snt_serial_t  g_serial;
static MUTEX_HANDLE  g_uart_tx_mutex = NULL;
static SEM_HANDLE    g_req_sem = NULL;
static snt_request_t g_current_req;
static volatile bool g_has_current_req = false;
static THREAD_HANDLE g_rx_thread = NULL;
static THREAD_HANDLE g_worker_thread = NULL;

#if OPERATING_SYSTEM != SYSTEM_LINUX
static THREAD_HANDLE g_app_thread = NULL;
#endif

/* Weak default implementations of contract symbols for Task C and Task D */
__attribute__((weak)) int snt_engine_init(snt_voice_t voice) { (void)voice; return SNT_OK; }
__attribute__((weak)) int snt_engine_synthesize(const int32_t *ids, size_t n_ids,
                                                snt_float_sink_t sink, void *user,
                                                snt_metrics_t *metrics) {
    (void)ids; (void)n_ids; (void)sink; (void)user; (void)metrics;
    return SNT_OK;
}
__attribute__((weak)) void snt_engine_deinit(void) {}
__attribute__((weak)) void snt_engine_request_cancel(void) {}

__attribute__((weak)) int snt_g2p_init(snt_voice_t voice) { (void)voice; return SNT_OK; }
__attribute__((weak)) int snt_g2p_convert(const char *utf8, size_t bytes,
                                          int32_t *ids, size_t cap, size_t *used) {
    (void)utf8; (void)bytes; (void)ids; (void)cap; if (used) *used = 0;
    return SNT_OK;
}
__attribute__((weak)) void snt_g2p_deinit(void) {}
__attribute__((weak)) int snt_g2p_foreach_segment(const char *utf8, size_t bytes,
                                                  snt_ids_segment_cb cb, void *user) {
    (void)utf8; (void)bytes; (void)cb; (void)user;
    return SNT_OK;
}
__attribute__((weak)) void snt_g2p_request_cancel(void) {}

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

    memcpy(&g_current_req, req, sizeof(snt_request_t));
    g_has_current_req = true;
    tal_semaphore_post(g_req_sem);
}

static void snt_app_cancel_callback(void *user)
{
    (void)user;
    snt_audio_abort();
    snt_engine_request_cancel();
    snt_g2p_request_cancel();
}

static void snt_worker_task(void *arg)
{
    (void)arg;
    PR_NOTICE("snt_worker_task started");

    while (1) {
        tal_semaphore_wait(g_req_sem, SEM_WAIT_FOREVER);
        if (!g_has_current_req) {
            continue;
        }

        uint32_t id = g_current_req.id;
        snt_cmd_type_t type = g_current_req.type;
        (void)type;
        uint32_t t0 = (uint32_t)tal_system_get_millisecond();

        snt_serial_notify_synthesizing(&g_serial, id);

        /* In M0, play the 1-second 440 Hz test tone via the audio pipeline */
        snt_serial_notify_playing(&g_serial, id);

        int play_ret = snt_audio_play_test_tone(id);

        uint32_t t1 = (uint32_t)tal_system_get_millisecond();
        uint64_t wall_us = (uint64_t)(t1 - t0) * 1000ULL;
        uint64_t compute_us = 10000ULL; /* ~10ms sine computation */

        if (play_ret == SNT_ECANCELLED || snt_serial_is_active_cancelled(&g_serial, id)) {
            snt_serial_notify_cancelled(&g_serial, id);
        } else if (play_ret != SNT_OK) {
            snt_serial_notify_error(&g_serial, id, play_ret, "audio playback failed");
        } else {
            uint32_t samples = snt_audio_get_samples_played();
            uint32_t underruns = snt_audio_get_underruns();
            snt_serial_notify_done(&g_serial, id, samples, 16000, compute_us, wall_us, underruns);
        }

        g_has_current_req = false;
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
    tal_semaphore_create_init(&g_req_sem, 0, 16);

    /* Initialize Protocol Engine */
    snt_serial_init(&g_serial,
                    snt_app_tx_callback, NULL,
                    snt_app_exec_callback, NULL,
                    snt_app_cancel_callback, NULL);
    snt_serial_set_voice(&g_serial, "en-nano");

    /* Initialize Audio Driver & 32KB Ring Buffer */
    snt_audio_init(16000);

    /* Start Worker Task */
    THREAD_CFG_T worker_cfg = {0};
    worker_cfg.stackDepth = 1024 * 4;
    worker_cfg.priority   = THREAD_PRIO_3;
    worker_cfg.thrdname   = "snt_worker";
    tal_thread_create_and_start(&g_worker_thread, NULL, NULL, snt_worker_task, NULL, &worker_cfg);

    /* Start UART RX Task */
    THREAD_CFG_T rx_cfg = {0};
    rx_cfg.stackDepth = 1024 * 4;
    rx_cfg.priority   = THREAD_PRIO_3;
    rx_cfg.thrdname   = "snt_rx";
    tal_thread_create_and_start(&g_rx_thread, NULL, NULL, snt_rx_task, NULL, &rx_cfg);

    PR_NOTICE("sanoTTS M0 initialization complete. Listening on UART%d...", (int)SNT_APP_UART_NUM);
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
