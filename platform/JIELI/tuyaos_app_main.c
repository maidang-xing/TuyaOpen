#include "app_config.h"
#include "system/includes.h"
#include "os/os_api.h"

#include "tkl_init.h"
#include "tkl_output.h"

void example_jieli_uart_hello(void);

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

static void jieli_hello_task(void *arg)
{
    (void)arg;
    while (1) {
        os_time_dly(200);
    }
}

void app_main(void)
{
    tkl_init();
    tkl_log_output("TuyaOpen Jieli AC7916A\r\n");
    example_jieli_uart_hello();
    os_task_create(jieli_hello_task, NULL, 10, 1000, 0, "tuya_hello");
}
