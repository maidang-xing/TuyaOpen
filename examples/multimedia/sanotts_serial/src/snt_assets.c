/* snt_assets.c -- Flash asset loader and decompressor for sanoTTS Chinese models & dict.
 */

#include "snt_assets.h"
#include "snt_contract.h"
#include "miniz.h"
#ifdef crc32
#undef crc32
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(BEKEN_PLATFORM) || defined(__arm__) || (defined(OPERATING_SYSTEM) && OPERATING_SYSTEM != SYSTEM_LINUX)
#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tkl_flash.h"
#include "tal_log.h"
#define SNT_PSRAM_ALLOC(sz) tal_psram_malloc(sz)
#define SNT_PSRAM_FREE(p)   tal_psram_free(p)
#else
#define SNT_PSRAM_ALLOC(sz) malloc(sz)
#define SNT_PSRAM_FREE(p)   free(p)
#define PR_NOTICE(...) printf(__VA_ARGS__); printf("\n")
#define PR_ERR(...)    fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n")
#endif

typedef struct {
    char name[32];
    void *data;
    size_t size;
} snt_loaded_entry_t;

static snt_asset_header_t s_hdr;
static bool s_header_valid = false;
static snt_loaded_entry_t s_loaded[16];
static size_t s_n_loaded = 0;
static bool s_chinese_ready = false;

static void unshuffle_f32(const uint8_t *shuffled, uint8_t *out, size_t total_bytes)
{
    if (total_bytes % 4 != 0) return;
    size_t n = total_bytes / 4;
    const uint8_t *b0 = shuffled;
    const uint8_t *b1 = shuffled + n;
    const uint8_t *b2 = shuffled + 2 * n;
    const uint8_t *b3 = shuffled + 3 * n;
    for (size_t i = 0; i < n; ++i) {
        out[i * 4 + 0] = b0[i];
        out[i * 4 + 1] = b1[i];
        out[i * 4 + 2] = b2[i];
        out[i * 4 + 3] = b3[i];
    }
}

static int read_flash_bytes(uint32_t addr, uint8_t *dst, uint32_t size)
{
#if defined(BEKEN_PLATFORM) || defined(__arm__) || (defined(OPERATING_SYSTEM) && OPERATING_SYSTEM != SYSTEM_LINUX)
    OPERATE_RET ret = tkl_flash_read(addr, dst, size);
    if (ret != OPRT_OK) {
        PR_ERR("snt_assets: tkl_flash_read failed: addr=0x%08X, size=%u, ret=%d",
               (unsigned)addr, (unsigned)size, (int)ret);
        return SNT_EIO;
    }
    return SNT_OK;
#else
    FILE *f = fopen("examples/multimedia/sanotts_serial/models/chinese/snt_zh_assets.bin", "rb");
    if (!f) return SNT_EIO;
    fseek(f, addr - SNT_FLASH_MODEL_ADDR, SEEK_SET);
    size_t n = fread(dst, 1, size, f);
    fclose(f);
    return (n == size) ? SNT_OK : SNT_EIO;
#endif
}

int snt_assets_init(void)
{
    if (s_header_valid) {
        return SNT_OK;
    }

    int ret = read_flash_bytes(SNT_FLASH_MODEL_ADDR, (uint8_t *)&s_hdr, sizeof(s_hdr));
    if (ret != SNT_OK) {
        PR_ERR("snt_assets: failed to read Flash header at 0x%08X", (unsigned)SNT_FLASH_MODEL_ADDR);
        return ret;
    }

    if (s_hdr.magic != SNT_ASSET_MAGIC) {
        PR_ERR("snt_assets: invalid magic 0x%08X (expected 0x%08X)",
               (unsigned)s_hdr.magic, (unsigned)SNT_ASSET_MAGIC);
        return SNT_EIO;
    }

    PR_NOTICE("snt_assets: valid pack found at 0x%08X, ver=%u, entries=%u, total_sz=%u",
              (unsigned)SNT_FLASH_MODEL_ADDR, (unsigned)s_hdr.version,
              (unsigned)s_hdr.num_entries, (unsigned)s_hdr.total_size);
    s_header_valid = true;
    return SNT_OK;
}

static uint8_t s_inflate_chunk[4096];

static int inflate_stream(uint32_t flash_addr, uint32_t comp_size, uint8_t *out_buf, uint32_t orig_size)
{
    mz_stream stream;
    memset(&stream, 0, sizeof(stream));
    int status = mz_inflateInit(&stream);
    if (status != MZ_OK) return status;

    uint32_t bytes_left = comp_size;
    uint32_t cur_addr = flash_addr;

    stream.next_out = out_buf;
    stream.avail_out = orig_size;

    while (bytes_left > 0 && status != MZ_STREAM_END) {
        uint32_t to_read = (bytes_left > sizeof(s_inflate_chunk)) ? sizeof(s_inflate_chunk) : bytes_left;
        int r = read_flash_bytes(cur_addr, s_inflate_chunk, to_read);
        if (r != SNT_OK) {
            mz_inflateEnd(&stream);
            return -1;
        }
        cur_addr += to_read;
        bytes_left -= to_read;

        stream.next_in = s_inflate_chunk;
        stream.avail_in = to_read;

        status = mz_inflate(&stream, (bytes_left == 0) ? MZ_FINISH : MZ_NO_FLUSH);
        if (status != MZ_OK && status != MZ_STREAM_END) {
            mz_inflateEnd(&stream);
            return status;
        }
    }
    mz_inflateEnd(&stream);
    return (stream.total_out == orig_size) ? MZ_OK : -2;
}

