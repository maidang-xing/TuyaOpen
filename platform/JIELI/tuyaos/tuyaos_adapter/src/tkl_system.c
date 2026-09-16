#include "tkl_init.h"
#include "tuya_error_code.h"

static TKL_ABILITY_T s_jieli_ability = {
    .uart = TRUE,
};

OPERATE_RET tkl_init(void)
{
    return OPRT_OK;
}

char *tkl_get_version(void)
{
    return "jieli-ac7916a-tkl-0.1.0";
}

TKL_ABILITY_T *tkl_get_ability(void)
{
    return &s_jieli_ability;
}
