#ifndef __SNT_FIXTURE_EN_H__
#define __SNT_FIXTURE_EN_H__

#include <stddef.h>
#include <stdint.h>

#define SNT_R00_N_TOKENS 73
#define SNT_R00_N_SAMPLES 105984
#define SNT_R00_SEED     2236265385529901705ULL

extern const int32_t snt_r00_ids[SNT_R00_N_TOKENS];
extern const int32_t snt_r00_durs[SNT_R00_N_TOKENS];
#ifdef SNT_NO_FULL_EN_FIXTURE
extern const float *snt_r00_audio;
#else
extern const float   snt_r00_audio[SNT_R00_N_SAMPLES];
#endif

#endif /* __SNT_FIXTURE_EN_H__ */
