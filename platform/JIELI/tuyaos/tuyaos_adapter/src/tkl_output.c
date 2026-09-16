#include "tkl_output.h"
#include "tuya_error_code.h"

#include <stdarg.h>
#include <stdio.h>

void tkl_log_output(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

OPERATE_RET tkl_log_close(void)
{
    return OPRT_OK;
}

OPERATE_RET tkl_log_open(void)
{
    return OPRT_OK;
}
