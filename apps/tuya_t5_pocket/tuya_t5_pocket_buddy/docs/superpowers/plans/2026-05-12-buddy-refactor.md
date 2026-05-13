# Tuya T5 Pocket Buddy Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor the Claude Code companion device firmware (BLE→WebSocket) and rewrite the PC-side plugin in Node.js/TypeScript, with fully decoupled transport/protocol/display layers.

**Architecture:** Device acts as WS client connecting to PC's WS server. Three-layer separation on device: transport (buddy_ws.c), protocol (buddy_protocol.c + buddy_state.c), display (LVGL screens reading state snapshots). Plugin side: Node.js daemon with HTTP hook server, event router, WS server, and permission bridge.

**Tech Stack:** C (TuyaOpen SDK, LVGL v9, cJSON, TAL APIs), TypeScript (Node.js, ws, http), WebSocket (RFC 6455)

**Reference files:** Branch `maidang/xb/claude_buddy_pocket` for all original code. Use `git show maidang/xb/claude_buddy_pocket:<path>` to read any reference file.

**Constraints:**
- All changes inside `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/`
- English comments, Chinese documentation
- Each milestone produces a summary doc in `docs/`

---

## File Map

### Device Firmware (C)

| File | Responsibility | Status |
|------|---------------|--------|
| `CMakeLists.txt` | Build config (simulator/embedded dual-mode) | Create |
| `Kconfig` | Feature toggles (CJK font, WS) | Create |
| `app_default.config` | Default build config | Create |
| `config/TUYA_T5AI_POCKET.config` | T5AI device config | Create |
| `config/TUYA_LINUX_LVGL_SIMULATOR.config` | PC simulator config | Create |
| `include/tuya_config.h` | Device auth credentials | Create |
| `include/buddy_types.h` | Shared data structures | Create |
| `include/buddy_protocol.h` | Protocol interface declarations | Create |
| `include/buddy_transport.h` | Transport interface declarations | Create |
| `include/app_display.h` | Display entry point | Create |
| `src/buddy_main.c` | Main entry, IoT init | Create |
| `src/input/buddy_indev.c` | Button/joystick input | Create |
| `src/transport/buddy_ws.h` | WS client header | Create |
| `src/transport/buddy_ws.c` | WS client implementation | Create |
| `src/protocol/buddy_protocol.c` | JSON frame parsing | Create |
| `src/protocol/buddy_state.c` | Mutex-protected state management | Create |
| `src/display/CMakeLists.txt` | Display module build | Create |
| `src/display/screen_manager.h` | Screen stack API | Create |
| `src/display/screen_manager.c` | Screen stack implementation | Create |
| `src/display/screens/startup_screen.h` | Startup screen header | Create |
| `src/display/screens/startup_screen.c` | Startup splash | Create |
| `src/display/screens/main_screen.h` | Main screen header | Create |
| `src/display/screens/main_screen.c` | Main screen (persona + sessions) | Create |
| `src/display/screens/approval_screen.h` | Approval screen header | Create |
| `src/display/screens/approval_screen.c` | Permission approval UI | Create |
| `src/display/screens/session_screen.h` | Session detail header | Create |
| `src/display/screens/session_screen.c` | Session detail view | Create |
| `src/display/screens/status_screen.h` | Status dashboard header | Create |
| `src/display/screens/status_screen.c` | Stats dashboard | Create |
| `src/display/screens/chart_screen.h` | Chart screen header | Create |
| `src/display/screens/chart_screen.c` | 28-day token chart | Create |
| `src/display/screens/pie_screen.h` | Pie screen header | Create |
| `src/display/screens/pie_screen.c` | Model distribution pie | Create |
| `src/display/widgets/status_bar.h` | Status bar header | Create |
| `src/display/widgets/status_bar.c` | Top status bar widget | Create |
| `src/display/widgets/led_indicator.h` | LED control header | Create |
| `src/display/widgets/led_indicator.c` | LED state machine | Create |
| `src/display/persona/persona_registry.h` | Persona registry header | Create |
| `src/display/persona/persona_registry.c` | 18-species registry | Create |
| `src/display/persona/persona_*.c` | 18 ASCII persona files | Create |
| `src/display/fonts/*.c` | Font resources (copy from ref) | Create |
| `src/media/media_pet.c` | Media/sound resources | Create |

### Plugin (Node.js/TypeScript)

| File | Responsibility | Status |
|------|---------------|--------|
| `tuya_pocket_buddy_plugin/package.json` | NPM manifest | Create |
| `tuya_pocket_buddy_plugin/tsconfig.json` | TypeScript config | Create |
| `tuya_pocket_buddy_plugin/.claude-plugin/plugin.json` | Plugin manifest | Create |
| `tuya_pocket_buddy_plugin/settings/hooks.json` | Claude Code hooks | Create |
| `tuya_pocket_buddy_plugin/scripts/hook_handler.js` | PreToolUse blocking handler | Create |
| `tuya_pocket_buddy_plugin/commands/buddy-install.md` | Install command | Create |
| `tuya_pocket_buddy_plugin/commands/buddy-start.md` | Start command | Create |
| `tuya_pocket_buddy_plugin/commands/buddy-stop.md` | Stop command | Create |
| `tuya_pocket_buddy_plugin/commands/buddy-status.md` | Status command | Create |
| `tuya_pocket_buddy_plugin/src/config.ts` | Configuration constants | Create |
| `tuya_pocket_buddy_plugin/src/wire.ts` | Protocol frame encoder | Create |
| `tuya_pocket_buddy_plugin/src/permissions.ts` | Approval bridge | Create |
| `tuya_pocket_buddy_plugin/src/hook-server.ts` | HTTP hook receiver | Create |
| `tuya_pocket_buddy_plugin/src/hook-router.ts` | Event routing + state aggregation | Create |
| `tuya_pocket_buddy_plugin/src/ws-server.ts` | WebSocket server | Create |
| `tuya_pocket_buddy_plugin/src/index.ts` | Daemon entry point | Create |

---

## Task 1: Project Skeleton (M1 - Build System)

**Files:**
- Create: `CMakeLists.txt`
- Create: `Kconfig`
- Create: `app_default.config`
- Create: `config/TUYA_T5AI_POCKET.config`
- Create: `config/TUYA_LINUX_LVGL_SIMULATOR.config`
- Create: `include/tuya_config.h`
- Create: `include/app_display.h`

- [ ] **Step 1: Create CMakeLists.txt**

```cmake
##
# @file CMakeLists.txt
# @brief Build configuration for Claude Buddy (WebSocket refactor)
#/

set(APP_PATH ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(APP_NAME ${APP_PATH} NAME)

if (CONFIG_LVGL_PC_SIMULATOR STREQUAL "y")
########################################
# Simulator mode: display only
########################################
add_library(${EXAMPLE_LIB})

target_compile_options(${EXAMPLE_LIB}
    PRIVATE
        "-DLV_LVGL_H_INCLUDE_SIMPLE"
)

target_include_directories(${EXAMPLE_LIB}
    PRIVATE
        ${APP_PATH}/include
)

add_subdirectory(${APP_PATH}/src/display)

else()
########################################
# Embedded mode: full build
########################################

set(APP_SRCS)
aux_source_directory(${APP_PATH}/src APP_SRCS)
aux_source_directory(${APP_PATH}/src/input INPUT_SRCS)
aux_source_directory(${APP_PATH}/src/transport TRANSPORT_SRCS)
aux_source_directory(${APP_PATH}/src/protocol PROTOCOL_SRCS)
aux_source_directory(${APP_PATH}/src/media MEDIA_SRCS)

list(APPEND APP_SRCS ${INPUT_SRCS} ${TRANSPORT_SRCS} ${PROTOCOL_SRCS} ${MEDIA_SRCS})

set(APP_INC
    ${APP_PATH}/include
    ${APP_PATH}/src/transport
    ${APP_PATH}/src/protocol
)

add_library(${EXAMPLE_LIB})

target_sources(${EXAMPLE_LIB}
    PRIVATE
        ${APP_SRCS}
)

target_include_directories(${EXAMPLE_LIB}
    PRIVATE
        ${APP_INC}
)

target_compile_options(${EXAMPLE_LIB}
    PRIVATE
        "-DLV_LVGL_H_INCLUDE_SIMPLE"
)

add_subdirectory(${APP_PATH}/src/display)
add_subdirectory(${APP_PATH}/../../tuya.ai/ai_components)
target_include_directories(${EXAMPLE_LIB} PRIVATE ${APP_PATH}/../../tuya.ai/ai_components)

endif()
```

- [ ] **Step 2: Create Kconfig**

```kconfig
menu "Tuya T5 Pocket Buddy"

config DISABLE_BUDDY_CJK_FONT
    bool "Disable CJK font (faster compile)"
    default n
    help
        Disables the large CJK bitmap font to speed up compilation
        during development. Turn this OFF for production builds.

endmenu
```

- [ ] **Step 3: Create app_default.config**

```
CONFIG_PROJECT_VERSION="0.0.1"
# CONFIG_ENABLE_COMP_AI_MODE_ONESHOT is not set
# CONFIG_ENABLE_COMP_AI_MODE_WAKEUP is not set
# CONFIG_ENABLE_COMP_AI_MODE_FREE is not set
# CONFIG_ENABLE_AI_UI_ICON_FONT is not set
CONFIG_BOARD_CHOICE_T5AI=y
CONFIG_BOARD_CHOICE_TUYA_T5AI_POCKET=y
CONFIG_ENABLE_LIBLVGL=y
CONFIG_BUTTON_NAME="btn_menu"
CONFIG_BUTTON_NAME_2="btn_enter"
CONFIG_BUTTON_NAME_3="btn_esc"
CONFIG_BUTTON_NAME_4="ai_chat_button"
```

- [ ] **Step 4: Create config/TUYA_T5AI_POCKET.config**

Same content as `app_default.config`.

- [ ] **Step 5: Create config/TUYA_LINUX_LVGL_SIMULATOR.config**

```
CONFIG_PROJECT_VERSION="0.0.1"
CONFIG_BOARD_CHOICE_LINUX=y
CONFIG_BOARD_CHOICE_UBUNTU=y
CONFIG_ENABLE_LIBLVGL=y
CONFIG_LVGL_PC_SIMULATOR=y
```

- [ ] **Step 6: Create include/tuya_config.h**

Copy from reference branch verbatim:
```bash
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/include/tuya_config.h > include/tuya_config.h
```

- [ ] **Step 7: Create include/app_display.h**

