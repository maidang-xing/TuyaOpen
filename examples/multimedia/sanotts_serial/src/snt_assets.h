#ifndef __SNT_ASSETS_H__
#define __SNT_ASSETS_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_SNT_MODEL_ZH_PARTITION_OFFSET
#define SNT_FLASH_MODEL_ADDR CONFIG_SNT_MODEL_ZH_PARTITION_OFFSET
#else
#define SNT_FLASH_MODEL_ADDR 0x0028A000
#endif
#define SNT_ASSET_MAGIC      0x534E5431 /* "SNT1" */

#define SNT_ASSET_FLAG_SHUFFLE_F32 1
#define SNT_ASSET_FLAG_COMPRESSED   2

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

/**
 * @brief Initialize asset subsystem and verify Flash header/TOC.
 * @return 0 on success, negative error code on failure.
 */
int snt_assets_init(void);

/**
 * @brief Load all Chinese assets from Flash to PSRAM.
 * @param scratch Optional scratch buffer (e.g. from engine arena) for unshuffling.
 * @param scratch_size Size of scratch buffer in bytes.
 * @return 0 on success, negative error code on failure.
 */
int snt_assets_load_chinese(void *scratch, size_t scratch_size);

/**
 * @brief Query a decompressed asset buffer by name.
 * @param name File name (e.g. "front_f32.bin", "cmn_dict")
 * @param out_size Optional output pointer to received byte size.
 * @return Pointer to buffer in PSRAM, or NULL if not found.
 */
const void *snt_assets_get(const char *name, size_t *out_size);

/**
 * @brief Check if Chinese assets are loaded and ready in PSRAM.
 */
bool snt_assets_is_chinese_ready(void);

/**
 * @brief Check if a pointer points to one of the loaded resident asset buffers.
 */
bool snt_assets_is_resident(const void *ptr);

/**
 * @brief Free decompressed Chinese assets from PSRAM.
 */
void snt_assets_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __SNT_ASSETS_H__ */
