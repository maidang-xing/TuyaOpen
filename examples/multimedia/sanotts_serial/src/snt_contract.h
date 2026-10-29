#ifndef __SNT_CONTRACT_H__
#define __SNT_CONTRACT_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SNT_OK = 0,
    SNT_EINVAL = -1,
    SNT_E2BIG = -2,
    SNT_ENOMEM = -3,
    SNT_EBUSY = -4,
    SNT_EUNSUPPORTED = -5,
    SNT_EIO = -6,
    SNT_ECANCELLED = -7,
    SNT_ETIMEOUT = -8
} snt_result_t;

typedef enum {
    SNT_VOICE_EN_NANO = 0,
    SNT_VOICE_ZH = 1
} snt_voice_t;

typedef struct {
    uint32_t source_rate;
    uint32_t samples;
    uint64_t compute_us;
    uint64_t first_pcm_us;
    size_t arena_peak;
} snt_metrics_t;

/* 同步消费数据，返回前必须复制或消费完；0 继续，非 0 中止。 */
typedef int (*snt_float_sink_t)(const float *pcm, size_t n,
                                uint32_t rate, void *user);

int snt_engine_init(snt_voice_t voice);
int snt_engine_synthesize(const int32_t *ids, size_t n_ids,
                          snt_float_sink_t sink, void *user,
                          snt_metrics_t *metrics);
void snt_engine_deinit(void);
/* 控制任务可调用；线程安全。新请求开始时复位取消状态。 */
void snt_engine_request_cancel(void);

/* ids 由调用者拥有；容量按 int32_t 元素计，文本长度按字节计。 */
int snt_g2p_init(snt_voice_t voice);
int snt_g2p_convert(const char *utf8, size_t bytes,
                    int32_t *ids, size_t cap, size_t *used);
void snt_g2p_deinit(void);
/* 回调中的 IDs 仅在回调期间有效，按原文顺序逐段输出。 */
typedef int (*snt_ids_segment_cb)(const int32_t *ids, size_t n_ids,
                                  size_t segment_index, void *user);
int snt_g2p_foreach_segment(const char *utf8, size_t bytes,
                            snt_ids_segment_cb cb, void *user);
void snt_g2p_request_cancel(void); /* 同样线程安全 */

int snt_audio_init(uint32_t output_rate);
int snt_audio_begin(uint32_t request_id, uint32_t source_rate);
int snt_audio_sink(const float *pcm, size_t n, uint32_t rate, void *user);
int snt_audio_finish(uint32_t timeout_ms); /* 等待缓冲和设备排空 */
void snt_audio_abort(void); /* 取消后清空残留，下一请求可重新 begin */

#ifdef __cplusplus
}
#endif

#endif /* __SNT_CONTRACT_H__ */
