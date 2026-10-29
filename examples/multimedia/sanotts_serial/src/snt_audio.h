/**
 * @file snt_audio.h
 * @brief sanoTTS Audio Output Driver, Ring Buffer and Backpressure
 */

#ifndef __SNT_AUDIO_H__
#define __SNT_AUDIO_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "snt_contract.h"
#include "snt_resampler.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SNT_AUDIO_RINGBUF_SIZE   32768   /* 32 KiB PCM16 = 16384 samples (~1.024s at 16kHz) */
#define SNT_AUDIO_FRAME_SAMPLES  320     /* 20ms at 16kHz */

/* Initialize audio hardware, ring buffer and playback thread */
int snt_audio_init(uint32_t output_rate);

/* Begin a new audio request */
int snt_audio_begin(uint32_t request_id, uint32_t source_rate);

/* Sink float PCM chunks from engine / generator */
int snt_audio_sink(const float *pcm, size_t n, uint32_t rate, void *user);

/* Wait for ring buffer and hardware speaker FIFO to drain */
int snt_audio_finish(uint32_t timeout_ms);

/* Immediately abort playback, clear ring buffer and driver buffers */
void snt_audio_abort(void);

/* Check if audio is currently aborted/cancelled */
bool snt_audio_is_aborted(void);

/* Generate and play 1 second 440 Hz test tone at 16kHz */
int snt_audio_play_test_tone(uint32_t request_id);

/* Diagnostics */
uint32_t snt_audio_get_samples_played(void);
uint32_t snt_audio_get_underruns(void);

#ifdef __cplusplus
}
#endif

#endif /* __SNT_AUDIO_H__ */
