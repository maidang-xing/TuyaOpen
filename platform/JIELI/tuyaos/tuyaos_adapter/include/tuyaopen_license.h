#ifndef TUYAOPEN_LICENSE_H
#define TUYAOPEN_LICENSE_H

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET tuyaopen_license_read(char **data, uint32_t *data_len);
OPERATE_RET tuyaopen_license_write(const char *data, uint32_t data_len);

#ifdef __cplusplus
}
#endif

#endif
