#include "miniz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#define MAGIC 0x534E5431
#define FLAG_SHUFFLE_F32 1
#define FLAG_COMPRESSED   2

typedef struct __attribute__((packed)) {
    char name[32];
    uint32_t offset;
    uint32_t comp_size;
    uint32_t orig_size;
    uint32_t crc32;
    uint16_t flags;
    uint16_t reserved;
} snt_asset_entry_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t num_entries;
    uint32_t total_size;
    snt_asset_entry_t entries[16];
} snt_asset_header_t;

static void unshuffle_f32(const uint8_t *shuffled, uint8_t *out, size_t total_bytes)
{
    assert(total_bytes % 4 == 0);
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

int main(void)
{
    FILE *f = fopen("examples/multimedia/sanotts_serial/models/chinese/snt_zh_assets.bin", "rb");
    if (!f) {
        perror("fopen");
        return 1;
    }
    snt_asset_header_t hdr;
    if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        printf("Failed to read header\n");
        return 1;
    }
    printf("Magic: 0x%08X (expected 0x%08X), Entries: %u, Total size: %u\n",
           (unsigned)hdr.magic, (unsigned)MAGIC, (unsigned)hdr.num_entries, (unsigned)hdr.total_size);
    assert(hdr.magic == MAGIC);

    for (uint32_t i = 0; i < hdr.num_entries; ++i) {
        snt_asset_entry_t *e = &hdr.entries[i];
        printf("Checking entry %u: %s (comp=%u, orig=%u, flags=0x%04X, crc=0x%08X)... ",
               (unsigned)i, e->name, (unsigned)e->comp_size, (unsigned)e->orig_size,
               (unsigned)e->flags, (unsigned)e->crc32);

        uint8_t *comp_buf = (uint8_t *)malloc(e->comp_size);
        fseek(f, e->offset, SEEK_SET);
        if (fread(comp_buf, 1, e->comp_size, f) != e->comp_size) {
            printf("read failed\n");
            return 1;
        }

        uint8_t *decomp_buf = (uint8_t *)malloc(e->orig_size);
        mz_ulong dest_len = e->orig_size;
        int status = mz_uncompress(decomp_buf, &dest_len, comp_buf, e->comp_size);
        if (status != MZ_OK || dest_len != e->orig_size) {
            printf("uncompress failed: status=%d\n", status);
            return 1;
        }

        uint8_t *final_buf = decomp_buf;
        uint8_t *unshuf_buf = NULL;
        if (e->flags & FLAG_SHUFFLE_F32) {
            unshuf_buf = (uint8_t *)malloc(e->orig_size);
            unshuffle_f32(decomp_buf, unshuf_buf, e->orig_size);
            final_buf = unshuf_buf;
        }

        uint32_t calc_crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, final_buf, e->orig_size);
        if (calc_crc != e->crc32) {
            printf("CRC MISMATCH: 0x%08X != 0x%08X\n", (unsigned)calc_crc, (unsigned)e->crc32);
            return 1;
        }
        printf("PASS (bit-exact CRC matched)\n");

        free(comp_buf);
        free(decomp_buf);
        if (unshuf_buf) free(unshuf_buf);
    }

    fclose(f);
    printf("ALL CHINESE ASSETS VERIFIED 100%% BIT-EXACT!\n");
    return 0;
}