```c
/**
 * @file app_display.h
 * @brief Display system entry point for Claude Buddy.
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __APP_DISPLAY_H__
#define __APP_DISPLAY_H__

#include "tuya_cloud_types.h"
#include "screen_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET ai_ui_chat_register(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_DISPLAY_H__ */
```

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt Kconfig app_default.config config/ include/tuya_config.h include/app_display.h
git commit -m "feat(buddy): add project skeleton with build system"
```

---

## Task 2: Shared Types (M1 - Data Structures)

**Files:**
- Create: `include/buddy_types.h`
- Create: `include/buddy_protocol.h`
- Create: `include/buddy_transport.h`

- [ ] **Step 1: Create include/buddy_types.h**

```c
/**
 * @file buddy_types.h
 * @brief Shared data structures for Claude Buddy.
 * @copyright Copyright (c) 2024-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef BUDDY_TYPES_H
#define BUDDY_TYPES_H

#include "tuya_cloud_types.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUDDY_ENTRIES_RING       8
#define BUDDY_ENTRY_NAME_LEN     24
#define BUDDY_ENTRY_HINT_LEN     48
#define BUDDY_SESSIONS_MAX       12
#define BUDDY_SESSION_NAME_LEN   32
#define BUDDY_SESSION_ENTRIES    4
#define BUDDY_MODEL_LEN          15
#define BUDDY_MSTATS_MAX         4
#define BUDDY_DAILY_HISTORY      28

#define BUDDY_WS_DEFAULT_PORT    7681
#define BUDDY_WS_PATH            "/buddy"
#define BUDDY_WS_HOST_LEN        64

typedef struct {
    char type;
    char name[BUDDY_ENTRY_NAME_LEN + 1];
    char hint[BUDDY_ENTRY_HINT_LEN + 1];
} buddy_entry_t;

typedef struct {
    char     sid[16];
    char     name[BUDDY_SESSION_NAME_LEN + 1];
    char     model[BUDDY_MODEL_LEN + 1];
    uint32_t tokens;
    char     project[16];
    bool     is_running;
    char     local_entries[BUDDY_SESSION_ENTRIES][BUDDY_ENTRY_NAME_LEN + 1];
    uint8_t  local_entries_count;
} buddy_session_t;

typedef struct {
    char     model[BUDDY_MODEL_LEN + 1];
    uint32_t tokens;
} buddy_mstat_t;

typedef enum {
    PERSONA_SLEEP = 0,
    PERSONA_IDLE,
    PERSONA_BUSY,
    PERSONA_ATTENTION,
    PERSONA_CELEBRATE,
    PERSONA_HEART,
    PERSONA_DIZZY,
} buddy_persona_state_e;

typedef enum {
    LED_OFF = 0,
    LED_ON_DIM,
    LED_BLINK_SLOW,
    LED_BLINK_FAST,
    LED_FLASH_ONCE,
} buddy_led_state_e;

typedef struct {
    bool     ws_connected;

    uint8_t  sessions_total;
    uint8_t  sessions_running;
    uint8_t  sessions_waiting;

    uint32_t tokens;
    uint32_t tokens_today;
    uint32_t tokens_in;
    uint32_t tokens_in_today;
    uint32_t cache_read;
    uint32_t cache_write;
    uint32_t ctx_used;
    uint32_t ctx_total;

    char     msg[64];
    char     owner_name[32];
    char     model[BUDDY_MODEL_LEN + 1];
    char     claude_version[20];
    uint32_t cost_today_ucc;
    uint32_t cost_total_ucc;

    bool     has_prompt;
    char     prompt_id[40];
    char     prompt_tool[32];
    char     prompt_hint[64];

    buddy_entry_t entries[BUDDY_ENTRIES_RING];
    uint8_t       entries_count;
    uint8_t       entries_head;

    buddy_session_t sessions[BUDDY_SESSIONS_MAX];
    uint8_t         sessions_count;

    buddy_mstat_t mstats[BUDDY_MSTATS_MAX];
    uint8_t       mstats_count;

    uint32_t daily_tokens[BUDDY_DAILY_HISTORY];

    int64_t  wall_epoch_s;
    int16_t  wall_tz_min;
    uint64_t wall_local_ms_at_rx;

    uint8_t               persona_id;
    buddy_persona_state_e persona_state;
    buddy_led_state_e     led_state;
} buddy_tama_state_t;

#ifdef __cplusplus
}
#endif

#endif /* BUDDY_TYPES_H */
```

- [ ] **Step 2: Create include/buddy_protocol.h**

```c
/**
 * @file buddy_protocol.h
 * @brief Protocol layer interface for JSON frame parsing and state management.
 * @copyright Copyright (c) 2024-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef BUDDY_PROTOCOL_H
#define BUDDY_PROTOCOL_H

#include "buddy_types.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET buddy_protocol_init(void);
void        buddy_protocol_on_recv(const char *json_str);

OPERATE_RET buddy_state_init(void);
void        buddy_state_snapshot(buddy_tama_state_t *out);
void        buddy_state_set_connected(bool connected);
void        buddy_state_update_from_heartbeat(void *cjson_root);
void        buddy_state_update_time(int64_t epoch_s, int16_t tz_min);
void        buddy_state_set_owner(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* BUDDY_PROTOCOL_H */
```

- [ ] **Step 3: Create include/buddy_transport.h**

```c
/**
 * @file buddy_transport.h
 * @brief Transport layer interface for WebSocket communication.
 * @copyright Copyright (c) 2024-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef BUDDY_TRANSPORT_H
#define BUDDY_TRANSPORT_H

#include "buddy_types.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET buddy_ws_init(void);
OPERATE_RET buddy_ws_start(void);
OPERATE_RET buddy_ws_stop(void);
bool        buddy_ws_is_connected(void);

OPERATE_RET buddy_ws_send_permission(const char *id, const char *decision);
OPERATE_RET buddy_ws_send_asr(const char *text, const char *sid);
OPERATE_RET buddy_ws_send_hb_req(const char *page);
OPERATE_RET buddy_ws_send_ack(const char *cmd);

#ifdef __cplusplus
}
#endif

#endif /* BUDDY_TRANSPORT_H */
```

- [ ] **Step 4: Commit**

```bash
git add include/buddy_types.h include/buddy_protocol.h include/buddy_transport.h
git commit -m "feat(buddy): add shared type definitions and interface headers"
```

---

## Task 3: Main Entry + Input (M1 - Application Bootstrap)

**Files:**
- Create: `src/buddy_main.c`
- Create: `src/input/buddy_indev.c`

- [ ] **Step 1: Create src/buddy_main.c**

Adapt from reference `buddy_main.c`. Key changes: replace `buddy_ble_init/start` with `buddy_ws_init/start`, remove BLE-specific includes.

```c
/**
 * @file buddy_main.c
 * @brief Claude Buddy standalone project entry point.
 *
 * Initializes Tuya IoT stack, hardware, and WebSocket transport.
 * On MQTT connected, starts WS connection to PC-side plugin.
 *
 * @copyright Copyright (c) 2025-2026 Tuya Inc. All Rights Reserved.
 */

#include "tuya_cloud_types.h"

#include <assert.h>
#include "cJSON.h"
#include "tal_api.h"
#include "tuya_config.h"
#include "tuya_iot.h"
#include "tuya_iot_dp.h"
#include "netmgr.h"
#include "tkl_output.h"
#include "tal_cli.h"
#include "tuya_authorize.h"
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
#include "netconn_wifi.h"
#endif
#if defined(ENABLE_WIRED) && (ENABLE_WIRED == 1)
#include "netconn_wired.h"
#endif
#if defined(ENABLE_LIBLWIP) && (ENABLE_LIBLWIP == 1)
#include "lwip_init.h"
#endif

#include "board_com_api.h"
#include "buddy_transport.h"
#include "buddy_protocol.h"

tuya_iot_client_t ai_client;
tuya_iot_license_t license;

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "1.0.0"
#endif

STATIC VOID_T user_log_output_cb(const char *str)
{
    tal_uart_write(TUYA_UART_NUM_0, (const uint8_t *)str, strlen(str));
}

STATIC VOID_T user_upgrade_notify_on(tuya_iot_client_t *client, cJSON *upgrade)
{
    PR_INFO("----- Upgrade information -----");
    if (!upgrade) {
        PR_WARN("upgrade JSON is NULL");
        return;
    }
    cJSON *version_item = cJSON_GetObjectItem(upgrade, "version");
    PR_INFO("Version: %s", cJSON_IsString(version_item) ? version_item->valuestring : "N/A");
}

STATIC VOID_T user_event_handler_on(tuya_iot_client_t *client, tuya_event_msg_t *event)
{
    PR_DEBUG("Tuya Event ID:%d(%s)", event->id, EVENT_ID2STR(event->id));

    switch (event->id) {
    case TUYA_EVENT_BIND_START:
        PR_INFO("Device Bind Start!");
        break;

    case TUYA_EVENT_MQTT_CONNECTED: {
        PR_INFO("Device MQTT Connected!");
        STATIC uint8_t first = 1;
        if (first) {
            first = 0;
            OPERATE_RET rt = buddy_ws_start();
            if (rt != OPRT_OK) {
                PR_WARN("buddy_ws_start failed rt=%d", rt);
            }
        }
    } break;

    case TUYA_EVENT_MQTT_DISCONNECT:
        PR_INFO("Device MQTT DisConnected!");
        break;

    case TUYA_EVENT_UPGRADE_NOTIFY:
        user_upgrade_notify_on(client, event->value.asJSON);
        break;

    case TUYA_EVENT_TIMESTAMP_SYNC:
        tal_event_publish("app.time.sync", NULL);
        break;

    case TUYA_EVENT_RESET:
        PR_INFO("Device Reset:%d", (int)event->value.asInteger);
        break;

    case TUYA_EVENT_RESET_COMPLETE:
        PR_INFO("Device Reset Complete!");
        tal_system_reset();
        break;

    case TUYA_EVENT_DP_RECEIVE_OBJ: {
        dp_obj_recv_t *dpobj = event->value.dpobj;
        tuya_iot_dp_obj_report(client, dpobj->devid, dpobj->dps, dpobj->dpscnt, 0);
    } break;

    case TUYA_EVENT_DP_RECEIVE_RAW: {
        dp_raw_recv_t *dpraw = event->value.dpraw;
        tuya_iot_dp_raw_report(client, dpraw->devid, &dpraw->dp, 3);
    } break;

    default:
        break;
    }
}

STATIC bool user_network_check(void)
{
    netmgr_status_e status = NETMGR_LINK_DOWN;
    netmgr_conn_get(NETCONN_AUTO, NETCONN_CMD_STATUS, &status);
    return status != NETMGR_LINK_DOWN;
}

void user_main(void)
{
    int ret = OPRT_OK;

#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    cJSON_InitHooks(&(cJSON_Hooks){.malloc_fn = tal_psram_malloc, .free_fn = tal_psram_free});
#else
    cJSON_InitHooks(&(cJSON_Hooks){.malloc_fn = tal_malloc, .free_fn = tal_free});
#endif

    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, (TAL_LOG_OUTPUT_CB)tkl_log_output);

    PR_NOTICE("Application information:");
    PR_NOTICE("Project name:        %s", PROJECT_NAME);
    PR_NOTICE("App version:         %s", PROJECT_VERSION);
    PR_NOTICE("Compile time:        %s", __DATE__);
    PR_NOTICE("TuyaOpen version:    %s", OPEN_VERSION);

    tal_kv_init(&(tal_kv_cfg_t){
        .seed = "vmlkasdh93dlvlcy",
        .key  = "dflfuap134ddlduq",
    });
    tal_sw_timer_init();
    tal_workq_init();
    tal_time_service_init();
    tal_cli_init();
    tuya_authorize_init();

    ret = board_register_hardware();
    if (ret != OPRT_OK) {
        PR_ERR("board_register_hardware failed");
    }

    if (OPRT_OK != tuya_authorize_read(&license)) {
        license.uuid    = TUYA_OPENSDK_UUID;
        license.authkey = TUYA_OPENSDK_AUTHKEY;
        PR_WARN("Replace UUID and AUTHKEY in tuya_config.h");
    }

    ret = tuya_iot_init(&ai_client, &(const tuya_iot_config_t){
                                        .software_ver  = PROJECT_VERSION,
                                        .productkey    = TUYA_PRODUCT_ID,
                                        .uuid          = license.uuid,
                                        .authkey       = license.authkey,
                                        .event_handler = user_event_handler_on,
                                        .network_check = user_network_check,
                                    });
    assert(ret == OPRT_OK);

#if defined(ENABLE_LIBLWIP) && (ENABLE_LIBLWIP == 1)
    TUYA_LwIP_Init();
#endif

    netmgr_type_e type = 0;
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
    type |= NETCONN_WIFI;
#endif
#if defined(ENABLE_WIRED) && (ENABLE_WIRED == 1)
    type |= NETCONN_WIRED;
#endif
    netmgr_init(type);
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
    netmgr_conn_set(NETCONN_WIFI, NETCONN_CMD_NETCFG,
                    &(netcfg_args_t){.type = NETCFG_TUYA_BLE | NETCFG_TUYA_WIFI_AP});
#endif

    tuya_iot_start(&ai_client);

    buddy_state_init();
    buddy_protocol_init();
    buddy_ws_init();

    tkl_wifi_set_lp_mode(0, 0);

    for (;;) {
        tuya_iot_yield(&ai_client);
    }
}

#if OPERATING_SYSTEM == SYSTEM_LINUX
void main(int argc, char *argv[])
{
    user_main();
}
#else
STATIC THREAD_HANDLE ty_app_thread = NULL;

STATIC VOID_T tuya_app_thread_fn(VOID_T *arg)
{
    user_main();
    tal_thread_delete(ty_app_thread);
    ty_app_thread = NULL;
}

void tuya_app_main(void)
{
    THREAD_CFG_T thrd_param = {0};
    thrd_param.stackDepth   = 1024 * 24;
    thrd_param.priority     = THREAD_PRIO_1;
    thrd_param.thrdname     = "tuya_app_main";
    tal_thread_create_and_start(&ty_app_thread, NULL, NULL, tuya_app_thread_fn, NULL, &thrd_param);
}
#endif
```

- [ ] **Step 2: Create src/input/buddy_indev.c**

Copy verbatim from reference branch:
```bash
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/buddy_indev.c > src/input/buddy_indev.c
```

- [ ] **Step 3: Commit**

```bash
git add src/buddy_main.c src/input/buddy_indev.c
git commit -m "feat(buddy): add main entry point and input device handler"
```

---

## Task 4: State Management (M2 - Protocol Layer)

**Files:**
- Create: `src/protocol/buddy_state.c`
- Create: `src/protocol/buddy_protocol.c`

- [ ] **Step 1: Create src/protocol/buddy_state.c**

```c
/**
 * @file buddy_state.c
 * @brief Mutex-protected shared state for Claude Buddy.
 *
 * Transport layer writes state under mutex lock.
 * Display layer reads atomic snapshots.
 *
 * @copyright Copyright (c) 2024-2026 Tuya Inc. All Rights Reserved.
 */

