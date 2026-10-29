/* snt_engine.h -- sanoTTS inference engine wrapper.
 *
 * Implements the snt_contract.h engine interface for:
 *   - SNT_VOICE_EN_NANO: 24,000 Hz INT8 duration + acoustic + TinyVocos (Task C1)
 *   - SNT_VOICE_ZH:      22,050 Hz FP32 front + contextual chunked decoder (Task C2)
 */

#ifndef __SNT_ENGINE_H__
#define __SNT_ENGINE_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "snt_contract.h"
#include "snt_chinese_chunk.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Test hooks and configuration (optional) */
void snt_engine_set_model_dir(const char *dir);
void snt_engine_set_model_blobs(snt_voice_t voice,
                                const void *front_blob, size_t front_size,
                                const void *dec_blob, size_t dec_size);
void snt_engine_set_test_seed(uint64_t seed);
void snt_engine_set_dur_override(const int32_t *durs, size_t n_durs);
void snt_engine_set_chunk_config(const snt_chunk_config_t *cfg);

/* Memory queries */
int snt_engine_preallocate_arena(snt_voice_t voice);
void *snt_engine_get_arena(size_t *size);
size_t snt_engine_get_arena_peak(void);
size_t snt_engine_get_arena_size(void);
uint32_t snt_engine_get_sample_rate(void);
snt_voice_t snt_engine_get_active_voice(void);

#ifdef __cplusplus
}
#endif

#endif /* __SNT_ENGINE_H__ */
