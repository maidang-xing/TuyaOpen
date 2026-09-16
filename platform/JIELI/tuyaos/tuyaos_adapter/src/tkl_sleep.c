#include "tkl_sleep.h"

#include "tal_system_port.h"
#include "tuya_error_code.h"

void tkl_system_sleep(uint32_t num_ms)
{
    tal_system_port_delay_ms(num_ms);
}

void tkl_system_delay(uint32_t num_ms)
{
    tal_system_port_delay_ms(num_ms);
}

OPERATE_RET tkl_cpu_sleep_mode_set(BOOL_T enable, TUYA_CPU_SLEEP_MODE_E mode)
{
    (void)enable;
    (void)mode;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_cpu_sleep_callback_register(TUYA_SLEEP_CB_T *sleep_cb)
{
    (void)sleep_cb;
    return OPRT_NOT_SUPPORTED;
}

void tkl_cpu_allow_sleep(void)
{
}

void tkl_cpu_force_wakeup(void)
{
}