#include "buddy_protocol.h"
#include "tal_mutex.h"
#include "tal_system.h"
#include <string.h>

static buddy_tama_state_t s_state;
static MUTEX_HANDLE        s_mutex = NULL;

OPERATE_RET buddy_state_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    if (!s_mutex) {
        return tal_mutex_create_init(&s_mutex);
    }
    return OPRT_OK;
}

void buddy_state_snapshot(buddy_tama_state_t *out)
{
    if (!out || !s_mutex) {
        return;
    }
    tal_mutex_lock(s_mutex);
    memcpy(out, &s_state, sizeof(buddy_tama_state_t));
    tal_mutex_unlock(s_mutex);
}

void buddy_state_set_connected(bool connected)
{
    if (!s_mutex) return;
    tal_mutex_lock(s_mutex);
    s_state.ws_connected = connected;
    tal_mutex_unlock(s_mutex);
}

void buddy_state_set_owner(const char *name)
{
    if (!s_mutex || !name) return;
    tal_mutex_lock(s_mutex);
    strncpy(s_state.owner_name, name, sizeof(s_state.owner_name) - 1);
    s_state.owner_name[sizeof(s_state.owner_name) - 1] = '\0';
    tal_mutex_unlock(s_mutex);
}

void buddy_state_update_time(int64_t epoch_s, int16_t tz_min)
{
    if (!s_mutex) return;
    tal_mutex_lock(s_mutex);
    s_state.wall_epoch_s      = epoch_s;
    s_state.wall_tz_min       = tz_min;
    s_state.wall_local_ms_at_rx = tal_system_get_millisecond();
    tal_mutex_unlock(s_mutex);
}

void buddy_state_update_from_heartbeat(void *cjson_root)
{
    cJSON *root = (cJSON *)cjson_root;
    if (!s_mutex || !root) return;

    tal_mutex_lock(s_mutex);

    cJSON *item;

    #define GET_U8(field, key) \
        if ((item = cJSON_GetObjectItem(root, key)) && cJSON_IsNumber(item)) \
            s_state.field = (uint8_t)item->valueint;
    #define GET_U32(field, key) \
        if ((item = cJSON_GetObjectItem(root, key)) && cJSON_IsNumber(item)) \
            s_state.field = (uint32_t)item->valuedouble;
    #define GET_STR(field, key, maxlen) \
        if ((item = cJSON_GetObjectItem(root, key)) && cJSON_IsString(item)) { \
            strncpy(s_state.field, item->valuestring, maxlen); \
            s_state.field[maxlen] = '\0'; \
        }

    GET_U8(sessions_total, "total");
    GET_U8(sessions_running, "running");
    GET_U8(sessions_waiting, "waiting");
    GET_U32(tokens, "tokens");
    GET_U32(tokens_today, "tokens_today");
    GET_U32(tokens_in, "tokens_in");
    GET_U32(tokens_in_today, "tokens_in_today");
    GET_U32(cache_read, "cache_read");
    GET_U32(cache_write, "cache_write");
    GET_U32(ctx_used, "ctx_used");
    GET_U32(ctx_total, "ctx_total");
    GET_U32(cost_today_ucc, "cost_td");
    GET_U32(cost_total_ucc, "cost_all");
    GET_STR(model, "model", BUDDY_MODEL_LEN);
    GET_STR(claude_version, "ver", sizeof(s_state.claude_version) - 1);

    /* entries[] */
    cJSON *entries = cJSON_GetObjectItem(root, "entries");
    if (entries && cJSON_IsArray(entries)) {
        int count = cJSON_GetArraySize(entries);
        if (count > BUDDY_ENTRIES_RING) count = BUDDY_ENTRIES_RING;
        s_state.entries_count = (uint8_t)count;
        s_state.entries_head  = 0;
        for (int i = 0; i < count; i++) {
            cJSON *e = cJSON_GetArrayItem(entries, i);
            cJSON *t = cJSON_GetObjectItem(e, "t");
            cJSON *n = cJSON_GetObjectItem(e, "n");
            cJSON *h = cJSON_GetObjectItem(e, "h");
            s_state.entries[i].type = (t && cJSON_IsString(t) && t->valuestring[0]) ? t->valuestring[0] : '?';
            if (n && cJSON_IsString(n)) {
                strncpy(s_state.entries[i].name, n->valuestring, BUDDY_ENTRY_NAME_LEN);
                s_state.entries[i].name[BUDDY_ENTRY_NAME_LEN] = '\0';
            }
            if (h && cJSON_IsString(h)) {
                strncpy(s_state.entries[i].hint, h->valuestring, BUDDY_ENTRY_HINT_LEN);
                s_state.entries[i].hint[BUDDY_ENTRY_HINT_LEN] = '\0';
            }
        }
    }

    /* sessions[] */
    cJSON *sessions = cJSON_GetObjectItem(root, "sessions");
    if (sessions && cJSON_IsArray(sessions)) {
        int count = cJSON_GetArraySize(sessions);
        if (count > BUDDY_SESSIONS_MAX) count = BUDDY_SESSIONS_MAX;
        s_state.sessions_count = (uint8_t)count;
        for (int i = 0; i < count; i++) {
            cJSON *s = cJSON_GetArrayItem(sessions, i);
            buddy_session_t *ss = &s_state.sessions[i];
            memset(ss, 0, sizeof(*ss));
            GET_STR(sessions[i].sid, "sid", sizeof(ss->sid) - 1);
            GET_STR(sessions[i].name, "name", BUDDY_SESSION_NAME_LEN);
            GET_STR(sessions[i].model, "model", BUDDY_MODEL_LEN);
            GET_STR(sessions[i].project, "proj", sizeof(ss->project) - 1);

            cJSON *tok = cJSON_GetObjectItem(s, "tok");
            if (tok && cJSON_IsNumber(tok)) ss->tokens = (uint32_t)tok->valuedouble;

            cJSON *run = cJSON_GetObjectItem(s, "run");
            if (run) ss->is_running = cJSON_IsTrue(run);

            cJSON *ent = cJSON_GetObjectItem(s, "ent");
            if (ent && cJSON_IsArray(ent)) {
                int ec = cJSON_GetArraySize(ent);
                if (ec > BUDDY_SESSION_ENTRIES) ec = BUDDY_SESSION_ENTRIES;
                ss->local_entries_count = (uint8_t)ec;
                for (int j = 0; j < ec; j++) {
                    cJSON *ei = cJSON_GetArrayItem(ent, j);
                    if (ei && cJSON_IsString(ei)) {
                        strncpy(ss->local_entries[j], ei->valuestring, BUDDY_ENTRY_NAME_LEN);
                        ss->local_entries[j][BUDDY_ENTRY_NAME_LEN] = '\0';
                    }
                }
            }
        }
    }

    /* mstats[] */
    cJSON *mstats = cJSON_GetObjectItem(root, "mstats");
    if (mstats && cJSON_IsArray(mstats)) {
        int count = cJSON_GetArraySize(mstats);
        if (count > BUDDY_MSTATS_MAX) count = BUDDY_MSTATS_MAX;
        s_state.mstats_count = (uint8_t)count;
        for (int i = 0; i < count; i++) {
            cJSON *ms = cJSON_GetArrayItem(mstats, i);
            cJSON *m  = cJSON_GetObjectItem(ms, "m");
            cJSON *tk = cJSON_GetObjectItem(ms, "tok");
            if (m && cJSON_IsString(m)) {
                strncpy(s_state.mstats[i].model, m->valuestring, BUDDY_MODEL_LEN);
                s_state.mstats[i].model[BUDDY_MODEL_LEN] = '\0';
            }
            if (tk && cJSON_IsNumber(tk)) {
                s_state.mstats[i].tokens = (uint32_t)tk->valuedouble;
            }
        }
    }

    /* daily[] */
    cJSON *daily = cJSON_GetObjectItem(root, "daily");
    if (daily && cJSON_IsArray(daily)) {
        int count = cJSON_GetArraySize(daily);
        if (count > BUDDY_DAILY_HISTORY) count = BUDDY_DAILY_HISTORY;
        for (int i = 0; i < count; i++) {
            cJSON *d = cJSON_GetArrayItem(daily, i);
            if (d && cJSON_IsNumber(d)) {
                s_state.daily_tokens[i] = (uint32_t)d->valuedouble;
            }
        }
    }

    /* prompt */
    cJSON *prompt = cJSON_GetObjectItem(root, "prompt");
    if (prompt && cJSON_IsObject(prompt)) {
        s_state.has_prompt = true;
        GET_STR(prompt_id, "id", sizeof(s_state.prompt_id) - 1);
        GET_STR(prompt_tool, "tool", sizeof(s_state.prompt_tool) - 1);
        GET_STR(prompt_hint, "hint", sizeof(s_state.prompt_hint) - 1);
    } else {
        s_state.has_prompt = false;
        s_state.prompt_id[0]   = '\0';
        s_state.prompt_tool[0] = '\0';
        s_state.prompt_hint[0] = '\0';
    }

    /* time */
    cJSON *time_arr = cJSON_GetObjectItem(root, "time");
    if (time_arr && cJSON_IsArray(time_arr) && cJSON_GetArraySize(time_arr) >= 2) {
        s_state.wall_epoch_s      = (int64_t)cJSON_GetArrayItem(time_arr, 0)->valuedouble;
        s_state.wall_tz_min       = (int16_t)cJSON_GetArrayItem(time_arr, 1)->valueint;
        s_state.wall_local_ms_at_rx = tal_system_get_millisecond();
    }

    #undef GET_U8
    #undef GET_U32
    #undef GET_STR

    tal_mutex_unlock(s_mutex);
}
```

- [ ] **Step 2: Create src/protocol/buddy_protocol.c**

```c
/**
 * @file buddy_protocol.c
 * @brief JSON frame dispatcher for Claude Buddy protocol.
 * @copyright Copyright (c) 2024-2026 Tuya Inc. All Rights Reserved.
 */

#include "buddy_protocol.h"
#include "cJSON.h"
#include "tal_log.h"

#define TAG "buddy_proto"

OPERATE_RET buddy_protocol_init(void)
{
    PR_INFO("buddy_protocol_init");
    return OPRT_OK;
}

void buddy_protocol_on_recv(const char *json_str)
{
    if (!json_str || !json_str[0]) return;

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        PR_WARN("buddy_protocol: invalid JSON");
        return;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    if (cmd && cJSON_IsString(cmd)) {
        if (strcmp(cmd->valuestring, "owner") == 0) {
            cJSON *name = cJSON_GetObjectItem(root, "name");
            if (name && cJSON_IsString(name)) {
                buddy_state_set_owner(name->valuestring);
            }
        } else if (strcmp(cmd->valuestring, "status") == 0) {
            /* PC requesting device status — handled by transport layer */
        }
    } else {
        /* No "cmd" field — treat as heartbeat */
        buddy_state_update_from_heartbeat(root);
    }

    cJSON_Delete(root);
}
```

- [ ] **Step 3: Commit**

```bash
git add src/protocol/buddy_state.c src/protocol/buddy_protocol.c
git commit -m "feat(buddy): add protocol parser and mutex-protected state management"
```

---

## Task 5: WebSocket Client (M2 - Transport Layer)

**Files:**
- Create: `src/transport/buddy_ws.h`
- Create: `src/transport/buddy_ws.c`

- [ ] **Step 1: Create src/transport/buddy_ws.h**

```c
/**
 * @file buddy_ws.h
 * @brief WebSocket client for Claude Buddy.
 * @copyright Copyright (c) 2024-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef BUDDY_WS_H
#define BUDDY_WS_H

#include "buddy_transport.h"

/* CLI registration for "buddy ws set/status" commands */
void buddy_ws_cli_register(void);

