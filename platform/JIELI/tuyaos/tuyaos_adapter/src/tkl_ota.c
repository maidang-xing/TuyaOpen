#include "tkl_ota.h"
#include "tuya_error_code.h"

OPERATE_RET tkl_ota_get_ability(uint32_t *image_size, TUYA_OTA_TYPE_E *type)
{
    if (NULL == image_size || NULL == type) {
        return OPRT_INVALID_PARM;
    }
    *image_size = 512U * 1024U;
    *type = TUYA_OTA_FULL;
    return OPRT_OK;
}

OPERATE_RET tkl_ota_start_notify(uint32_t image_size, TUYA_OTA_TYPE_E type, TUYA_OTA_PATH_E path)
{
    (void)image_size;
    (void)type;
    (void)path;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_ota_data_process(TUYA_OTA_DATA_T *pack, uint32_t *remain_len)
{
    (void)pack;
    if (remain_len) {
        *remain_len = 0;
    }
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_ota_end_notify(BOOL_T reset)
{
    (void)reset;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_ota_get_old_firmware_info(TUYA_OTA_FIRMWARE_INFO_T **info)
{
    if (info) {
        *info = NULL;
    }
    return OPRT_NOT_SUPPORTED;
}
