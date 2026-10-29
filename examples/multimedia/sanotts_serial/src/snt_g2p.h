#ifndef __SNT_G2P_H__
#define __SNT_G2P_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "snt_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 设置 eSpeak 字典及数据文件所在目录路径。
 * 默认路径：
 * - 嵌入式板端："/models/chinese"
 * - 主机测试：通过环境变量 SNT_DATA_DIR 指定或默认 "examples/multimedia/sanotts_serial/models/chinese"
 *
 * @param dir 数据目录绝对或相对路径
 * @return SNT_OK 成功；SNT_EINVAL 参数无效
 */
int snt_g2p_set_data_dir(const char *dir);

/**
 * 获取当前配置的数据目录路径。
 */
const char *snt_g2p_get_data_dir(void);

/**
 * 获取未知未映射字符统计（用于 M3 未知字符可定位审计）。
 */
int snt_g2p_get_unknown_symbol_count(void);

/**
 * 重置未知未映射字符统计。
 */
void snt_g2p_reset_unknown_symbol_count(void);

/**
 * 严格遵照 snt_contract.h 契约的公开接口：
 * - int snt_g2p_init(snt_voice_t voice);
 * - int snt_g2p_convert(const char *utf8, size_t bytes, int32_t *ids, size_t cap, size_t *used);
 * - int snt_g2p_foreach_segment(const char *utf8, size_t bytes, snt_ids_segment_cb cb, void *user);
 * - void snt_g2p_request_cancel(void);
 * - void snt_g2p_deinit(void);
 */

#ifdef __cplusplus
}
#endif

#endif /* __SNT_G2P_H__ */