#endif /* BUDDY_WS_H */
```

- [ ] **Step 2: Create src/transport/buddy_ws.c**

This is the largest device-side file. It implements:
1. WS client handshake (client-side: send Upgrade request, validate server accept key)
2. WS frame encode with masking (RFC 6455 client MUST mask)
3. WS frame decode (server frames are unmasked)
4. TCP connect + recv thread
5. Auto-reconnect with exponential backoff
6. CLI commands for IP configuration
7. KV persistence of host/port

Reference the WS frame handling from `apps/mimiclaw/gateway/ws_server.c` (lines 157-352 for frame encode/decode) but adapt for client direction (add masking on send, don't expect masking on recv).

```c
/**
 * @file buddy_ws.c
 * @brief WebSocket client transport for Claude Buddy.
 *
 * Implements RFC 6455 WebSocket client connecting to PC-side plugin.
 * Handles handshake, frame encode/decode, auto-reconnect, and CLI config.
 *
 * Reference: apps/mimiclaw/gateway/ws_server.c for WS frame handling.
 *
 * @copyright Copyright (c) 2024-2026 Tuya Inc. All Rights Reserved.
 */

#include "buddy_ws.h"
#include "buddy_protocol.h"
#include "buddy_types.h"

#include "cJSON.h"
#include "tal_api.h"
#include "tal_network.h"
#include "tal_hash.h"
#include "tal_cli.h"
#include "tal_kv.h"

#include <string.h>
#include <stdlib.h>

#define TAG "buddy_ws"

#define WS_RX_BUF_SIZE    8192
#define WS_TX_BUF_SIZE    4096
#define WS_RECONNECT_MIN  5000
#define WS_RECONNECT_MAX  60000
#define WS_STACK_SIZE      (8 * 1024)

#define KV_KEY_HOST "buddy_ws_host"
#define KV_KEY_PORT "buddy_ws_port"

static THREAD_HANDLE   s_ws_thread   = NULL;
static volatile BOOL_T s_ws_running  = FALSE;
static volatile BOOL_T s_ws_connected = FALSE;
static int             s_ws_fd       = -1;
static MUTEX_HANDLE    s_ws_tx_mutex = NULL;

static char     s_host[BUDDY_WS_HOST_LEN + 1] = {0};
static uint16_t s_port = BUDDY_WS_DEFAULT_PORT;

static uint8_t  s_rx_buf[WS_RX_BUF_SIZE];
static size_t   s_rx_len = 0;

/* ---- WS frame helpers (adapted from mimiclaw/ws_server.c) ---- */

static OPERATE_RET __send_all(int fd, const uint8_t *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        int n = tal_net_send(fd, buf + sent, (uint32_t)(len - sent));
        if (n == OPRT_RESOURCE_NOT_READY) {
            tal_system_sleep(5);
            continue;
        }
        if (n <= 0) return OPRT_SEND_ERR;
        sent += (size_t)n;
    }
    return OPRT_OK;
}

static OPERATE_RET __send_ws_frame(int fd, uint8_t opcode,
                                    const uint8_t *payload, size_t payload_len)
{
    uint8_t header[14] = {0};
    size_t  header_len = 0;

    header[0] = (uint8_t)(0x80 | (opcode & 0x0F));

    /* Client frames MUST be masked (RFC 6455 Section 5.1) */
    uint8_t mask[4];
    uint32_t mask_val = (uint32_t)rand();
    memcpy(mask, &mask_val, 4);

    if (payload_len <= 125) {
        header[1]  = (uint8_t)(0x80 | payload_len);
        header_len = 2;
    } else if (payload_len <= 0xFFFF) {
        header[1]  = 0x80 | 126;
        header[2]  = (uint8_t)((payload_len >> 8) & 0xFF);
        header[3]  = (uint8_t)(payload_len & 0xFF);
        header_len = 4;
    } else {
        header[1] = 0x80 | 127;
        uint64_t plen64 = (uint64_t)payload_len;
        for (int i = 0; i < 8; i++) {
            header[2 + i] = (uint8_t)((plen64 >> (56 - i * 8)) & 0xFF);
        }
        header_len = 10;
    }

    memcpy(header + header_len, mask, 4);
    header_len += 4;

    OPERATE_RET rt = __send_all(fd, header, header_len);
    if (rt != OPRT_OK) return rt;

    if (payload_len > 0) {
        uint8_t *masked = tal_malloc(payload_len);
        if (!masked) return OPRT_MALLOC_FAILED;
        for (size_t i = 0; i < payload_len; i++) {
            masked[i] = payload[i] ^ mask[i % 4];
        }
        rt = __send_all(fd, masked, payload_len);
        tal_free(masked);
    }
    return rt;
}

static OPERATE_RET __decode_ws_frame(uint8_t **out_payload, size_t *out_len,
                                      uint8_t *out_opcode, size_t *consumed)
{
    if (s_rx_len < 2) return OPRT_RESOURCE_NOT_READY;

    uint8_t op   = s_rx_buf[0] & 0x0F;
    bool masked  = (s_rx_buf[1] & 0x80) != 0;
    uint64_t plen = s_rx_buf[1] & 0x7F;
    size_t off    = 2;

    if (plen == 126) {
        if (s_rx_len < off + 2) return OPRT_RESOURCE_NOT_READY;
        plen = (uint64_t)((s_rx_buf[off] << 8) | s_rx_buf[off + 1]);
        off += 2;
    } else if (plen == 127) {
        if (s_rx_len < off + 8) return OPRT_RESOURCE_NOT_READY;
        plen = 0;
        for (int i = 0; i < 8; i++)
            plen = (plen << 8) | s_rx_buf[off + i];
        off += 8;
    }

    if (plen > WS_RX_BUF_SIZE - 16) return OPRT_MSG_OUT_OF_LIMIT;

    size_t mask_len = masked ? 4 : 0;
    size_t frame_len = off + mask_len + (size_t)plen;
    if (s_rx_len < frame_len) return OPRT_RESOURCE_NOT_READY;

    uint8_t mask_key[4] = {0};
    if (masked) {
        memcpy(mask_key, s_rx_buf + off, 4);
        off += 4;
    }

    uint8_t *data = tal_malloc((size_t)plen + 1);
    if (!data) return OPRT_MALLOC_FAILED;

    if (plen > 0) {
        memcpy(data, s_rx_buf + off, (size_t)plen);
        if (masked) {
            for (size_t i = 0; i < (size_t)plen; i++)
                data[i] ^= mask_key[i % 4];
        }
    }
    data[plen] = '\0';

    *out_payload = data;
    *out_len     = (size_t)plen;
    *out_opcode  = op;
    *consumed    = frame_len;
    return OPRT_OK;
}

static void __consume_rx(size_t n)
{
    if (n >= s_rx_len) {
        s_rx_len = 0;
    } else {
        memmove(s_rx_buf, s_rx_buf + n, s_rx_len - n);
        s_rx_len -= n;
    }
}

/* ---- WS handshake (client side) ---- */

static OPERATE_RET __ws_handshake(int fd)
{
    /* Generate random 16-byte key, base64 encode */
    uint8_t raw_key[16];
    for (int i = 0; i < 16; i++) raw_key[i] = (uint8_t)(rand() & 0xFF);

    char ws_key[32] = {0};
    tuya_base64_encode(raw_key, ws_key, 16);

    char request[512];
    int n = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "X-Buddy-Name: Claude_Buddy\r\n"
        "X-Buddy-Version: 1.0\r\n"
        "\r\n",
        BUDDY_WS_PATH, s_host, (unsigned)s_port, ws_key);

    OPERATE_RET rt = __send_all(fd, (const uint8_t *)request, (size_t)n);
    if (rt != OPRT_OK) return rt;

    /* Read HTTP response (expect 101) */
    uint8_t resp[1024];
    size_t  resp_len = 0;
    int     timeout_ms = 5000;
    int     elapsed = 0;

    while (elapsed < timeout_ms) {
        int r = tal_net_recv(fd, resp + resp_len, (uint32_t)(sizeof(resp) - resp_len));
        if (r > 0) {
            resp_len += (size_t)r;
            resp[resp_len] = '\0';
            if (strstr((char *)resp, "\r\n\r\n")) break;
        } else if (r == OPRT_RESOURCE_NOT_READY) {
            tal_system_sleep(50);
            elapsed += 50;
        } else {
            return OPRT_RECV_ERR;
        }
    }

    if (!strstr((char *)resp, "101")) {
        PR_WARN("WS handshake rejected: %.*s", (int)(resp_len > 80 ? 80 : resp_len), resp);
        return OPRT_COM_ERROR;
    }

    PR_INFO("WS handshake success");
    return OPRT_OK;
}

/* ---- Send JSON text frame ---- */

static OPERATE_RET __send_json(const char *json_str)
{
    if (!s_ws_connected || s_ws_fd < 0 || !json_str) return OPRT_COM_ERROR;

    tal_mutex_lock(s_ws_tx_mutex);
    OPERATE_RET rt = __send_ws_frame(s_ws_fd, 0x1,
                                      (const uint8_t *)json_str, strlen(json_str));
    tal_mutex_unlock(s_ws_tx_mutex);

    if (rt != OPRT_OK) {
        PR_WARN("WS send failed rt=%d", rt);
    }
    return rt;
}

/* ---- Public send APIs ---- */

OPERATE_RET buddy_ws_send_permission(const char *id, const char *decision)
{
    if (!id || !decision) return OPRT_INVALID_PARM;
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "cmd", "permission");
    cJSON_AddStringToObject(obj, "id", id);
    cJSON_AddStringToObject(obj, "decision", decision);
    char *str = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    OPERATE_RET rt = __send_json(str);
    cJSON_free(str);
    return rt;
}

OPERATE_RET buddy_ws_send_asr(const char *text, const char *sid)
{
    if (!text) return OPRT_INVALID_PARM;
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "cmd", "asr");
    cJSON_AddStringToObject(obj, "text", text);
    if (sid) cJSON_AddStringToObject(obj, "sid", sid);
    char *str = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    OPERATE_RET rt = __send_json(str);
    cJSON_free(str);
    return rt;
}

OPERATE_RET buddy_ws_send_hb_req(const char *page)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "cmd", "hb_req");
    if (page) cJSON_AddStringToObject(obj, "page", page);
    char *str = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    OPERATE_RET rt = __send_json(str);
    cJSON_free(str);
    return rt;
}

OPERATE_RET buddy_ws_send_ack(const char *cmd)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "ack", cmd ? cmd : "status");
    cJSON_AddBoolToObject(obj, "ok", 1);
    char *str = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    OPERATE_RET rt = __send_json(str);
    cJSON_free(str);
    return rt;
}

bool buddy_ws_is_connected(void)
{
    return s_ws_connected;
}

/* ---- Connection loop ---- */

static void __ws_close(void)
{
    if (s_ws_fd >= 0) {
        tal_net_close(s_ws_fd);
        s_ws_fd = -1;
    }
    if (s_ws_connected) {
        s_ws_connected = FALSE;
        buddy_state_set_connected(false);
        PR_INFO("WS disconnected");
    }
    s_rx_len = 0;
}

static OPERATE_RET __ws_connect(void)
{
    if (s_host[0] == '\0') return OPRT_COM_ERROR;

    TUYA_IP_ADDR_T addr = tal_net_str2addr(s_host);
    if (addr == 0) {
        addr = tal_net_gethostbyname(s_host);
        if (addr == 0) {
            PR_WARN("DNS resolve failed: %s", s_host);
            return OPRT_COM_ERROR;
        }
    }

    int fd = tal_net_socket_create(PROTOCOL_TCP);
    if (fd < 0) return OPRT_SOCK_ERR;

    OPERATE_RET rt = tal_net_connect(fd, addr, s_port);
    if (rt != OPRT_OK) {
        tal_net_close(fd);
        return rt;
    }

    tal_net_set_block(fd, FALSE);
    s_ws_fd = fd;

    rt = __ws_handshake(fd);
    if (rt != OPRT_OK) {
        __ws_close();
        return rt;
    }

    s_ws_connected = TRUE;
    buddy_state_set_connected(true);
    PR_INFO("WS connected to %s:%u", s_host, s_port);
    return OPRT_OK;
}

