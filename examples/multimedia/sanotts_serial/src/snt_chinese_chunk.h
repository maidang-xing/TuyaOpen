/* snt_chinese_chunk.h -- Contextual chunked decoding for Chinese piperlite.
 *
 * Implements low-memory chunked neural audio synthesis for the Chinese FP32
 * piperlite decoder (Task C2).
 *
 * Receptive field derivation (exact backward analysis):
 * ---------------------------------------------------
 *  1. WaveformPostFilter (at audio sample rate 22,050 Hz):
 *     - in_conv:  K=9, dil=1, pad=4 -> radius 4
 *     - ResUnit:  K=3, dil=1, pad=1 (conv1) + K=3, dil=1, pad=1 (conv2) -> radius 2
 *     - out_conv: K=9, dil=1, pad=4 -> radius 4
 *     Total PF radius = 4 + 2 + 4 = 10 audio samples.
 *
 *  2. Post-conv (at audio sample rate):
 *     - Conv1d:   K=7, dil=1, pad=3 -> radius 3 audio samples.
 *
 *  3. Stage 2 Residual Bank (at audio sample rate = 256 * frames):
 *     - Branch 2: conv1 (K=7, dil=3, pad=9) + conv2 (K=7, dil=12, pad=36)
 *     Total ResBank radius = 9 + 36 = 45 audio samples.
 *     Accumulated radius at audio rate = 45 + 3 + 10 = 58 audio samples.
 *
 *  4. Stage 2 Upsampling ConvTranspose1d (stride=4, K=8, pad=2):
 *     Scatter: j = 4*t + k - 2, k in [0, 7].
 *     To cover output radius 58: required input radius at rate T2 (64 * frames)
 *     is ceil((58 - 2) / 4) = 14 -> 15 elements.
 *
 *  5. Stage 1 Residual Bank (at rate T2):
 *     Branch 2 adds radius 45 elements -> 15 + 45 = 60 elements at rate T2.
 *
 *  6. Stage 1 Upsampling ConvTranspose1d (stride=8, K=16, pad=4):
 *     Scatter: j = 8*t + k - 4, k in [0, 15].
 *     To cover 60 elements: required input radius at rate T1 (8 * frames)
 *     is ceil((60 - 4) / 8) = 7 -> 8 elements.
 *
 *  7. Stage 0 Residual Bank (at rate T1):
 *     Branch 2 adds radius 45 elements -> 8 + 45 = 53 elements at rate T1.
 *
 *  8. Stage 0 Upsampling ConvTranspose1d (stride=8, K=16, pad=4):
 *     To cover 53 elements: required input radius at rate T0 (latent frame rate)
 *     is ceil((53 - 4) / 8) = 7 -> 8 latent frames.
 *
 *  9. Pre-conv (at latent frame rate):
 *     Conv1d: K=7, dil=1, pad=3 -> radius 3 latent frames.
 *
 *  Total theoretical minimum context radius = 8 + 3 = 11 latent frames.
 *  Any context >= 11 latent frames guarantees 100% mathematical bit-exactness
 *  with full-sentence FP32 decoding (max error = 0.0, corr = 1.000000).
 *
 *  Default configuration:
 *    - Center chunk length: 16 latent frames (~185.8 ms at 22,050 Hz)
 *    - Left/Right context:  16 latent frames (margin > 11 for total fidelity)
 *    - Total chunk size:    <= 48 latent frames (~3.46 MB peak arena, vs 15+ MB)
 */

#ifndef __SNT_CHINESE_CHUNK_H__
#define __SNT_CHINESE_CHUNK_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "snt_contract.h"
#include "snt_piperlite.h"
#include "snt_front_f32.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SNT_CHUNK_MIN_CONTEXT_FRAMES    11
#define SNT_CHUNK_DEFAULT_CENTER_FRAMES 16
#define SNT_CHUNK_DEFAULT_CONTEXT_FRAMES 16

typedef struct {
    int center_frames;     /* Center decoded frames to retain per chunk (default 16) */
    int context_frames;    /* Context frames on left and right (default 16, min 11) */
} snt_chunk_config_t;

/* Initialize chunk configuration with default parameters */
void snt_chunk_config_default(snt_chunk_config_t *cfg);

/* Calculate maximum floats required in working arena for chunked decoding */
size_t snt_chinese_chunk_arena_floats(const snt_piperlite_model *m,
                                      const snt_chunk_config_t *cfg);

/* Chunked synthesis:
 * Decodes latent [m->in_ch, total_frames] chunk-by-chunk using center_frames
 * and context_frames. Only center chunk audio is emitted.
 *
 * Boundary handling:
 *   - For chunk 0 (t0 == 0): Left edge uses natural zero-padding identical to
 *     full-sentence start.
 *   - For last chunk (t1 == total_frames): Right edge uses natural zero-padding
 *     identical to full-sentence end.
 *   - Non-integer tail frames: Retains exact remaining center frames without padding.
 *
 * Parameters:
 *   m: initialized piperlite model
 *   latent: channel-major latent tensor [m->in_ch, total_frames]
 *   total_frames: total number of latent frames
 *   cfg: chunk configuration (NULL for default)
 *   sink: optional callback to stream center chunks to caller/audio driver
 *   user: user context passed to sink
 *   audio_out: optional destination buffer for full stitched audio (size >= total_frames * 256)
 *   arena: caller-provided scratch buffer
 *   arena_floats: size of scratch buffer in floats
 *   cancel_flag: optional pointer to cancellation flag (checked per chunk)
 *   first_pcm_us: optional pointer to receive latency until first chunk ready
 *
 * Returns:
 *   SNT_OK (0) on success
 *   SNT_ECANCELLED if cancelled
 *   SNT_EINVAL / SNT_ENOMEM on error
 */
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
                                 uint64_t *first_pcm_us);

#ifdef __cplusplus
}
#endif

#endif /* __SNT_CHINESE_CHUNK_H__ */
