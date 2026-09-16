#include "tuyaopen_license.h"

OPERATE_RET tuyaopen_license_read(char **data, uint32_t *data_len)
{
    if (data != NULL) {
        *data = NULL;
    }
    if (data_len != NULL) {
        *data_len = 0;
    }
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tuyaopen_license_write(const char *data, uint32_t data_len)
{
    (void)data;
    (void)data_len;
    return OPRT_NOT_SUPPORTED;
}