static void __ws_task(void *arg)
{
    (void)arg;
    uint32_t backoff = WS_RECONNECT_MIN;

    PR_INFO("WS task started");

    while (s_ws_running) {
        if (!s_ws_connected) {
            if (s_host[0] == '\0') {
                tal_system_sleep(1000);
                continue;
            }
            OPERATE_RET rt = __ws_connect();
            if (rt != OPRT_OK) {
                PR_DEBUG("WS connect failed, retry in %u ms", backoff);
                tal_system_sleep(backoff);
                if (backoff < WS_RECONNECT_MAX) backoff *= 2;
                continue;
            }
            backoff = WS_RECONNECT_MIN;
        }

        /* Poll for incoming data */
        TUYA_FD_SET_T readfds;
        TAL_FD_ZERO(&readfds);
        TAL_FD_SET(s_ws_fd, &readfds);

        int ready = tal_net_select(s_ws_fd + 1, &readfds, NULL, NULL, 200);
        if (ready < 0) {
            __ws_close();
            continue;
        }
        if (ready == 0) continue;

        if (s_rx_len >= sizeof(s_rx_buf)) {
            PR_WARN("WS RX buffer overflow");
            __ws_close();
            continue;
        }

        int n = tal_net_recv(s_ws_fd, s_rx_buf + s_rx_len,
                              (uint32_t)(sizeof(s_rx_buf) - s_rx_len));
        if (n == OPRT_RESOURCE_NOT_READY) continue;
        if (n <= 0) {
            __ws_close();
            continue;
        }
        s_rx_len += (size_t)n;

        /* Process all complete frames */
        while (s_rx_len > 0) {
            uint8_t *payload  = NULL;
            size_t   pay_len  = 0;
            uint8_t  opcode   = 0;
            size_t   consumed = 0;

            OPERATE_RET rt = __decode_ws_frame(&payload, &pay_len, &opcode, &consumed);
            if (rt == OPRT_RESOURCE_NOT_READY) break;
            if (rt != OPRT_OK) {
                tal_free(payload);
                __ws_close();
                break;
            }

            __consume_rx(consumed);

            if (opcode == 0x1) {
                /* Text frame — dispatch to protocol layer */
                buddy_protocol_on_recv((const char *)payload);
            } else if (opcode == 0x8) {
                /* Close frame */
                __send_ws_frame(s_ws_fd, 0x8, payload, pay_len);
                tal_free(payload);
                __ws_close();
                break;
            } else if (opcode == 0x9) {
                /* Ping → Pong */
                __send_ws_frame(s_ws_fd, 0xA, payload, pay_len);
            }

            tal_free(payload);
        }
    }

    __ws_close();
    PR_INFO("WS task stopped");
}

/* ---- KV persistence ---- */

static void __load_kv_config(void)
{
    size_t len = sizeof(s_host);
    if (tal_kv_get(KV_KEY_HOST, (uint8_t *)s_host, &len) != OPRT_OK) {
        s_host[0] = '\0';
    }

    uint8_t port_buf[8] = {0};
    len = sizeof(port_buf);
    if (tal_kv_get(KV_KEY_PORT, port_buf, &len) == OPRT_OK) {
        s_port = (uint16_t)atoi((char *)port_buf);
        if (s_port == 0) s_port = BUDDY_WS_DEFAULT_PORT;
    }

    if (s_host[0]) {
        PR_INFO("WS config loaded: %s:%u", s_host, s_port);
    } else {
        PR_INFO("WS config not set. Use: buddy ws set <ip> [port]");
    }
}

/* ---- CLI commands ---- */

static void __cli_ws_handler(int argc, char *argv[])
{
    if (argc < 2) {
        PR_NOTICE("Usage: buddy ws <set|status>");
        return;
    }

    if (strcmp(argv[1], "set") == 0) {
        if (argc < 3) {
            PR_NOTICE("Usage: buddy ws set <ip> [port]");
            return;
        }
        strncpy(s_host, argv[2], BUDDY_WS_HOST_LEN);
        s_host[BUDDY_WS_HOST_LEN] = '\0';
        tal_kv_set(KV_KEY_HOST, (const uint8_t *)s_host, strlen(s_host) + 1);

        if (argc >= 4) {
            s_port = (uint16_t)atoi(argv[3]);
            if (s_port == 0) s_port = BUDDY_WS_DEFAULT_PORT;
        } else {
            s_port = BUDDY_WS_DEFAULT_PORT;
        }
        char port_str[8];
        snprintf(port_str, sizeof(port_str), "%u", s_port);
        tal_kv_set(KV_KEY_PORT, (const uint8_t *)port_str, strlen(port_str) + 1);

        PR_NOTICE("WS target set to %s:%u", s_host, s_port);

        /* Force reconnect */
        __ws_close();

    } else if (strcmp(argv[1], "status") == 0) {
        PR_NOTICE("WS host:      %s", s_host[0] ? s_host : "(not set)");
        PR_NOTICE("WS port:      %u", s_port);
        PR_NOTICE("WS connected: %s", s_ws_connected ? "yes" : "no");
    } else {
        PR_NOTICE("Unknown subcommand: %s", argv[1]);
    }
}

void buddy_ws_cli_register(void)
{
    tal_cli_cmd_register("buddy", __cli_ws_handler);
}

/* ---- Lifecycle ---- */

OPERATE_RET buddy_ws_init(void)
{
    __load_kv_config();

    if (!s_ws_tx_mutex) {
        OPERATE_RET rt = tal_mutex_create_init(&s_ws_tx_mutex);
        if (rt != OPRT_OK) return rt;
    }

    buddy_ws_cli_register();

    PR_INFO("buddy_ws_init done");
    return OPRT_OK;
}

OPERATE_RET buddy_ws_start(void)
{
    if (s_ws_thread) return OPRT_OK;

    s_ws_running = TRUE;

    THREAD_CFG_T cfg = {0};
    cfg.stackDepth = WS_STACK_SIZE;
    cfg.priority   = THREAD_PRIO_2;
    cfg.thrdname   = "buddy_ws";

    OPERATE_RET rt = tal_thread_create_and_start(&s_ws_thread, NULL, NULL,
                                                  __ws_task, NULL, &cfg);
    if (rt != OPRT_OK) {
        s_ws_running = FALSE;
        PR_ERR("WS thread create failed rt=%d", rt);
    }
    return rt;
}

OPERATE_RET buddy_ws_stop(void)
{
    s_ws_running = FALSE;
    if (s_ws_thread) {
        tal_thread_delete(s_ws_thread);
        s_ws_thread = NULL;
    }
    __ws_close();
    return OPRT_OK;
}
```

- [ ] **Step 3: Commit**

```bash
git add src/transport/buddy_ws.h src/transport/buddy_ws.c
git commit -m "feat(buddy): add WebSocket client transport with auto-reconnect"
```

---

## Task 6: Screen Manager + Startup Screen (M3 - UI Framework)

**Files:**
- Create: `src/display/CMakeLists.txt`
- Create: `src/display/screen_manager.h`
- Create: `src/display/screen_manager.c`
- Create: `src/display/screens/startup_screen.h`
- Create: `src/display/screens/startup_screen.c`

- [ ] **Step 1: Copy screen_manager and startup_screen from reference**

These files are well-structured in the reference and can be copied directly, then adapted to the new directory layout:

```bash
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/screen_manager.h > src/display/screen_manager.h
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/screen_manager.c > src/display/screen_manager.c
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/startup_screen.h > src/display/screens/startup_screen.h
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/startup_screen.c > src/display/screens/startup_screen.c
```

- [ ] **Step 2: Update startup_screen.c include paths**

Change `#include "buddy_main_screen.h"` to `#include "main_screen.h"` and update screen_manager.h include path if needed.

- [ ] **Step 3: Create src/display/CMakeLists.txt**

```cmake
##
# @file CMakeLists.txt
# @brief Display module build configuration
#/

set(APP_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR})

set(APP_MODULE_SRCS)

aux_source_directory(${APP_MODULE_PATH} DISPLAY_ROOT_SRCS)
aux_source_directory(${APP_MODULE_PATH}/screens SCREEN_SRCS)
aux_source_directory(${APP_MODULE_PATH}/widgets WIDGET_SRCS)
aux_source_directory(${APP_MODULE_PATH}/persona PERSONA_SRCS)
aux_source_directory(${APP_MODULE_PATH}/fonts FONTS_SRCS)

if(DISABLE_BUDDY_CJK_FONT)
    list(FILTER FONTS_SRCS EXCLUDE REGEX ".*ui_font_puhui_.*\\.c$")
endif()

list(APPEND APP_MODULE_SRCS
    ${DISPLAY_ROOT_SRCS}
    ${SCREEN_SRCS}
    ${WIDGET_SRCS}
    ${PERSONA_SRCS}
    ${FONTS_SRCS}
)

set(APP_MODULE_INC
    ${APP_MODULE_PATH}
    ${APP_MODULE_PATH}/screens
    ${APP_MODULE_PATH}/widgets
    ${APP_MODULE_PATH}/persona
)

target_sources(${EXAMPLE_LIB}
    PRIVATE
        ${APP_MODULE_SRCS}
)

target_include_directories(${EXAMPLE_LIB}
    PRIVATE
        ${APP_MODULE_INC}
)
```

- [ ] **Step 4: Copy font files from reference**

```bash
mkdir -p src/display/fonts
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/fonts/lv_font_terminusTTF_Bold_14.c > src/display/fonts/lv_font_terminusTTF_Bold_14.c
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/fonts/lv_font_terminusTTF_Bold_16.c > src/display/fonts/lv_font_terminusTTF_Bold_16.c
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/fonts/lv_font_terminusTTF_Bold_18.c > src/display/fonts/lv_font_terminusTTF_Bold_18.c
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/fonts/ui_font_puhui_18_2.c > src/display/fonts/ui_font_puhui_18_2.c
```

- [ ] **Step 5: Copy logo files from reference**

```bash
mkdir -p src/display/logo
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/logo/tuyaopen_logo-384-168.c > src/display/logo/tuyaopen_logo-384-168.c
```

- [ ] **Step 6: Commit**

```bash
git add src/display/
git commit -m "feat(buddy): add screen manager, startup screen, and display build system"
```

---

## Task 7: Status Bar + LED Widgets (M3 - Reusable Widgets)

**Files:**
- Create: `src/display/widgets/status_bar.h`
- Create: `src/display/widgets/status_bar.c`
- Create: `src/display/widgets/led_indicator.h`
- Create: `src/display/widgets/led_indicator.c`

- [ ] **Step 1: Copy and adapt status_bar from reference**

```bash
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/buddy_status_bar.h > src/display/widgets/status_bar.h
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/buddy_status_bar.c > src/display/widgets/status_bar.c
```

Update include paths: replace `#include "buddy_ble.h"` with `#include "buddy_transport.h"` and change `buddy_ble_snapshot()` calls to `buddy_state_snapshot()` (from `buddy_protocol.h`).

- [ ] **Step 2: Copy and adapt LED indicator from reference**

```bash
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/buddy_led.h > src/display/widgets/led_indicator.h
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/buddy_led.c > src/display/widgets/led_indicator.c
```

No API changes needed — LED is hardware-only, independent of transport.

- [ ] **Step 3: Commit**

```bash
git add src/display/widgets/
git commit -m "feat(buddy): add status bar and LED indicator widgets"
```

---

## Task 8: Main Screen (M3 - Core UI)

**Files:**
- Create: `src/display/screens/main_screen.h`
- Create: `src/display/screens/main_screen.c`

- [ ] **Step 1: Copy main screen from reference and adapt**

```bash
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/buddy_main_screen.h > src/display/screens/main_screen.h
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/buddy_main_screen.c > src/display/screens/main_screen.c
```

- [ ] **Step 2: Refactor to use decoupled state access**

In `main_screen.c`, make these systematic replacements:
- `#include "buddy_ble.h"` → `#include "buddy_protocol.h"` and `#include "buddy_transport.h"`
- `buddy_ble_snapshot(&state)` → `buddy_state_snapshot(&state)`
- `buddy_ble_send_hb_req(page)` → `buddy_ws_send_hb_req(page)`
- `buddy_ble_send_permission(id, dec)` → `buddy_ws_send_permission(id, dec)`
- `state.ble_connected` → `state.ws_connected`
- Update function name prefix from `buddy_main_screen_` to keep consistent

- [ ] **Step 3: Commit**

```bash
git add src/display/screens/main_screen.h src/display/screens/main_screen.c
git commit -m "feat(buddy): add main screen with persona and session list"
```

---

## Task 9: Approval Screen (M4 - Permission Flow)

**Files:**
- Create: `src/display/screens/approval_screen.h`
- Create: `src/display/screens/approval_screen.c`

- [ ] **Step 1: Copy approval screen from reference and adapt**

```bash
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/buddy_approval_screen.h > src/display/screens/approval_screen.h
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/buddy_approval_screen.c > src/display/screens/approval_screen.c
```

- [ ] **Step 2: Refactor to use decoupled APIs**

Same replacement pattern as Task 8:
- `buddy_ble_snapshot` → `buddy_state_snapshot`
- `buddy_ble_send_permission` → `buddy_ws_send_permission`
- Update includes

- [ ] **Step 3: Commit**

```bash
git add src/display/screens/approval_screen.h src/display/screens/approval_screen.c
git commit -m "feat(buddy): add approval screen for tool permission decisions"
```