int snt_assets_load_chinese(void *scratch, size_t scratch_size)
{
    if (s_chinese_ready) {
        return SNT_OK;
    }

    int init_ret = snt_assets_init();
    if (init_ret != SNT_OK) {
        return init_ret;
    }

    PR_NOTICE("snt_assets: decompressing %u Chinese assets to PSRAM...", (unsigned)s_hdr.num_entries);
    for (uint32_t i = 0; i < s_hdr.num_entries; ++i) {
        const snt_asset_entry_t *e = &s_hdr.entries[i];
        if (e->name[0] == '\0' || e->orig_size == 0) continue;

        uint8_t *final_buf = (uint8_t *)SNT_PSRAM_ALLOC(e->orig_size);
        if (!final_buf) {
            PR_ERR("snt_assets: out of memory allocating %u B for %s", (unsigned)e->orig_size, e->name);
            return SNT_ENOMEM;
        }

        if (e->flags & SNT_ASSET_FLAG_SHUFFLE_F32) {
            if (!scratch || scratch_size < e->orig_size) {
                PR_ERR("snt_assets: insufficient scratch (%zu < %u) for %s", scratch_size, (unsigned)e->orig_size, e->name);
                SNT_PSRAM_FREE(final_buf);
                return SNT_ENOMEM;
            }
            int r = inflate_stream(SNT_FLASH_MODEL_ADDR + e->offset, e->comp_size, (uint8_t *)scratch, e->orig_size);
            if (r != MZ_OK) {
                PR_ERR("snt_assets: inflate_stream error %d for %s", r, e->name);
                SNT_PSRAM_FREE(final_buf);
                return SNT_EIO;
            }
            unshuffle_f32((const uint8_t *)scratch, final_buf, e->orig_size);
        } else {
            int r = inflate_stream(SNT_FLASH_MODEL_ADDR + e->offset, e->comp_size, final_buf, e->orig_size);
            if (r != MZ_OK) {
                PR_ERR("snt_assets: inflate_stream error %d for %s", r, e->name);
                SNT_PSRAM_FREE(final_buf);
                return SNT_EIO;
            }
        }

        /* Check CRC32 */
        uint32_t calc_crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, final_buf, e->orig_size);
        if (calc_crc != e->crc32) {
            SNT_PSRAM_FREE(final_buf);
            PR_ERR("snt_assets: CRC mismatch for %s (0x%08X != 0x%08X)",
                   e->name, (unsigned)calc_crc, (unsigned)e->crc32);
            return SNT_EIO;
        }

        strncpy(s_loaded[s_n_loaded].name, e->name, sizeof(s_loaded[0].name) - 1);
        s_loaded[s_n_loaded].data = final_buf;
        s_loaded[s_n_loaded].size = e->orig_size;
        s_n_loaded++;

        PR_NOTICE("snt_assets: loaded %s (%u B, CRC 0x%08X OK)",
                  e->name, (unsigned)e->orig_size, (unsigned)e->crc32);
    }

    s_chinese_ready = true;
    PR_NOTICE("snt_assets: all %u Chinese assets successfully resident in PSRAM", (unsigned)s_n_loaded);
    return SNT_OK;
}

const void *snt_assets_get(const char *name, size_t *out_size)
{
    if (!name) return NULL;
    for (size_t i = 0; i < s_n_loaded; ++i) {
        if (strcmp(s_loaded[i].name, name) == 0) {
            if (out_size) *out_size = s_loaded[i].size;
            return s_loaded[i].data;
        }
    }
    return NULL;
}

bool snt_assets_is_chinese_ready(void)
{
    return s_chinese_ready;
}

bool snt_assets_is_resident(const void *ptr)
{
    if (!ptr) return false;
    for (size_t i = 0; i < s_n_loaded; ++i) {
        if (s_loaded[i].data == ptr) {
            return true;
        }
    }
    return false;
}

void snt_assets_deinit(void)
{
    for (size_t i = 0; i < s_n_loaded; ++i) {
        if (s_loaded[i].data) {
            SNT_PSRAM_FREE(s_loaded[i].data);
            s_loaded[i].data = NULL;
        }
    }
    s_n_loaded = 0;
    s_chinese_ready = false;
    s_header_valid = false;
}
