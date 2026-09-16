#include "app_config.h"
#include "system/includes.h"
#include "os/os_api.h"

#include "tkl_init.h"

void tuya_app_main(void);

const struct irq_info irq_info_table[] = {
    { -1, -1, -1 },
};

const struct task_info task_info_table[] = {
    { "app_core", 15, 2048, 1024 },
    { "sys_event", 29, 512, 0 },
    { "systimer", 14, 256, 0 },
    { "sys_timer", 9, 512, 128 },
    { 0, 0, 0, 0 },
};

void app_main(void)
{
    (void)tkl_init();
    tuya_app_main();
}
