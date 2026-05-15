/**
 * @file buddy_sim_stubs.c
 * @brief Simulator-only stubs for transport and protocol symbols.
 *
 * Compiled only in the simulator CMake branch (CONFIG_LVGL_PC_SIMULATOR=y).
 * Provides no-op safe-return stubs so the simulator binary links cleanly
 * without the real buddy_ws / buddy_protocol network stack.
 */

#include "buddy_transport.h"
#include "buddy_protocol.h"
#include "buddy_types.h"
#include <string.h>

OPERATE_RET buddy_ws_init(void)                                        { return OPRT_OK; }
OPERATE_RET buddy_ws_start(void *data)                                 { (void)data; return OPRT_OK; }
OPERATE_RET buddy_ws_stop(void)                                        { return OPRT_OK; }
bool        buddy_ws_is_connected(void)                                { return false; }

OPERATE_RET buddy_ws_send_permission(const char *id, const char *d)   { (void)id; (void)d; return OPRT_OK; }
OPERATE_RET buddy_ws_send_asr(const char *t, const char *s)           { (void)t; (void)s; return OPRT_OK; }
OPERATE_RET buddy_ws_send_hb_req(const char *page)                    { (void)page; return OPRT_OK; }
OPERATE_RET buddy_ws_send_ack(const char *cmd)                        { (void)cmd; return OPRT_OK; }
OPERATE_RET buddy_ws_set_host(const char *host)                       { (void)host; return OPRT_OK; }

void buddy_state_snapshot(buddy_tama_state_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
}