---

## Task 10: Remaining Screens (M5 - Full UI)

**Files:**
- Create: `src/display/screens/session_screen.h` + `.c`
- Create: `src/display/screens/status_screen.h` + `.c`
- Create: `src/display/screens/chart_screen.h` + `.c`
- Create: `src/display/screens/pie_screen.h` + `.c`

- [ ] **Step 1: Copy all remaining screens from reference and adapt**

```bash
# Session screen
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/buddy_session_screen.h > src/display/screens/session_screen.h
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/buddy_session_screen.c > src/display/screens/session_screen.c

# Status screen
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/buddy_status_screen.h > src/display/screens/status_screen.h
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/buddy_status_screen.c > src/display/screens/status_screen.c

# Chart screen
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/buddy_chart_screen.h > src/display/screens/chart_screen.h
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/buddy_chart_screen.c > src/display/screens/chart_screen.c

# Pie screen
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/buddy_pie_screen.h > src/display/screens/pie_screen.h
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/buddy_pie_screen.c > src/display/screens/pie_screen.c
```

- [ ] **Step 2: Apply same decoupling refactor to all screens**

For each screen file, replace:
- `buddy_ble_snapshot` → `buddy_state_snapshot`
- `buddy_ble_send_hb_req` → `buddy_ws_send_hb_req`
- `#include "buddy_ble.h"` → `#include "buddy_protocol.h"` + `#include "buddy_transport.h"`
- `ble_connected` → `ws_connected`

- [ ] **Step 3: Commit**

```bash
git add src/display/screens/session_screen.* src/display/screens/status_screen.* \
        src/display/screens/chart_screen.* src/display/screens/pie_screen.*
git commit -m "feat(buddy): add session, status, chart, and pie screens"
```

---

## Task 11: Persona System (M6 - Animations)

**Files:**
- Create: `src/display/persona/persona_registry.h`
- Create: `src/display/persona/persona_registry.c`
- Create: `src/display/persona/ascii_persona.h`
- Create: `src/display/persona/ascii_persona.c`
- Create: `src/display/persona/persona_*.c` (18 files)

- [ ] **Step 1: Copy persona system from reference**

```bash
mkdir -p src/display/persona

# Core files
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/persona_registry.h > src/display/persona/persona_registry.h
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/persona_registry.c > src/display/persona/persona_registry.c
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/ascii_persona.h > src/display/persona/ascii_persona.h
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/ascii_persona.c > src/display/persona/ascii_persona.c

# All 18 persona species
for p in axolotl blob cactus capybara cat chonk dragon duck ghost goose mushroom octopus owl penguin rabbit robot snail turtle; do
    git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_ui/persona_${p}.c > src/display/persona/persona_${p}.c
done
```

- [ ] **Step 2: Verify all persona files compile**

No API changes needed — persona files are pure data (ASCII art frames) with no transport dependencies.

- [ ] **Step 3: Commit**

```bash
git add src/display/persona/
git commit -m "feat(buddy): add 18 ASCII persona animations and registry"
```

---

## Task 12: Media and UI Entry (M3 - Glue)

**Files:**
- Create: `src/media/media_pet.c`
- Create: `include/media/media_pet.h`

- [ ] **Step 1: Copy media files from reference**

```bash
mkdir -p include/media
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/include/media/media_pet.h > include/media/media_pet.h
git show maidang/xb/claude_buddy_pocket:apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/media/media_pet.c > src/media/media_pet.c
```

- [ ] **Step 2: Commit**

```bash
git add include/media/ src/media/
git commit -m "feat(buddy): add media resources"
```

---

## Task 13: Plugin Scaffold (M7 - Node.js Plugin)

**Files:**
- Create: `tuya_pocket_buddy_plugin/package.json`
- Create: `tuya_pocket_buddy_plugin/tsconfig.json`
- Create: `tuya_pocket_buddy_plugin/.claude-plugin/plugin.json`
- Create: `tuya_pocket_buddy_plugin/settings/hooks.json`
- Create: `tuya_pocket_buddy_plugin/src/config.ts`

- [ ] **Step 1: Create package.json**

```json
{
  "name": "tuya-pocket-buddy",
  "version": "1.0.0",
  "description": "Claude Code companion device plugin — WebSocket bridge",
  "main": "dist/index.js",
  "scripts": {
    "build": "tsc",
    "start": "node dist/index.js run",
    "dev": "ts-node src/index.ts run"
  },
  "dependencies": {
    "ws": "^8.16.0"
  },
  "devDependencies": {
    "@types/node": "^20.0.0",
    "@types/ws": "^8.5.0",
    "typescript": "^5.4.0",
    "ts-node": "^10.9.0"
  }
}
```

- [ ] **Step 2: Create tsconfig.json**

```json
{
  "compilerOptions": {
    "target": "ES2022",
    "module": "commonjs",
    "lib": ["ES2022"],
    "outDir": "dist",
    "rootDir": "src",
    "strict": true,
    "esModuleInterop": true,
    "skipLibCheck": true,
    "forceConsistentCasingInFileNames": true,
    "resolveJsonModule": true,
    "declaration": true
  },
  "include": ["src/**/*"],
  "exclude": ["node_modules", "dist"]
}
```

- [ ] **Step 3: Create .claude-plugin/plugin.json**

```json
{
  "name": "tuya-pocket-buddy",
  "version": "1.0.0",
  "description": "WebSocket companion mirror + hardware approval bridge for T5AI Pocket",
  "author": "Tuya Inc.",
  "license": "MIT",
  "keywords": ["claude", "companion", "hardware", "websocket", "approval"]
}
```

- [ ] **Step 4: Create settings/hooks.json**

```json
{
  "hooks": {
    "SessionStart": [
      {
        "type": "command",
        "command": "curl -s -X POST http://127.0.0.1:9878/hook -d @- -H 'Content-Type: application/json' --max-time 3 || true"
      }
    ],
    "UserPromptSubmit": [
      {
        "type": "command",
        "command": "curl -s -X POST http://127.0.0.1:9878/hook -d @- -H 'Content-Type: application/json' --max-time 3 || true"
      }
    ],
    "PostToolUse": [
      {
        "type": "command",
        "command": "curl -s -X POST http://127.0.0.1:9878/hook -d @- -H 'Content-Type: application/json' --max-time 3 || true"
      }
    ],
    "Stop": [
      {
        "type": "command",
        "command": "curl -s -X POST http://127.0.0.1:9878/hook -d @- -H 'Content-Type: application/json' --max-time 3 || true"
      }
    ],
    "PreToolUse": [
      {
        "type": "command",
        "command": "node tuya_pocket_buddy_plugin/scripts/hook_handler.js"
      }
    ]
  }
}
```

- [ ] **Step 5: Create src/config.ts**

```typescript
export const CONFIG = {
  hookPort: 9878,
  wsPort: 7681,
  maxDevices: 4,
  permissionTimeoutMs: 35_000,
  heartbeatIntervalMs: 10_000,
  timeSyncIntervalMs: 30_000,
  maxPayloadBytes: 65_536,
} as const;
```

- [ ] **Step 6: Install dependencies and commit**

```bash
cd tuya_pocket_buddy_plugin && npm install && cd ..
git add tuya_pocket_buddy_plugin/package.json tuya_pocket_buddy_plugin/tsconfig.json \
        tuya_pocket_buddy_plugin/.claude-plugin/ tuya_pocket_buddy_plugin/settings/ \
        tuya_pocket_buddy_plugin/src/config.ts tuya_pocket_buddy_plugin/package-lock.json
git commit -m "feat(plugin): scaffold Node.js/TS plugin with config and hooks"
```

---

## Task 14: Wire Protocol Encoder (M7 - Plugin Protocol)

**Files:**
- Create: `tuya_pocket_buddy_plugin/src/wire.ts`

- [ ] **Step 1: Create src/wire.ts**

```typescript
/**
 * Protocol frame encoder for Claude Buddy WebSocket wire format.
 * Mirrors the device-side buddy_protocol.c frame structures.
 */

export interface Entry {
  t: string;  // 't'=tool, 'd'=done, 'e'=error
  n: string;  // name
  h: string;  // hint
}

export interface SessionInfo {
  sid: string;
  name: string;
  model: string;
  tok: number;
  proj: string;
  run: boolean;
  ent: string[];
}

export interface ModelStat {
  m: string;
  tok: number;
}

export interface PromptInfo {
  id: string;
  tool: string;
  hint: string;
}

export interface Heartbeat {
  total: number;
  running: number;
  waiting: number;
  tokens: number;
  tokens_today: number;
  tokens_in: number;
  tokens_in_today: number;
  cache_read: number;
  cache_write: number;
  ctx_used: number;
  ctx_total: number;
  model: string;
  ver: string;
  cost_td: number;
  cost_all: number;
  entries: Entry[];
  sessions: SessionInfo[];
  mstats: ModelStat[];
  daily: number[];
  prompt?: PromptInfo;
  time: [number, number];
}

export function encodeHeartbeat(hb: Heartbeat): string {
  return JSON.stringify(hb);
}

export function encodeTimeSync(epochS: number, tzMin: number): string {
  return JSON.stringify({ time: [epochS, tzMin] });
}

export function encodeOwner(name: string): string {
  return JSON.stringify({ cmd: "owner", name });
}

export function encodeStatusRequest(): string {
  return JSON.stringify({ cmd: "status" });
}

export type DeviceFrameKind = "permission" | "asr" | "hb_req" | "ack" | "unknown";

export interface DeviceFrame {
  kind: DeviceFrameKind;
  payload: Record<string, unknown>;
}

export function parseDeviceFrame(raw: string): DeviceFrame {
  const obj = JSON.parse(raw);

  if (obj.cmd === "permission") {
    return { kind: "permission", payload: obj };
  }
  if (obj.cmd === "asr") {
    return { kind: "asr", payload: obj };
  }
  if (obj.cmd === "hb_req") {
    return { kind: "hb_req", payload: obj };
  }
  if (obj.ack) {
    return { kind: "ack", payload: obj };
  }

  return { kind: "unknown", payload: obj };
}
```

- [ ] **Step 2: Commit**

```bash
git add tuya_pocket_buddy_plugin/src/wire.ts
git commit -m "feat(plugin): add wire protocol encoder/decoder"
```

---

## Task 15: Permission Bridge (M7 - Approval Logic)

**Files:**
- Create: `tuya_pocket_buddy_plugin/src/permissions.ts`

- [ ] **Step 1: Create src/permissions.ts**

```typescript
/**
 * Permission bridge: coordinates between Claude Code PreToolUse hooks
 * and device button decisions over WebSocket.
 */

import { CONFIG } from "./config";

export type Decision = "once" | "always" | "deny";

interface PendingPrompt {
  id: string;
  tool: string;
  hint: string;
  resolve: (decision: Decision) => void;
  timer: ReturnType<typeof setTimeout>;
}

export class PermissionBridge {
  private pending: PendingPrompt | null = null;

  async waitForApproval(
    promptId: string,
    tool: string,
    hint: string
  ): Promise<Decision> {
    // Cancel any existing pending prompt
    if (this.pending) {
      clearTimeout(this.pending.timer);
      this.pending.resolve("deny");
      this.pending = null;
    }

    return new Promise<Decision>((resolve) => {
      const timer = setTimeout(() => {
        if (this.pending?.id === promptId) {
          this.pending = null;
          resolve("deny");
        }
      }, CONFIG.permissionTimeoutMs);

      this.pending = { id: promptId, tool, hint, resolve, timer };
    });
  }

  resolve(promptId: string, decision: Decision): boolean {
    if (!this.pending || this.pending.id !== promptId) {
      return false;
    }
    clearTimeout(this.pending.timer);
    this.pending.resolve(decision);
    this.pending = null;
    return true;
  }

  getCurrentPrompt(): { id: string; tool: string; hint: string } | null {
    if (!this.pending) return null;
    return {
      id: this.pending.id,
      tool: this.pending.tool,
      hint: this.pending.hint,
    };
  }

  hasPending(): boolean {
    return this.pending !== null;
  }
}
```

- [ ] **Step 2: Commit**

```bash
git add tuya_pocket_buddy_plugin/src/permissions.ts
git commit -m "feat(plugin): add permission bridge with timeout handling"
```

---

## Task 16: Hook Server (M7 - HTTP Receiver)

**Files:**
- Create: `tuya_pocket_buddy_plugin/src/hook-server.ts`

- [ ] **Step 1: Create src/hook-server.ts**

