#include "snt_g2p.h"
#include "snt_text.h"
#include "snt_contract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include "espeak-ng/speak_lib.h"
#include "espeak-ng/espeak_ng.h"

#define ID_PAD 0
#define ID_BOS 1
#define ID_EOS 2

typedef struct {
    unsigned int cp;
    unsigned char id;
} cp_id_t;

/* CP_ID_ZH table: codepoint -> Piper phoneme id for Chinese (152 entries, sorted by cp) */
static const cp_id_t CP_ID_ZH[152] = {
    {0x20, 3},   {0x21, 4},   {0x22, 150}, {0x23, 149}, {0x24, 2},   {0x27, 5},
    {0x28, 6},   {0x29, 7},   {0x2C, 8},   {0x2D, 9},   {0x2E, 10},  {0x30, 130},
    {0x31, 131}, {0x32, 132}, {0x33, 133}, {0x34, 134}, {0x35, 135}, {0x36, 136},
    {0x37, 137}, {0x38, 138}, {0x39, 139}, {0x3A, 11},  {0x3B, 12},  {0x3F, 13},
    {0x5E, 1},   {0x5F, 0},   {0x61, 14},  {0x62, 15},  {0x63, 16},  {0x64, 17},
    {0x65, 18},  {0x66, 19},  {0x68, 20},  {0x69, 21},  {0x6A, 22},  {0x6B, 23},
    {0x6C, 24},  {0x6D, 25},  {0x6E, 26},  {0x6F, 27},  {0x70, 28},  {0x71, 29},
    {0x72, 30},  {0x73, 31},  {0x74, 32},  {0x75, 33},  {0x76, 34},  {0x77, 35},
    {0x78, 36},  {0x79, 37},  {0x7A, 38},  {0xE6, 39},  {0xE7, 40},  {0xF0, 41},
    {0xF8, 42},  {0x127, 43}, {0x14B, 44}, {0x153, 45}, {0x1C0, 46}, {0x1C1, 47},
    {0x1C2, 48}, {0x1C3, 49}, {0x250, 50}, {0x251, 51}, {0x252, 52}, {0x253, 53},
    {0x254, 54}, {0x255, 55}, {0x256, 56}, {0x257, 57}, {0x258, 58}, {0x259, 59},
    {0x25A, 60}, {0x25B, 61}, {0x25C, 62}, {0x25E, 63}, {0x25F, 64}, {0x260, 65},
    {0x261, 66}, {0x262, 67}, {0x263, 68}, {0x264, 69}, {0x265, 70}, {0x266, 71},
    {0x267, 72}, {0x268, 73}, {0x26A, 74}, {0x26B, 75}, {0x26C, 76}, {0x26D, 77},
    {0x26E, 78}, {0x26F, 79}, {0x270, 80}, {0x271, 81}, {0x272, 82}, {0x273, 83},
    {0x274, 84}, {0x275, 85}, {0x276, 86}, {0x278, 87}, {0x279, 88}, {0x27A, 89},
    {0x27B, 90}, {0x27D, 91}, {0x27E, 92}, {0x280, 93}, {0x281, 94}, {0x282, 95},
    {0x283, 96}, {0x284, 97}, {0x288, 98}, {0x289, 99}, {0x28A, 100},{0x28B, 101},
    {0x28C, 102},{0x28D, 103},{0x28E, 104},{0x28F, 105},{0x290, 106},{0x291, 107},
    {0x292, 108},{0x294, 109},{0x295, 110},{0x298, 111},{0x299, 112},{0x29B, 113},
    {0x29C, 114},{0x29D, 115},{0x29F, 116},{0x2A1, 117},{0x2A2, 118},{0x2B0, 145},
    {0x2B2, 119},{0x2C8, 120},{0x2CC, 121},{0x2D0, 122},{0x2D1, 123},{0x2DE, 124},
    {0x2E4, 146},{0x303, 141},{0x327, 140},{0x329, 144},{0x32A, 142},{0x32F, 143},
    {0x3B2, 125},{0x3B5, 147},{0x3B8, 126},{0x3C7, 127},{0x1D7B, 128},{0x2191, 151},
    {0x2193, 148},{0x2C71, 129}
};

static char g_data_dir[512] = "";
static bool g_initialized = false;
static snt_voice_t g_current_voice = SNT_VOICE_ZH;
static volatile int g_cancel_requested = 0;
static int g_unknown_count = 0;

