/* snt_port_t5.c -- Tuya T5AI / FreeRTOS & POSIX platform port for sanoTTS.
 *
 * Implements porting interface required by snt_port.h:
 *   - snt_par_run: serial execution fallback for single-task inference
 *   - snt_scratch_id: thread scratch buffer slot (0 for single task)
 *   - snt_now_us: microsecond wall clock profiling
 */

#include "snt_port.h"
#include <stdint.h>
#include <stddef.h>

#if defined(BEKEN_PLATFORM) || defined(__arm__) || (defined(OPERATING_SYSTEM) && OPERATING_SYSTEM != SYSTEM_LINUX) || defined(TARGET_T5AI) || defined(TUYA_OS)
#include "tal_api.h"
#include "tal_system.h"
#else
#include <time.h>
#include <sys/time.h>
#endif

void snt_par_run(snt_par_fn f, int n, void *ctx) {
    /* Serial execution for single-task inference as mandated by the design */
    if (f && n > 0) {
        f(0, n, ctx);
    }
}

int snt_scratch_id(void) {
    return 0;
}

int64_t snt_now_us(void) {
#if defined(BEKEN_PLATFORM) || defined(__arm__) || (defined(OPERATING_SYSTEM) && OPERATING_SYSTEM != SYSTEM_LINUX)
    return (int64_t)tal_system_get_millisecond() * 1000LL;
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (int64_t)ts.tv_sec * 1000000LL + (int64_t)(ts.tv_nsec / 1000LL);
    }
    return (int64_t)clock() * 1000000LL / CLOCKS_PER_SEC;
#else
    return (int64_t)clock() * 1000000LL / CLOCKS_PER_SEC;
#endif
}