```typescript
/**
 * HTTP server receiving Claude Code hook payloads on 127.0.0.1:9878.
 * Loopback-only for security.
 */

import * as http from "http";
import { CONFIG } from "./config";

export type HookHandler = (
  eventName: string,
  payload: Record<string, unknown>
) => Promise<Record<string, unknown>>;

export class HookServer {
  private server: http.Server | null = null;
  private handler: HookHandler;

  constructor(handler: HookHandler) {
    this.handler = handler;
  }

  start(): Promise<void> {
    return new Promise((resolve, reject) => {
      this.server = http.createServer(async (req, res) => {
        if (req.method !== "POST" || req.url !== "/hook") {
          res.writeHead(404);
          res.end();
          return;
        }

        // Loopback-only check
        const host = req.headers.host || "";
        if (!host.startsWith("127.0.0.1") && !host.startsWith("localhost")) {
          res.writeHead(403);
          res.end(JSON.stringify({ error: "loopback only" }));
          return;
        }

        const chunks: Buffer[] = [];
        let totalLen = 0;

        req.on("data", (chunk: Buffer) => {
          totalLen += chunk.length;
          if (totalLen > CONFIG.maxPayloadBytes) {
            res.writeHead(413);
            res.end();
            req.destroy();
            return;
          }
          chunks.push(chunk);
        });

        req.on("end", async () => {
          try {
            const body = Buffer.concat(chunks).toString("utf-8");
            const payload = JSON.parse(body || "{}");
            const eventName = payload.event || payload.type || "unknown";
            const result = await this.handler(eventName, payload);
            res.writeHead(200, { "Content-Type": "application/json" });
            res.end(JSON.stringify(result));
          } catch (err) {
            res.writeHead(500);
            res.end(JSON.stringify({ error: String(err) }));
          }
        });
      });

      this.server.listen(CONFIG.hookPort, "127.0.0.1", () => {
        console.log(`[hook-server] listening on 127.0.0.1:${CONFIG.hookPort}`);
        resolve();
      });

      this.server.on("error", reject);
    });
  }

  stop(): void {
    this.server?.close();
    this.server = null;
  }
}
```

- [ ] **Step 2: Commit**

```bash
git add tuya_pocket_buddy_plugin/src/hook-server.ts
git commit -m "feat(plugin): add HTTP hook server for Claude Code events"
```

---

## Task 17: WebSocket Server (M7 - Device Connections)

**Files:**
- Create: `tuya_pocket_buddy_plugin/src/ws-server.ts`

- [ ] **Step 1: Create src/ws-server.ts**

```typescript
/**
 * WebSocket server managing multiple device connections on port 7681.
 * Routes incoming device frames and broadcasts heartbeats.
 */

import { WebSocketServer, WebSocket } from "ws";
import { IncomingMessage } from "http";
import { CONFIG } from "./config";
import { parseDeviceFrame, DeviceFrame } from "./wire";

export interface DeviceSession {
  ws: WebSocket;
  name: string;
  connectedAt: number;
}

export type DeviceFrameHandler = (
  session: DeviceSession,
  frame: DeviceFrame
) => void;

export class WsServer {
  private wss: WebSocketServer | null = null;
  private devices: Map<string, DeviceSession> = new Map();
  private frameHandler: DeviceFrameHandler;

  constructor(frameHandler: DeviceFrameHandler) {
    this.frameHandler = frameHandler;
  }

  start(): void {
    this.wss = new WebSocketServer({ port: CONFIG.wsPort, path: "/buddy" });

    this.wss.on("connection", (ws: WebSocket, req: IncomingMessage) => {
      const name =
        (req.headers["x-buddy-name"] as string) || `device_${Date.now()}`;

      // Enforce max connections
      if (this.devices.size >= CONFIG.maxDevices) {
        console.log(
          `[ws-server] max devices reached, rejecting ${name}`
        );
        ws.close(1013, "max devices");
        return;
      }

      // Replace existing session with same name
      const existing = this.devices.get(name);
      if (existing) {
        console.log(`[ws-server] replacing existing session for ${name}`);
        existing.ws.close(1000, "replaced");
        this.devices.delete(name);
      }

      const session: DeviceSession = {
        ws,
        name,
        connectedAt: Date.now(),
      };
      this.devices.set(name, session);
      console.log(
        `[ws-server] device connected: ${name} (${this.devices.size} total)`
      );

      ws.on("message", (data: Buffer | string) => {
        try {
          const raw = typeof data === "string" ? data : data.toString("utf-8");
          const frame = parseDeviceFrame(raw);
          this.frameHandler(session, frame);
        } catch (err) {
          console.warn(`[ws-server] bad frame from ${name}:`, err);
        }
      });

      ws.on("close", () => {
        this.devices.delete(name);
        console.log(
          `[ws-server] device disconnected: ${name} (${this.devices.size} total)`
        );
      });

      ws.on("error", (err) => {
        console.warn(`[ws-server] error from ${name}:`, err.message);
      });
    });

    console.log(`[ws-server] listening on 0.0.0.0:${CONFIG.wsPort}/buddy`);
  }

  broadcast(json: string): void {
    for (const [, session] of this.devices) {
      if (session.ws.readyState === WebSocket.OPEN) {
        session.ws.send(json);
      }
    }
  }

  send(name: string, json: string): void {
    const session = this.devices.get(name);
    if (session && session.ws.readyState === WebSocket.OPEN) {
      session.ws.send(json);
    }
  }

  getConnectedCount(): number {
    return this.devices.size;
  }

  stop(): void {
    for (const [, session] of this.devices) {
      session.ws.close(1001, "server shutdown");
    }
    this.devices.clear();
    this.wss?.close();
    this.wss = null;
  }
}
```

- [ ] **Step 2: Commit**

```bash
git add tuya_pocket_buddy_plugin/src/ws-server.ts
git commit -m "feat(plugin): add WebSocket server with multi-device support"
```

---

## Task 18: Hook Router (M7 - State Aggregation)

**Files:**
- Create: `tuya_pocket_buddy_plugin/src/hook-router.ts`

- [ ] **Step 1: Create src/hook-router.ts**

This is the most complex plugin module. It tracks Claude Code sessions, aggregates stats, and constructs heartbeat frames.

```typescript
/**
 * Event router: translates Claude Code hook events into heartbeat state.
 * Aggregates sessions, tokens, and stats from hooks + file system.
 */

import * as fs from "fs";
import * as path from "path";
import * as os from "os";
import { Heartbeat, Entry, SessionInfo, ModelStat, PromptInfo } from "./wire";
import { PermissionBridge, Decision } from "./permissions";
import { WsServer, DeviceSession } from "./ws-server";
import { DeviceFrame, encodeHeartbeat } from "./wire";
import { CONFIG } from "./config";

interface TrackedSession {
  sid: string;
  name: string;
  model: string;
  isRunning: boolean;
  tokensOut: number;
  project: string;
  localEntries: string[];
}

export class HookRouter {
  private sessions: Map<string, TrackedSession> = new Map();
  private entries: Entry[] = [];
  private permissions: PermissionBridge;
  private wsServer: WsServer;
  private heartbeatTimer: ReturnType<typeof setInterval> | null = null;
  private currentModel = "unknown";
  private claudeVersion = "";

  constructor(permissions: PermissionBridge, wsServer: WsServer) {
    this.permissions = permissions;
    this.wsServer = wsServer;
  }

  start(): void {
    this.currentModel = this.detectModel();
    this.claudeVersion = this.detectVersion();

    this.heartbeatTimer = setInterval(() => {
      this.broadcastHeartbeat();
    }, CONFIG.heartbeatIntervalMs);
  }

  stop(): void {
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer);
      this.heartbeatTimer = null;
    }
  }

  async route(
    eventName: string,
    payload: Record<string, unknown>
  ): Promise<Record<string, unknown>> {
    switch (eventName) {
      case "SessionStart":
        return this.onSessionStart(payload);
      case "UserPromptSubmit":
        return this.onUserPromptSubmit(payload);
      case "PreToolUse":
        return this.onPreToolUse(payload);
      case "PostToolUse":
        return this.onPostToolUse(payload);
      case "Stop":
        return this.onStop(payload);
      default:
        return {};
    }
  }

  handleDeviceFrame(session: DeviceSession, frame: DeviceFrame): void {
    switch (frame.kind) {
      case "permission": {
        const id = frame.payload.id as string;
        const decision = frame.payload.decision as Decision;
        this.permissions.resolve(id, decision);
        break;
      }
      case "hb_req":
        this.sendHeartbeatTo(session);
        break;
      case "asr":
        console.log(
          `[router] ASR from ${session.name}: ${frame.payload.text}`
        );
        break;
      case "ack":
        break;
    }
  }

  private onSessionStart(
    payload: Record<string, unknown>
  ): Record<string, unknown> {
    const sid =
      (payload.session_id as string) || `s_${Date.now()}`;
    const cwd = (payload.cwd as string) || "";
    const project = cwd ? path.basename(cwd) : "";

    this.sessions.set(sid, {
      sid: sid.substring(0, 15),
      name: "",
      model: this.currentModel,
      isRunning: true,
      tokensOut: 0,
      project: project.substring(0, 15),
      localEntries: [],
    });

    this.broadcastHeartbeat();
    return {};
  }

  private onUserPromptSubmit(
    payload: Record<string, unknown>
  ): Record<string, unknown> {
    const sid = payload.session_id as string;
    const prompt = (payload.prompt as string) || "";
    const session = this.sessions.get(sid);
    if (session && !session.name) {
      session.name = prompt.substring(0, 32);
    }
    return {};
  }

  private async onPreToolUse(
    payload: Record<string, unknown>
  ): Promise<Record<string, unknown>> {
    const tool = (payload.tool as string) || "unknown";
    const input = payload.input as Record<string, unknown> | undefined;
    const hint =
      typeof input === "object" && input
        ? (input.command as string) ||
          (input.file_path as string) ||
          JSON.stringify(input).substring(0, 60)
        : "";

    const promptId = `p_${Date.now()}`;

    this.pushEntry({ t: "t", n: tool.substring(0, 24), h: hint.substring(0, 48) });

    this.broadcastHeartbeat();

    const decision = await this.permissions.waitForApproval(
      promptId,
      tool,
      hint
    );

    if (decision === "deny") {
      return { decision: "block", reason: "denied by device" };
    }
    return {};
  }

  private onPostToolUse(
    payload: Record<string, unknown>
  ): Record<string, unknown> {
    const sid = payload.session_id as string;
    const tool = (payload.tool as string) || "";
    const session = this.sessions.get(sid);
    if (session) {
      const entry = `${tool}`.substring(0, 24);
      session.localEntries.push(entry);
      if (session.localEntries.length > 4) {
        session.localEntries.shift();
      }
    }
    return {};
  }

  private onStop(payload: Record<string, unknown>): Record<string, unknown> {
    const sid = payload.session_id as string;
    const session = this.sessions.get(sid);
    if (session) {
      session.isRunning = false;
      const usage = payload.usage as Record<string, number> | undefined;
      if (usage) {
        session.tokensOut = usage.output_tokens || 0;
      }
    }

    this.pushEntry({ t: "d", n: "session", h: "completed" });
    this.broadcastHeartbeat();
    return {};
  }

  private pushEntry(entry: Entry): void {
    this.entries.push(entry);
    if (this.entries.length > 8) {
      this.entries.shift();
    }
  }

  private buildHeartbeat(): Heartbeat {
    const sessionList = Array.from(this.sessions.values());
    const running = sessionList.filter((s) => s.isRunning).length;
    const totalTokens = sessionList.reduce((sum, s) => sum + s.tokensOut, 0);

    const stats = this.readStatsCache();
    const prompt = this.permissions.getCurrentPrompt() || undefined;

    const now = Math.floor(Date.now() / 1000);
    const tzMin = -new Date().getTimezoneOffset();

    const sessions: SessionInfo[] = sessionList
      .slice(0, 12)
      .map((s) => ({
        sid: s.sid,
        name: s.name || "(unnamed)",
        model: s.model,
        tok: s.tokensOut,
        proj: s.project,
        run: s.isRunning,
        ent: s.localEntries,
      }));

    return {
      total: sessionList.length,
      running,
      waiting: 0,
      tokens: totalTokens + (stats.totalTokens || 0),
      tokens_today: stats.todayTokens || 0,
      tokens_in: stats.totalInputTokens || 0,
      tokens_in_today: stats.todayInputTokens || 0,
      cache_read: stats.cacheRead || 0,
      cache_write: stats.cacheWrite || 0,
      ctx_used: 0,
      ctx_total: 0,
      model: this.currentModel,
      ver: this.claudeVersion,
      cost_td: stats.costToday || 0,
      cost_all: stats.costTotal || 0,
      entries: this.entries.slice(),
      sessions,
      mstats: stats.mstats || [],
      daily: stats.daily || [],
      prompt: prompt
        ? { id: prompt.id, tool: prompt.tool, hint: prompt.hint }
        : undefined,
      time: [now, tzMin],
    };
  }

  private broadcastHeartbeat(): void {
    const hb = this.buildHeartbeat();
    this.wsServer.broadcast(encodeHeartbeat(hb));
  }

  private sendHeartbeatTo(session: DeviceSession): void {
    const hb = this.buildHeartbeat();
    this.wsServer.send(session.name, encodeHeartbeat(hb));
  }

  onDeviceConnected(session: DeviceSession): void {
    this.sendHeartbeatTo(session);
  }

  private readStatsCache(): {
    totalTokens: number;
    todayTokens: number;
    totalInputTokens: number;
    todayInputTokens: number;
    cacheRead: number;
    cacheWrite: number;
    costToday: number;
    costTotal: number;
    mstats: ModelStat[];
    daily: number[];
  } {
    const defaults = {
      totalTokens: 0,
      todayTokens: 0,
      totalInputTokens: 0,
      todayInputTokens: 0,
      cacheRead: 0,
      cacheWrite: 0,
      costToday: 0,
      costTotal: 0,
      mstats: [] as ModelStat[],
      daily: [] as number[],
    };

    try {
      const statsPath = path.join(
        os.homedir(),
        ".claude",
        "stats-cache.json"
      );
      if (!fs.existsSync(statsPath)) return defaults;
      const raw = fs.readFileSync(statsPath, "utf-8");
      const data = JSON.parse(raw);

      // Extract daily model tokens for the last 28 days
      const dailyModel = data.dailyModelTokens || {};
      const dates = Object.keys(dailyModel).sort().slice(-28);
      const daily = dates.map((d: string) => {
        const models = dailyModel[d] || {};
        return Object.values(models).reduce(
          (sum: number, v: unknown) => sum + (v as number),
          0
        );
      });

      // Model stats aggregation
      const modelTotals: Record<string, number> = {};
      for (const d of dates) {
        const models = dailyModel[d] || {};
        for (const [m, t] of Object.entries(models)) {
          modelTotals[m] = (modelTotals[m] || 0) + (t as number);
        }
      }
      const mstats: ModelStat[] = Object.entries(modelTotals)
        .sort((a, b) => b[1] - a[1])
        .slice(0, 4)
        .map(([m, tok]) => ({ m: m.substring(0, 15), tok }));

      return { ...defaults, mstats, daily };
    } catch {
      return defaults;
    }
  }

  private detectModel(): string {
    if (process.env.ANTHROPIC_MODEL) {
      return process.env.ANTHROPIC_MODEL.substring(0, 15);
    }
    try {
      const settingsPath = path.join(
        os.homedir(),
        ".claude",
        "settings.json"
      );
      if (fs.existsSync(settingsPath)) {
        const data = JSON.parse(fs.readFileSync(settingsPath, "utf-8"));
        if (data.model) return String(data.model).substring(0, 15);
      }
    } catch {}
    return "unknown";
  }

  private detectVersion(): string {
    try {
      const { execSync } = require("child_process");
      const ver = execSync("claude --version", { timeout: 3000 })
        .toString()
        .trim();
      return ver.substring(0, 19);
    } catch {
      return "";
    }
  }
}
```