static int lookup_zh(unsigned cp) {
    int lo = 0, hi = 152;
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if (CP_ID_ZH[m].cp < cp) lo = m + 1;
        else hi = m;
    }
    if (lo < 152 && CP_ID_ZH[lo].cp == cp) {
        return CP_ID_ZH[lo].id;
    }
    g_unknown_count++;
    return -1;
}

static void ensure_default_data_dir(void) {
    if (g_data_dir[0] != '\0') return;
    const char *env = getenv("SNT_DATA_DIR");
    if (env && env[0] != '\0') {
        snprintf(g_data_dir, sizeof(g_data_dir), "%s", env);
        return;
    }
#if defined(PLATFORM_LINUX) || defined(__linux__) || defined(__APPLE__)
    snprintf(g_data_dir, sizeof(g_data_dir), "examples/multimedia/sanotts_serial/models/chinese");
#else
    snprintf(g_data_dir, sizeof(g_data_dir), "/models/chinese");
#endif
}

int snt_g2p_set_data_dir(const char *dir) {
    if (!dir || strlen(dir) == 0 || strlen(dir) >= sizeof(g_data_dir)) {
        return SNT_EINVAL;
    }
    snprintf(g_data_dir, sizeof(g_data_dir), "%s", dir);
    return SNT_OK;
}

const char *snt_g2p_get_data_dir(void) {
    ensure_default_data_dir();
    return g_data_dir;
}

int snt_g2p_get_unknown_symbol_count(void) {
    return g_unknown_count;
}

void snt_g2p_reset_unknown_symbol_count(void) {
    g_unknown_count = 0;
}

void snt_g2p_request_cancel(void) {
    g_cancel_requested = 1;
}

int snt_g2p_init(snt_voice_t voice) {
    if (voice != SNT_VOICE_ZH && voice != SNT_VOICE_EN_NANO) {
        return SNT_EINVAL;
    }

    ensure_default_data_dir();

    if (g_initialized && g_current_voice == voice) {
        g_cancel_requested = 0;
        return SNT_OK;
    }

    if (g_initialized) {
        espeak_ng_Terminate();
        g_initialized = false;
    }

    espeak_ng_InitializePath(g_data_dir);
    espeak_ng_ERROR_CONTEXT ctx = NULL;
    espeak_ng_STATUS s = espeak_ng_Initialize(&ctx);
    if (s != ENS_OK) {
        return SNT_EIO;
    }

    const char *vname = (voice == SNT_VOICE_ZH) ? "cmn" : "en";
    s = espeak_ng_SetVoiceByName(vname);
    if (s != ENS_OK) {
        espeak_ng_Terminate();
        return SNT_EIO;
    }

    g_initialized = true;
    g_current_voice = voice;
    g_cancel_requested = 0;
    return SNT_OK;
}

void snt_g2p_deinit(void) {
    if (g_initialized) {
        espeak_ng_Terminate();
        g_initialized = false;
    }
    g_cancel_requested = 0;
}

int snt_g2p_convert(const char *utf8, size_t bytes,
                    int32_t *ids, size_t cap, size_t *used) {
    if (!utf8 || !ids || !used || cap == 0) {
        return SNT_EINVAL;
    }

    if (g_cancel_requested) {
        *used = 0;
        return SNT_ECANCELLED;
    }

    if (!g_initialized) {
        int rc = snt_g2p_init(SNT_VOICE_ZH);
        if (rc != SNT_OK) return rc;
    }

    if (bytes == 0) {
        *used = 0;
        return SNT_OK;
    }

    /* Check if input is only whitespace */
    bool all_space = true;
    for (size_t i = 0; i < bytes; i++) {
        if (!isspace((unsigned char)utf8[i])) {
            all_space = false;
            break;
        }
    }
    if (all_space) {
        *used = 0;
        return SNT_OK;
    }

    /* Clean and normalize text */
    char norm_buf[1024];
    size_t norm_len = 0;
    int nrc = snt_text_normalize(utf8, bytes, norm_buf, sizeof(norm_buf), &norm_len, SNT_TEXT_NORM_DEFAULT);
    if (nrc != SNT_OK) {
        /* Fall back to using input text up to buffer limit */
        size_t copy_len = (bytes < sizeof(norm_buf) - 1) ? bytes : sizeof(norm_buf) - 1;
        memcpy(norm_buf, utf8, copy_len);
        norm_buf[copy_len] = '\0';
    }

    size_t n = 0;
    if (n + 2 > cap) return SNT_E2BIG;
    ids[n++] = ID_BOS;
    ids[n++] = ID_PAD;

    const void *tp = norm_buf;
    int guard = 0;

    while (tp && *(const char *)tp && guard++ < 256) {
        if (g_cancel_requested) {
            *used = 0;
            return SNT_ECANCELLED;
        }

        const char *p0 = (const char *)tp;
        const char *ph = espeak_TextToPhonemes(&tp, espeakCHARS_UTF8, espeakPHONEMES_IPA);
        const char *p1 = tp ? (const char *)tp : p0 + strlen(p0);

        if (ph) {
            const char *p = ph;
            uint32_t cp = 0;
            int paren = 0;
            while (*p) {
                size_t step = snt_text_next_codepoint(p, strlen(p), &cp);
                if (step == 0) {
                    p++;
                    continue;
                }
                p += step;

                if (cp == '(') { paren = 1; continue; }
                if (cp == ')') { paren = 0; continue; }
                if (paren) continue;

                int id = lookup_zh(cp);
                if (id >= 0) {
                    if (n + 2 > cap) return SNT_E2BIG;
                    ids[n++] = id;
                    ids[n++] = ID_PAD;
                }
            }
        }

        /* Check for trailing punctuation in the processed segment */
        char tc = 0;
        for (const char *q = p1 - 1; q >= p0; q--) {
            char c = *q;
            if (c == '.' || c == ',' || c == '?' || c == '!' || c == ':' || c == ';') {
                tc = c;
                break;
            }
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '-') break;
        }

        if (tc) {
            int id = lookup_zh((unsigned char)tc);
            if (id >= 0) {
                if (n + 2 > cap) return SNT_E2BIG;
                ids[n++] = id;
                ids[n++] = ID_PAD;
            }
            if (tc == ',' || tc == ':' || tc == ';') {
                int sid = lookup_zh(' ');
                if (sid >= 0) {
                    if (n + 2 > cap) return SNT_E2BIG;
                    ids[n++] = sid;
                    ids[n++] = ID_PAD;
                }
            }
        }
    }

    if (n + 1 > cap) return SNT_E2BIG;
    ids[n++] = ID_EOS;

    *used = n;
    return SNT_OK;
}

/* User context for foreach_segment callback */
typedef struct {
    snt_ids_segment_cb cb;
    void *user;
    size_t seg_index;
    bool cancelled;
} foreach_ctx_t;

static int segment_text_callback(const char *seg_text, size_t seg_bytes,
                                 size_t index, void *user) {
    foreach_ctx_t *ctx = (foreach_ctx_t *)user;
    (void)index;

    if (g_cancel_requested) {
        ctx->cancelled = true;
        return 1;
    }

    int32_t seg_ids[256];
    size_t seg_used = 0;
    int rc = snt_g2p_convert(seg_text, seg_bytes, seg_ids, 256, &seg_used);

    if (rc == SNT_ECANCELLED) {
        ctx->cancelled = true;
        return 1;
    }

    if (rc == SNT_E2BIG) {
        /* Segment generated > 256 IDs (e.g. exceptionally long or dense)
         * Bisect segment into two halves and process recursively */
        size_t half = seg_bytes / 2;
        /* Align half to UTF-8 character boundary */
        while (half > 0 && ((unsigned char)seg_text[half] & 0xC0) == 0x80) {
            half--;
        }
        if (half > 0 && half < seg_bytes) {
            int r1 = segment_text_callback(seg_text, half, index, user);
            if (r1 != 0) return r1;
            return segment_text_callback(seg_text + half, seg_bytes - half, index + 1, user);
        }
    }

    if (rc != SNT_OK || seg_used == 0) {
        return 0; /* Continue to next segment on empty */
    }

    int cb_rc = ctx->cb(seg_ids, seg_used, ctx->seg_index++, ctx->user);
    if (cb_rc != 0) {
        return 1; /* Stop iteration */
    }

    return 0;
}

int snt_g2p_foreach_segment(const char *utf8, size_t bytes,
                            snt_ids_segment_cb cb, void *user) {
    if (!utf8 || !cb) return SNT_EINVAL;
    if (bytes == 0) return SNT_OK;

    g_cancel_requested = 0;

    foreach_ctx_t ctx = {
        .cb = cb,
        .user = user,
        .seg_index = 0,
        .cancelled = false
    };

    int rc = snt_text_split_segments(utf8, bytes, 25, segment_text_callback, &ctx);

    if (ctx.cancelled || g_cancel_requested) {
        return SNT_ECANCELLED;
    }

    return rc;
}