- [ ] **Step 2: Commit**

```bash
git add tuya_pocket_buddy_plugin/src/hook-router.ts
git commit -m "feat(plugin): add hook router with session tracking and heartbeat construction"
```

---

## Task 19: Hook Handler + Daemon Entry (M7 - Wiring)

**Files:**
- Create: `tuya_pocket_buddy_plugin/scripts/hook_handler.js`
- Create: `tuya_pocket_buddy_plugin/src/index.ts`

- [ ] **Step 1: Create scripts/hook_handler.js**

```javascript
#!/usr/bin/env node
/**
 * Blocking PreToolUse hook handler for Claude Code.
 * Reads payload from stdin, POSTs to daemon, waits for decision.
 * Exit 0 = approve, exit 2 = deny.
 */

const http = require("http");

const DAEMON_URL = "http://127.0.0.1:9878/hook";
const TIMEOUT_MS = 42000;

async function main() {
  const chunks = [];
  for await (const chunk of process.stdin) {
    chunks.push(chunk);
  }
  const payload = Buffer.concat(chunks).toString("utf-8");

  return new Promise((resolve) => {
    const url = new URL(DAEMON_URL);
    const req = http.request(
      {
        hostname: url.hostname,
        port: url.port,
        path: url.pathname,
        method: "POST",
        headers: { "Content-Type": "application/json" },
        timeout: TIMEOUT_MS,
      },
      (res) => {
        const resChunks = [];
        res.on("data", (c) => resChunks.push(c));
        res.on("end", () => {
          try {
            const body = JSON.parse(Buffer.concat(resChunks).toString());
            if (body.decision === "block") {
              process.exit(2);
            }
          } catch {}
          process.exit(0);
        });
      }
    );

    req.on("error", () => process.exit(0));
    req.on("timeout", () => {
      req.destroy();
      process.exit(0);
    });

    req.write(payload);
    req.end();
  });
}

main().catch(() => process.exit(0));
```

- [ ] **Step 2: Create src/index.ts**

```typescript
/**
 * Tuya Pocket Buddy daemon entry point.
 * Starts hook server, WS server, and hook router.
 */

import { HookServer } from "./hook-server";
import { HookRouter } from "./hook-router";
import { WsServer } from "./ws-server";
import { PermissionBridge } from "./permissions";

async function main(): Promise<void> {
  const command = process.argv[2] || "run";

  if (command === "run") {
    await runDaemon();
  } else if (command === "status") {
    console.log("Daemon status check not yet implemented");
  } else {
    console.log(`Unknown command: ${command}`);
    console.log("Usage: buddy-daemon [run|status]");
    process.exit(1);
  }
}

async function runDaemon(): Promise<void> {
  console.log("[daemon] starting...");

  const permissions = new PermissionBridge();

  const wsServer = new WsServer((session, frame) => {
    router.handleDeviceFrame(session, frame);
  });

  const router = new HookRouter(permissions, wsServer);

  const hookServer = new HookServer(async (eventName, payload) => {
    return router.route(eventName, payload);
  });

  // Start all services
  wsServer.start();
  await hookServer.start();
  router.start();

  // Send initial heartbeat to newly connected devices
  wsServer["wss"]?.on("connection", (ws, req) => {
    const name = (req.headers["x-buddy-name"] as string) || "unknown";
    const session = { ws, name, connectedAt: Date.now() };
    setTimeout(() => router.onDeviceConnected(session), 100);
  });

  console.log("[daemon] all services started");

  // Handle shutdown
  const shutdown = () => {
    console.log("[daemon] shutting down...");
    router.stop();
    hookServer.stop();
    wsServer.stop();
    process.exit(0);
  };

  process.on("SIGINT", shutdown);
  process.on("SIGTERM", shutdown);
}

main().catch((err) => {
  console.error("[daemon] fatal error:", err);
  process.exit(1);
});
```

- [ ] **Step 3: Commit**

```bash
git add tuya_pocket_buddy_plugin/scripts/hook_handler.js tuya_pocket_buddy_plugin/src/index.ts
git commit -m "feat(plugin): add hook handler script and daemon entry point"
```

---

## Task 20: Plugin Commands (M7 - CLI Integration)

**Files:**
- Create: `tuya_pocket_buddy_plugin/commands/buddy-install.md`
- Create: `tuya_pocket_buddy_plugin/commands/buddy-start.md`
- Create: `tuya_pocket_buddy_plugin/commands/buddy-stop.md`
- Create: `tuya_pocket_buddy_plugin/commands/buddy-status.md`

- [ ] **Step 1: Create buddy-install.md**

```markdown
---
name: buddy-install
description: Install the Tuya Pocket Buddy daemon and configure hooks
---

Install the daemon:

1. Navigate to `tuya_pocket_buddy_plugin/`
2. Run `npm install`
3. Run `npm run build`
4. Merge `settings/hooks.json` into your Claude Code hooks configuration

The daemon will be available via `buddy-start`.
```

- [ ] **Step 2: Create buddy-start.md**

```markdown
---
name: buddy-start
description: Start the Tuya Pocket Buddy daemon
---

Start the daemon process:

```bash
cd tuya_pocket_buddy_plugin && node dist/index.js run &
```

The daemon listens on:
- HTTP :9878 (hook server, loopback only)
- WebSocket :7681 (device connections)
```

- [ ] **Step 3: Create buddy-stop.md**

```markdown
---
name: buddy-stop
description: Stop the Tuya Pocket Buddy daemon
---

Stop the running daemon:

```bash
pkill -f "node dist/index.js run"
```
```

- [ ] **Step 4: Create buddy-status.md**

```markdown
---
name: buddy-status
description: Show Tuya Pocket Buddy daemon status
---

Check if the daemon is running and show connection info:

```bash
curl -s http://127.0.0.1:9878/hook -X POST -d '{}' -H 'Content-Type: application/json' --max-time 2 && echo " daemon is running" || echo "daemon is not running"
```
```

- [ ] **Step 5: Commit**

```bash
git add tuya_pocket_buddy_plugin/commands/
git commit -m "feat(plugin): add CLI command definitions"
```

---

## Task 21: Build Verification

- [ ] **Step 1: Verify plugin builds**

```bash
cd tuya_pocket_buddy_plugin && npm run build
```

Expected: TypeScript compiles to `dist/` without errors.

- [ ] **Step 2: Write M1 milestone doc**

Create `docs/m1-framework.md` summarizing the project skeleton, build system, and shared types.

- [ ] **Step 3: Write M2 milestone doc**

Create `docs/m2-transport.md` summarizing the WebSocket transport and protocol layers.

- [ ] **Step 4: Write M7 milestone doc**

Create `docs/m7-plugin.md` summarizing the Node.js/TS plugin architecture.

- [ ] **Step 5: Commit docs**

```bash
git add docs/
git commit -m "docs(buddy): add milestone summary documents"
```

---

## Task 22: ASR Integration (M8)

**Files:**
- Modify: `src/transport/buddy_ws.c` (already has `buddy_ws_send_asr`)
- Modify: `tuya_pocket_buddy_plugin/src/hook-router.ts` (already handles ASR frames)

- [ ] **Step 1: Verify ASR protocol path**

The ASR send function `buddy_ws_send_asr()` is already implemented in Task 5 (buddy_ws.c). The plugin-side handler is in `hook-router.ts` `handleDeviceFrame` case `"asr"`. The protocol path is complete.

- [ ] **Step 2: Add device-side ASR trigger**

The ASR trigger (button press → record → transcribe → send) depends on the device's ASR SDK availability. Create a stub in `src/media/media_pet.c` that calls `buddy_ws_send_asr()` when transcription completes. The actual ASR SDK integration will be done when the SDK is confirmed available.

- [ ] **Step 3: Write M8 milestone doc**

Create `docs/m8-asr.md` summarizing ASR protocol and integration status.

- [ ] **Step 4: Commit**

```bash
git add docs/m8-asr.md
git commit -m "docs(buddy): add ASR integration milestone doc"
```

---

## Verification Checklist

Before declaring the plan complete, verify against the design spec:

- [x] **Spec 1.1** Project goal: WS connection, display state, approval, ASR → Tasks 5, 8-10, 9, 22
- [x] **Spec 1.2** BLE→WS migration → Task 5 (buddy_ws.c replaces buddy_ble.c)
- [x] **Spec 1.3** Constraints: all in buddy dir, English comments, decoupled → All tasks
- [x] **Spec 2.1** Five-layer architecture → Tasks 1-20 map to all 5 layers
- [x] **Spec 3** Directory structure → Task 1 (CMake), Task 6 (display CMake)
- [x] **Spec 4** Protocol frames → Task 4 (state parser), Task 14 (wire.ts)
- [x] **Spec 5.1** buddy_types.h → Task 2
- [x] **Spec 5.2** buddy_ws.c → Task 5
- [x] **Spec 5.3** buddy_protocol/state → Task 4
- [x] **Spec 5.4** Display screens → Tasks 6-12
- [x] **Spec 6** Plugin modules → Tasks 13-20
- [x] **Spec 7** Approval flow → Tasks 9, 15, 19 (hook_handler.js)
- [x] **Spec 8** ASR → Task 22
- [x] **Spec 9** Milestones → Each task maps to a milestone, docs in Task 21
