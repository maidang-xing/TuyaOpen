# Claude Desktop Buddy - TuyaOpen Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the Claude Desktop Buddy firmware from Arduino/M5StickC Plus to TuyaOpen SDK on T5AI Pocket, using TAL BLE for communication and LVGL v9 for display.

**Architecture:** The app acts as a BLE peripheral that receives JSON heartbeat messages from the Claude Desktop application, maps them into 7 persona states (sleep, idle, busy, attention, heart, celebrate, dizzy), and drives LVGL GIF animations. Permission approval requests arrive over BLE and are handled via a dedicated approval screen with physical buttons. Stats (tokens, approvals, level) persist in tal_kv.

**Tech Stack:** TuyaOpen SDK (C), TAL BLE peripheral API (`tal_bluetooth.h`), LVGL v9 (GIF widget, screen manager), cJSON, tal_kv, tal_sw_timer

---

## Reference: Source Project

- **Original repo:** Claude Desktop Buddy (ESP32 M5StickC Plus, Arduino/PlatformIO)
- **Protocol:** JSON lines over BLE Nordic UART Service
- **Key doc:** `apps/tuya_t5_pocket/tuya_t5_pocket_ai/doc/PROJECT_OVERVIEW.md`

## Reference: Target Platform APIs

| Layer | Header | Key Functions |
|-------|--------|---------------|
| BLE | `tal_bluetooth.h` | `tal_ble_bt_init`, `tal_ble_advertising_start`, `tal_ble_server_common_send` |
| BLE defs | `tal_bluetooth_def.h` | `TAL_BLE_EVT_PARAMS_T`, `TAL_BLE_DATA_T`, event enums |
| Display | `lv_vendor.h` | `lv_vendor_init`, `lv_vendor_start`, `lv_vendor_disp_lock/unlock` |
| KV | `tal_kv.h` | `tal_kv_init`, `tal_kv_set`, `tal_kv_get`, `tal_kv_delete` |
| Timer | `tal_sw_timer.h` | `tal_sw_timer_create`, `tal_sw_timer_start` |
| System | `tal_api.h` | `tal_log_init`, `tal_system_get_millisecond`, `tal_malloc/free` |
| JSON | `cJSON.h` | `cJSON_Parse`, `cJSON_GetObjectItem`, `cJSON_CreateObject` |
| Screen Mgr | `screen_manager.h` (copy from existing app) | `screen_load`, `screen_back`, `screens_init` |

## File Structure

```
apps/tuya_t5_pocket/tuya_t5_pocket_buddy/
├── CMakeLists.txt                      # Build config (NEW)
├── app_default.config                  # Board/feature selection (NEW)
├── Kconfig                             # Kconfig options (NEW)
│
├── include/
│   ├── buddy_ble.h                     # BLE bridge: init, send, receive callbacks (NEW)
│   ├── buddy_protocol.h                # JSON protocol: parse heartbeat/permission/xfer (NEW)
│   ├── buddy_state.h                   # State machine: TamaState → PersonaState (NEW)
│   ├── buddy_stats.h                   # Statistics: tokens, approvals, level, KV persist (NEW)
│   └── buddy_display.h                 # Display init and message dispatch (NEW)
│
├── src/
│   ├── tuya_main.c                     # Entry point, init sequence (NEW)
│   ├── buddy_ble.c                     # BLE peripheral impl (NEW)
│   ├── buddy_protocol.c               # JSON parse/serialize impl (NEW)
│   ├── buddy_state.c                   # State machine impl (NEW)
│   ├── buddy_stats.c                   # Stats + KV impl (NEW)
│   │
│   └── display/
│       ├── CMakeLists.txt              # Display sub-build (NEW)
│       │
│       ├── ui/
│       │   ├── screen_manager.c        # Stack-based screen nav (COPY from existing app, adapt)
│       │   ├── screen_manager.h        # Screen_t struct, nav API (COPY from existing app, adapt)
│       │   ├── buddy_main_screen.c     # Main screen: pet + HUD + status bar (NEW)
│       │   ├── buddy_main_screen.h     # Main screen public API (NEW)
│       │   ├── buddy_approval_screen.c # Permission approval UI (NEW)
│       │   ├── buddy_approval_screen.h # Approval screen public API (NEW)
│       │   ├── buddy_stats_screen.c    # Stats page: mood/food/energy/level (NEW)
│       │   └── buddy_stats_screen.h    # Stats screen public API (NEW)
│       │
│       ├── anim/
│       │   └── (reuse ducky GIF assets from existing app via symlink or copy)
│       │
│       ├── icons/
│       │   └── (reuse battery/wifi icons from existing app)
│       │
│       └── fonts/
│           └── (reuse terminus fonts from existing app)
```

### Responsibility Breakdown

| File | Responsibility | Depends On |
|------|---------------|------------|
| `tuya_main.c` | Boot, init all subsystems, main loop | All modules |
| `buddy_ble.c/h` | BLE peripheral lifecycle, raw data send/receive | TAL BLE |
| `buddy_protocol.c/h` | JSON parse (heartbeat, permission, xfer), JSON serialize (responses) | cJSON |
| `buddy_state.c/h` | TamaState aggregation, PersonaState derivation, state transition rules | buddy_protocol |
| `buddy_stats.c/h` | Token tracking, approval stats, level calc, KV persist/restore | tal_kv |
| `buddy_display.h` | Display init, message dispatch to screens | lv_vendor |
| `buddy_main_screen.c/h` | Pet animation (GIF switching), status bar, HUD overlay | LVGL, buddy_state |
| `buddy_approval_screen.c/h` | Show permission request, handle approve/reject buttons | LVGL, buddy_ble |
| `buddy_stats_screen.c/h` | Display mood/food/energy/level bars | LVGL, buddy_stats |

---

## Task 1: Project Scaffolding and Build Verification

**Files:**
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/CMakeLists.txt`
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/app_default.config`
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/Kconfig`
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/include/buddy_ble.h`
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/include/buddy_protocol.h`
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/include/buddy_state.h`
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/include/buddy_stats.h`
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/include/buddy_display.h`
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/tuya_main.c`
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/CMakeLists.txt`

- [ ] **Step 1: Create app directory structure**

```bash
mkdir -p apps/tuya_t5_pocket/tuya_t5_pocket_buddy/{include,src/display/ui,src/display/anim,src/display/icons,src/display/fonts}
```

- [ ] **Step 2: Create Kconfig**

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/Kconfig`:

```kconfig
menu "configure app (t5 pocket buddy)"

config APP_CONFIG
    bool
    default y
    select ENABLE_LIBLVGL
    select ENABLE_LVGL_LODEPNG
    select ENABLE_BLUETOOTH
    select LV_FONT_MONTSERRAT_32

config TUYA_PRODUCT_ID
    string "product ID of project"
    default "buddy_desktop_01"

endmenu
```

- [ ] **Step 3: Create app_default.config**

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/app_default.config`:

```
CONFIG_PROJECT_VERSION="0.0.1"
CONFIG_BOARD_CHOICE_T5AI=y
CONFIG_BOARD_CHOICE_TUYA_T5AI_POCKET=y
CONFIG_BUTTON_NAME="btn_menu"
CONFIG_BUTTON_NAME_2="btn_enter"
CONFIG_BUTTON_NAME_3="btn_esc"
```

- [ ] **Step 4: Create CMakeLists.txt**

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/CMakeLists.txt`:

```cmake
##
# @file CMakeLists.txt
# @brief Claude Desktop Buddy - TuyaOpen Port
#/

# APP_PATH
set(APP_PATH ${CMAKE_CURRENT_LIST_DIR})

# APP_NAME
get_filename_component(APP_NAME ${APP_PATH} NAME)

# APP_SRCS
aux_source_directory(${APP_PATH}/src APP_SRCS)

set(APP_INC
            ${APP_PATH}/include
)

########################################
# Target Configure
########################################
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

########################################
# Add subdirectory
########################################
add_subdirectory(${APP_PATH}/src/display)
```

- [ ] **Step 5: Create display CMakeLists.txt**

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/CMakeLists.txt`:

```cmake
set(APP_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR})

aux_source_directory(${APP_MODULE_PATH}/ui UI_SRCS)
aux_source_directory(${APP_MODULE_PATH}/anim ANIM_SRCS)
aux_source_directory(${APP_MODULE_PATH}/icons ICONS_SRCS)
aux_source_directory(${APP_MODULE_PATH}/fonts FONTS_SRCS)

target_sources(${EXAMPLE_LIB}
    PRIVATE
        ${UI_SRCS}
        ${ANIM_SRCS}
        ${ICONS_SRCS}
        ${FONTS_SRCS}
    )

target_include_directories(${EXAMPLE_LIB}
    PRIVATE
        ${APP_MODULE_PATH}
        ${APP_MODULE_PATH}/ui
    )
```

- [ ] **Step 6: Create all header stubs**

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/include/buddy_ble.h`:

```c
#ifndef __BUDDY_BLE_H__
#define __BUDDY_BLE_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*buddy_ble_recv_cb_t)(const uint8_t *data, uint16_t len);

OPERATE_RET buddy_ble_init(buddy_ble_recv_cb_t recv_cb);
OPERATE_RET buddy_ble_send(const uint8_t *data, uint16_t len);
bool buddy_ble_is_connected(void);

#ifdef __cplusplus
}
#endif
#endif
```

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/include/buddy_protocol.h`:

```c
#ifndef __BUDDY_PROTOCOL_H__
#define __BUDDY_PROTOCOL_H__

#include "tuya_cloud_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int total_conversations;
    int running_conversations;
    int waiting_conversations;
    char message[128];
    uint32_t tokens;
    uint32_t timestamp;
} buddy_heartbeat_t;

typedef struct {
    char id[64];
    char tool[64];
    char hint[128];
} buddy_permission_req_t;

typedef enum {
    BUDDY_MSG_NONE = 0,
    BUDDY_MSG_HEARTBEAT,
    BUDDY_MSG_PERMISSION,
} buddy_msg_type_t;

typedef struct {
    buddy_msg_type_t type;
    union {
        buddy_heartbeat_t heartbeat;
        buddy_permission_req_t permission;
    };
} buddy_msg_t;

OPERATE_RET buddy_protocol_parse(const char *json_line, buddy_msg_t *out);
OPERATE_RET buddy_protocol_send_permission_response(const char *id, const char *decision);

#ifdef __cplusplus
}
#endif
#endif
```

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/include/buddy_state.h`:

```c
#ifndef __BUDDY_STATE_H__
#define __BUDDY_STATE_H__

#include "tuya_cloud_types.h"
#include "buddy_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PERSONA_SLEEP = 0,
    PERSONA_IDLE,
    PERSONA_BUSY,
    PERSONA_ATTENTION,
    PERSONA_HEART,
    PERSONA_CELEBRATE,
    PERSONA_DIZZY,
    PERSONA_STATE_COUNT,
} buddy_persona_state_t;

typedef void (*buddy_state_change_cb_t)(buddy_persona_state_t new_state);

OPERATE_RET buddy_state_init(buddy_state_change_cb_t cb);
void buddy_state_on_heartbeat(const buddy_heartbeat_t *hb);
void buddy_state_on_permission(const buddy_permission_req_t *req);
void buddy_state_on_permission_resolved(void);
void buddy_state_on_ble_connected(void);
void buddy_state_on_ble_disconnected(void);
void buddy_state_on_shake(void);
buddy_persona_state_t buddy_state_get(void);
const buddy_permission_req_t *buddy_state_get_pending_permission(void);

#ifdef __cplusplus
}
#endif
#endif
```

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/include/buddy_stats.h`:

```c
#ifndef __BUDDY_STATS_H__
#define __BUDDY_STATS_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t total_tokens;
    uint16_t level;
    uint16_t approvals;
    uint16_t rejections;
    uint8_t mood;
    uint8_t food;
    uint8_t energy;
} buddy_stats_t;

OPERATE_RET buddy_stats_init(void);
void buddy_stats_add_tokens(uint32_t tokens);
void buddy_stats_record_approval(uint32_t response_ms);
void buddy_stats_record_rejection(void);
void buddy_stats_add_energy(uint8_t amount);
const buddy_stats_t *buddy_stats_get(void);
OPERATE_RET buddy_stats_save(void);

#ifdef __cplusplus
}
#endif
#endif
```

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/include/buddy_display.h`:

```c
#ifndef __BUDDY_DISPLAY_H__
#define __BUDDY_DISPLAY_H__

#include "tuya_cloud_types.h"
#include "buddy_state.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET buddy_display_init(void);
void buddy_display_set_persona(buddy_persona_state_t state);
void buddy_display_set_ble_connected(bool connected);
void buddy_display_show_approval(const buddy_permission_req_t *req);

#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 7: Create minimal tuya_main.c**

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/tuya_main.c`:

```c
#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tal_kv.h"
#include "tal_cli.h"
#include "tkl_output.h"
#include "board_com_api.h"

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "0.0.1"
#endif

void user_main(void)
{
    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, (TAL_LOG_OUTPUT_CB)tkl_log_output);

    PR_NOTICE("=== Claude Desktop Buddy ===");
    PR_NOTICE("Version:  %s", PROJECT_VERSION);
    PR_NOTICE("Platform: %s / %s", PLATFORM_CHIP, PLATFORM_BOARD);

    tal_kv_init(&(tal_kv_cfg_t){
        .seed = "buddy_kv_seed_01",
        .key  = "buddy_kv_key__01",
    });
    tal_sw_timer_init();
    tal_workq_init();
    tal_cli_init();

    int ret = board_register_hardware();
    if (ret != OPRT_OK) {
        PR_ERR("board_register_hardware failed: %d", ret);
    }

    PR_NOTICE("Scaffolding OK - subsystems not yet initialized");

    for (;;) {
        tal_system_sleep(1000);
    }
}

#if OPERATING_SYSTEM == SYSTEM_LINUX
void main(int argc, char *argv[])
{
    user_main();
}
#else
static THREAD_HANDLE ty_app_thread = NULL;

static void tuya_app_thread(void *arg)
{
    user_main();
    tal_thread_delete(ty_app_thread);
    ty_app_thread = NULL;
}

void tuya_app_main(void)
{
    THREAD_CFG_T thrd_param = {0};
    thrd_param.stackDepth   = 1024 * 6;
    thrd_param.priority     = THREAD_PRIO_1;
    thrd_param.thrdname     = "tuya_app_main";
    tal_thread_create_and_start(&ty_app_thread, NULL, NULL, tuya_app_thread, NULL, &thrd_param);
}
#endif
```

- [ ] **Step 8: Verify project compiles**

```bash
cd /home/share/samba/tyopen/TuyaOpen
# Source environment
source export.sh
# Build the new app
tos build tuya_t5_pocket_buddy
```

Expected: Build succeeds (or at minimum, CMake configuration succeeds). Fix any build errors before proceeding.

- [ ] **Step 9: Commit scaffolding**

```bash
git add apps/tuya_t5_pocket/tuya_t5_pocket_buddy/
git commit -m "feat(buddy): scaffold Claude Desktop Buddy app structure"
```

---

## Task 2: BLE Communication Bridge

**Files:**
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/buddy_ble.c`
- Modify: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/include/buddy_ble.h` (already created)

This module wraps TAL BLE peripheral mode. It advertises as "Claude-Buddy", accepts one connection, and provides raw byte send/receive over the default Tuya service characteristics (write char = desktop→device, notify char = device→desktop).

- [ ] **Step 1: Implement buddy_ble.c**

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/buddy_ble.c`:

```c
#include "buddy_ble.h"
#include "tal_bluetooth.h"
#include "tal_log.h"
#include "tal_system.h"

#include <string.h>

#define BLE_DEVICE_NAME      "Claude-Buddy"
#define BLE_ADV_INTERVAL_MIN 0x0060
#define BLE_ADV_INTERVAL_MAX 0x00C0

static buddy_ble_recv_cb_t s_recv_cb = NULL;
static bool s_connected = false;
static TAL_BLE_PEER_INFO_T s_peer = {0};

static void ble_event_callback(TAL_BLE_EVT_PARAMS_T *p_event)
{
    switch (p_event->type) {
    case TAL_BLE_STACK_INIT: {
        PR_NOTICE("BLE stack initialized");

        uint8_t adv_data[] = {
            0x02, 0x01, 0x06,
            0x0D, 0x09,
            'C','l','a','u','d','e','-','B','u','d','d','y',
        };
        TAL_BLE_DATA_T adv = {
            .p_data = adv_data,
            .len = sizeof(adv_data),
        };
        tal_ble_advertising_data_set(&adv, NULL);

        TAL_BLE_ADV_PARAMS_T adv_params = {
            .adv_interval_min = BLE_ADV_INTERVAL_MIN,
            .adv_interval_max = BLE_ADV_INTERVAL_MAX,
            .adv_type = TAL_BLE_ADV_TYPE_CONN_UNDIR,
        };
        tal_ble_advertising_start(&adv_params);
        PR_NOTICE("BLE advertising started");
    } break;

    case TAL_BLE_EVT_PERIPHERAL_CONNECT: {
        s_connected = true;
        memcpy(&s_peer, &p_event->ble_event.connect.peer, sizeof(TAL_BLE_PEER_INFO_T));
        PR_NOTICE("BLE connected, handle=%d", s_peer.conn_handle);
    } break;

    case TAL_BLE_EVT_DISCONNECT: {
        s_connected = false;
        memset(&s_peer, 0, sizeof(TAL_BLE_PEER_INFO_T));
        PR_NOTICE("BLE disconnected, restarting advertising");

        TAL_BLE_ADV_PARAMS_T adv_params = {
            .adv_interval_min = BLE_ADV_INTERVAL_MIN,
            .adv_interval_max = BLE_ADV_INTERVAL_MAX,
            .adv_type = TAL_BLE_ADV_TYPE_CONN_UNDIR,
        };
        tal_ble_advertising_start(&adv_params);
    } break;

    case TAL_BLE_EVT_WRITE_REQ: {
        uint8_t *data = p_event->ble_event.write_report.report.p_data;
        uint16_t len = p_event->ble_event.write_report.report.len;
        PR_DEBUG("BLE recv %d bytes", len);

        if (s_recv_cb && data && len > 0) {
            s_recv_cb(data, len);
        }
    } break;

    case TAL_BLE_EVT_MTU_REQUEST: {
        uint16_t mtu = p_event->ble_event.exchange_mtu.mtu;
        PR_DEBUG("BLE MTU request: %d", mtu);
    } break;

    case TAL_BLE_EVT_SUBSCRIBE: {
        PR_DEBUG("BLE subscribe event, notify=%d",
                 p_event->ble_event.subscribe.cur_notify);
    } break;

    default:
        break;
    }
}

OPERATE_RET buddy_ble_init(buddy_ble_recv_cb_t recv_cb)
{
    s_recv_cb = recv_cb;
    s_connected = false;
    return tal_ble_bt_init(TAL_BLE_ROLE_PERIPERAL, ble_event_callback);
}

OPERATE_RET buddy_ble_send(const uint8_t *data, uint16_t len)
{
    if (!s_connected || !data || len == 0) {
        return OPRT_INVALID_PARM;
    }

    TAL_BLE_DATA_T pkt = {
        .p_data = (uint8_t *)data,
        .len = len,
    };
    return tal_ble_server_common_send(&pkt);
}

bool buddy_ble_is_connected(void)
{
    return s_connected;
}
```

- [ ] **Step 2: Build and verify compilation**

```bash
cd /home/share/samba/tyopen/TuyaOpen
tos build tuya_t5_pocket_buddy
```

Expected: Compiles without errors.

- [ ] **Step 3: Commit BLE bridge**

```bash
git add apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/buddy_ble.c
git commit -m "feat(buddy): implement BLE peripheral bridge"
```

---

## Task 3: JSON Protocol Parser

**Files:**
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/buddy_protocol.c`

The Claude Desktop app sends JSON lines over BLE. Two main message types:
1. **Heartbeat**: `{"total":3, "running":1, "waiting":0, "msg":"Working...", "tokens":12000}`
2. **Permission**: `{"prompt":{"id":"abc", "tool":"Read", "hint":"src/main.cpp"}}`

Device responds with: `{"cmd":"permission", "id":"abc", "decision":"once"}`

- [ ] **Step 1: Implement buddy_protocol.c**

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/buddy_protocol.c`:

```c
#include "buddy_protocol.h"
#include "buddy_ble.h"
#include "cJSON.h"
#include "tal_log.h"

#include <string.h>

OPERATE_RET buddy_protocol_parse(const char *json_line, buddy_msg_t *out)
{
    if (!json_line || !out) {
        return OPRT_INVALID_PARM;
    }

    memset(out, 0, sizeof(buddy_msg_t));
    out->type = BUDDY_MSG_NONE;

    cJSON *root = cJSON_Parse(json_line);
    if (!root) {
        PR_ERR("JSON parse failed");
        return OPRT_CJSON_PARSE_ERR;
    }

    cJSON *prompt = cJSON_GetObjectItem(root, "prompt");
    if (prompt && cJSON_IsObject(prompt)) {
        out->type = BUDDY_MSG_PERMISSION;
        buddy_permission_req_t *req = &out->permission;

        cJSON *id = cJSON_GetObjectItem(prompt, "id");
        if (id && cJSON_IsString(id)) {
            strncpy(req->id, id->valuestring, sizeof(req->id) - 1);
        }

        cJSON *tool = cJSON_GetObjectItem(prompt, "tool");
        if (tool && cJSON_IsString(tool)) {
            strncpy(req->tool, tool->valuestring, sizeof(req->tool) - 1);
        }

        cJSON *hint = cJSON_GetObjectItem(prompt, "hint");
        if (hint && cJSON_IsString(hint)) {
            strncpy(req->hint, hint->valuestring, sizeof(req->hint) - 1);
        }

        PR_DEBUG("Parsed permission: id=%s tool=%s hint=%s", req->id, req->tool, req->hint);
        cJSON_Delete(root);
        return OPRT_OK;
    }

    cJSON *total = cJSON_GetObjectItem(root, "total");
    if (total) {
        out->type = BUDDY_MSG_HEARTBEAT;
        buddy_heartbeat_t *hb = &out->heartbeat;

        hb->total_conversations = cJSON_IsNumber(total) ? total->valueint : 0;

        cJSON *running = cJSON_GetObjectItem(root, "running");
        hb->running_conversations = (running && cJSON_IsNumber(running)) ? running->valueint : 0;

        cJSON *waiting = cJSON_GetObjectItem(root, "waiting");
        hb->waiting_conversations = (waiting && cJSON_IsNumber(waiting)) ? waiting->valueint : 0;

        cJSON *msg = cJSON_GetObjectItem(root, "msg");
        if (msg && cJSON_IsString(msg)) {
            strncpy(hb->message, msg->valuestring, sizeof(hb->message) - 1);
        }

        cJSON *tokens = cJSON_GetObjectItem(root, "tokens");
        hb->tokens = (tokens && cJSON_IsNumber(tokens)) ? (uint32_t)tokens->valuedouble : 0;

        cJSON *ts = cJSON_GetObjectItem(root, "ts");
        hb->timestamp = (ts && cJSON_IsNumber(ts)) ? (uint32_t)ts->valuedouble : 0;

        PR_DEBUG("Parsed heartbeat: total=%d running=%d tokens=%u",
                 hb->total_conversations, hb->running_conversations, hb->tokens);
        cJSON_Delete(root);
        return OPRT_OK;
    }

    PR_WARN("Unknown JSON message type");
    cJSON_Delete(root);
    return OPRT_COM_ERROR;
}

OPERATE_RET buddy_protocol_send_permission_response(const char *id, const char *decision)
{
    if (!id || !decision) {
        return OPRT_INVALID_PARM;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return OPRT_MALLOC_FAILED;
    }

    cJSON_AddStringToObject(root, "cmd", "permission");
    cJSON_AddStringToObject(root, "id", id);
    cJSON_AddStringToObject(root, "decision", decision);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        return OPRT_MALLOC_FAILED;
    }

    size_t len = strlen(json_str);
    // Append newline for JSON line protocol
    char *line = tal_malloc(len + 2);
    if (!line) {
        cJSON_free(json_str);
        return OPRT_MALLOC_FAILED;
    }
    memcpy(line, json_str, len);
    line[len] = '\n';
    line[len + 1] = '\0';
    cJSON_free(json_str);

    OPERATE_RET ret = buddy_ble_send((const uint8_t *)line, len + 1);
    tal_free(line);
    return ret;
}
```

- [ ] **Step 2: Build and verify**

```bash
cd /home/share/samba/tyopen/TuyaOpen
tos build tuya_t5_pocket_buddy
```

- [ ] **Step 3: Commit protocol parser**

```bash
git add apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/buddy_protocol.c
git commit -m "feat(buddy): implement JSON protocol parser and response builder"
```

---

## Task 4: State Machine

**Files:**
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/buddy_state.c`

The state machine derives a `buddy_persona_state_t` from incoming data:
- No BLE connection → `PERSONA_SLEEP`
- Connected, no running conversations → `PERSONA_IDLE`
- Has running conversations → `PERSONA_BUSY`
- Permission request pending → `PERSONA_ATTENTION`
- Permission approved fast (< 5s) → `PERSONA_HEART` (2s, then back)
- Level up → `PERSONA_CELEBRATE` (3s, then back)
- Shake detected → `PERSONA_DIZZY` (2s, then back)

- [ ] **Step 1: Implement buddy_state.c**

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/buddy_state.c`:

```c
#include "buddy_state.h"
#include "buddy_stats.h"
#include "tal_log.h"
#include "tal_sw_timer.h"
#include "tal_system.h"

#include <string.h>

static buddy_persona_state_t s_current_state = PERSONA_SLEEP;
static buddy_state_change_cb_t s_change_cb = NULL;
static buddy_heartbeat_t s_last_heartbeat = {0};
static buddy_permission_req_t s_pending_permission = {0};
static bool s_has_pending_permission = false;
static bool s_ble_connected = false;
static uint32_t s_permission_recv_time = 0;
static TIMER_ID s_transient_timer = NULL;
static buddy_persona_state_t s_return_state = PERSONA_IDLE;

static void set_state(buddy_persona_state_t new_state)
{
    if (s_current_state == new_state) {
        return;
    }
    PR_DEBUG("State: %d -> %d", s_current_state, new_state);
    s_current_state = new_state;
    if (s_change_cb) {
        s_change_cb(new_state);
    }
}

static buddy_persona_state_t derive_base_state(void)
{
    if (!s_ble_connected) {
        return PERSONA_SLEEP;
    }
    if (s_has_pending_permission) {
        return PERSONA_ATTENTION;
    }
    if (s_last_heartbeat.running_conversations > 0) {
        return PERSONA_BUSY;
    }
    return PERSONA_IDLE;
}

static void transient_timer_cb(TIMER_ID timer_id, void *arg)
{
    set_state(s_return_state);
}

static void enter_transient_state(buddy_persona_state_t state, uint32_t duration_ms)
{
    s_return_state = derive_base_state();
    set_state(state);
    tal_sw_timer_start(s_transient_timer, duration_ms, TAL_TIMER_ONCE);
}

OPERATE_RET buddy_state_init(buddy_state_change_cb_t cb)
{
    s_change_cb = cb;
    s_current_state = PERSONA_SLEEP;
    s_ble_connected = false;
    s_has_pending_permission = false;
    return tal_sw_timer_create(transient_timer_cb, NULL, &s_transient_timer);
}

void buddy_state_on_heartbeat(const buddy_heartbeat_t *hb)
{
    if (!hb) return;

    uint32_t prev_tokens = s_last_heartbeat.tokens;
    memcpy(&s_last_heartbeat, hb, sizeof(buddy_heartbeat_t));

    if (hb->tokens > prev_tokens) {
        buddy_stats_add_tokens(hb->tokens - prev_tokens);
    }

    uint16_t prev_level = buddy_stats_get()->level;

    if (buddy_stats_get()->level > prev_level) {
        enter_transient_state(PERSONA_CELEBRATE, 3000);
        return;
    }

    set_state(derive_base_state());
}

void buddy_state_on_permission(const buddy_permission_req_t *req)
{
    if (!req) return;

    memcpy(&s_pending_permission, req, sizeof(buddy_permission_req_t));
    s_has_pending_permission = true;
    s_permission_recv_time = tal_system_get_millisecond();

    set_state(PERSONA_ATTENTION);
}

void buddy_state_on_permission_resolved(void)
{
    uint32_t elapsed = tal_system_get_millisecond() - s_permission_recv_time;
    s_has_pending_permission = false;

    if (elapsed < 5000) {
        buddy_stats_record_approval(elapsed);
        enter_transient_state(PERSONA_HEART, 2000);
    } else {
        buddy_stats_record_approval(elapsed);
        set_state(derive_base_state());
    }
}

void buddy_state_on_ble_connected(void)
{
    s_ble_connected = true;
    memset(&s_last_heartbeat, 0, sizeof(buddy_heartbeat_t));
    set_state(PERSONA_IDLE);
}

void buddy_state_on_ble_disconnected(void)
{
    s_ble_connected = false;
    s_has_pending_permission = false;
    set_state(PERSONA_SLEEP);
}

void buddy_state_on_shake(void)
{
    if (s_current_state == PERSONA_SLEEP) return;
    enter_transient_state(PERSONA_DIZZY, 2000);
}

buddy_persona_state_t buddy_state_get(void)
{
    return s_current_state;
}

const buddy_permission_req_t *buddy_state_get_pending_permission(void)
{
    return s_has_pending_permission ? &s_pending_permission : NULL;
}
```

- [ ] **Step 2: Build and verify**

```bash
cd /home/share/samba/tyopen/TuyaOpen
tos build tuya_t5_pocket_buddy
```

- [ ] **Step 3: Commit state machine**

```bash
git add apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/buddy_state.c
git commit -m "feat(buddy): implement 7-state persona state machine"
```

---

## Task 5: Statistics and Persistence

**Files:**
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/buddy_stats.c`

- [ ] **Step 1: Implement buddy_stats.c**

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/buddy_stats.c`:

```c
#include "buddy_stats.h"
#include "tal_kv.h"
#include "tal_log.h"

#include <string.h>

#define KV_KEY_STATS "buddy_stats"
#define TOKENS_PER_LEVEL 50000
#define TOKENS_PER_FOOD  5000

static buddy_stats_t s_stats = {0};

static void recalculate_derived(void)
{
    s_stats.level = (uint16_t)(s_stats.total_tokens / TOKENS_PER_LEVEL);
    s_stats.food = (uint8_t)((s_stats.total_tokens % TOKENS_PER_LEVEL) / TOKENS_PER_FOOD);
    if (s_stats.food > 100) s_stats.food = 100;

    uint16_t total = s_stats.approvals + s_stats.rejections;
    if (total == 0) {
        s_stats.mood = 50;
    } else {
        uint16_t rejection_pct = (s_stats.rejections * 100) / total;
        s_stats.mood = (uint8_t)(100 - rejection_pct);
    }
}

OPERATE_RET buddy_stats_init(void)
{
    uint8_t *buf = NULL;
    size_t len = 0;

    if (tal_kv_get(KV_KEY_STATS, &buf, &len) == OPRT_OK
        && len == sizeof(buddy_stats_t) && buf != NULL) {
        memcpy(&s_stats, buf, sizeof(buddy_stats_t));
        tal_free(buf);
        PR_INFO("Stats loaded: tokens=%u level=%u approvals=%u",
                s_stats.total_tokens, s_stats.level, s_stats.approvals);
    } else {
        if (buf) tal_free(buf);
        memset(&s_stats, 0, sizeof(buddy_stats_t));
        s_stats.mood = 50;
        s_stats.energy = 100;
        PR_INFO("Stats initialized to defaults");
    }

    recalculate_derived();
    return OPRT_OK;
}

void buddy_stats_add_tokens(uint32_t tokens)
{
    s_stats.total_tokens += tokens;
    recalculate_derived();
    buddy_stats_save();
}

void buddy_stats_record_approval(uint32_t response_ms)
{
    s_stats.approvals++;
    recalculate_derived();
    buddy_stats_save();
}

void buddy_stats_record_rejection(void)
{
    s_stats.rejections++;
    recalculate_derived();
    buddy_stats_save();
}

void buddy_stats_add_energy(uint8_t amount)
{
    uint16_t val = s_stats.energy + amount;
    s_stats.energy = (val > 100) ? 100 : (uint8_t)val;
    buddy_stats_save();
}

const buddy_stats_t *buddy_stats_get(void)
{
    return &s_stats;
}

OPERATE_RET buddy_stats_save(void)
{
    return tal_kv_set(KV_KEY_STATS, (const uint8_t *)&s_stats, sizeof(buddy_stats_t));
}
```

- [ ] **Step 2: Build and verify**

```bash
cd /home/share/samba/tyopen/TuyaOpen
tos build tuya_t5_pocket_buddy
```

- [ ] **Step 3: Commit stats module**

```bash
git add apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/buddy_stats.c
git commit -m "feat(buddy): implement stats tracking and KV persistence"
```

---

## Task 6: Copy Shared Display Assets

**Files:**
- Copy: Screen manager from existing app
- Copy: Animation GIFs (ducky assets as placeholder)
- Copy: Icons (battery, wifi)
- Copy: Fonts (terminus)

The existing `tuya_t5_pocket_ai` app has all the display assets we need. We copy the screen manager and assets, then build custom screens on top.

- [ ] **Step 1: Copy screen manager**

```bash
APP_SRC=apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ui
APP_DST=apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui

cp "$APP_SRC/screen_manager.c" "$APP_DST/"
cp "$APP_SRC/screen_manager.h" "$APP_DST/"
```

- [ ] **Step 2: Copy animation assets**

```bash
ANIM_SRC=apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/ducky
ANIM_DST=apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/anim

# Copy key animations that map to buddy persona states
cp "$ANIM_SRC/ducky_stand_still.c" "$ANIM_DST/"
cp "$ANIM_SRC/ducky_walk.c" "$ANIM_DST/"
cp "$ANIM_SRC/ducky_walk_to_left.c" "$ANIM_DST/"
cp "$ANIM_SRC/ducky_blink.c" "$ANIM_DST/"
cp "$ANIM_SRC/ducky_sleep.c" "$ANIM_DST/"
cp "$ANIM_SRC/ducky_dance.c" "$ANIM_DST/"
cp "$ANIM_SRC/ducky_eat.c" "$ANIM_DST/"
cp "$ANIM_SRC/ducky_sick.c" "$ANIM_DST/"
cp "$ANIM_SRC/ducky_emotion_happy.c" "$ANIM_DST/"
cp "$ANIM_SRC/ducky_emotion_angry.c" "$ANIM_DST/"
cp "$ANIM_SRC/ducky_emotion_cry.c" "$ANIM_DST/"
```

- [ ] **Step 3: Copy icons and fonts**

```bash
ICON_SRC=apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/icons
ICON_DST=apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/icons

# Copy essential icons only
cp "$ICON_SRC"/battery_*.c "$ICON_DST/"
cp "$ICON_SRC"/wifi_*.c "$ICON_DST/"

FONT_SRC=apps/tuya_t5_pocket/tuya_t5_pocket_ai/src/display/fonts
FONT_DST=apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/fonts

cp "$FONT_SRC"/lv_font_terminusTTF_Bold_*.c "$FONT_DST/"
```

- [ ] **Step 4: Build and verify assets compile**

```bash
cd /home/share/samba/tyopen/TuyaOpen
tos build tuya_t5_pocket_buddy
```

- [ ] **Step 5: Commit shared assets**

```bash
git add apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/
git commit -m "feat(buddy): copy shared display assets (screen manager, animations, icons, fonts)"
```

---

## Task 7: Main Screen (Pet Animation + HUD)

**Files:**
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_main_screen.h`
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_main_screen.c`

This is the primary display: a ducky pet animation in the center, with a BLE connection icon and status message overlay. The persona state maps to ducky animations:

| PersonaState | Ducky Animation | Visual |
|-------------|----------------|--------|
| SLEEP | ducky_sleep | Eyes closed, slow breathing |
| IDLE | ducky_walk/blink/stand | Walking around, blinking |
| BUSY | ducky_dance | Active, moving fast |
| ATTENTION | ducky_sick | Alert, LED blinking (repurpose "sick" as "alert") |
| HEART | ducky_emotion_happy | Floating hearts |
| CELEBRATE | ducky_dance | Celebration animation |
| DIZZY | ducky_emotion_cry | Spiral eyes |

- [ ] **Step 1: Create buddy_main_screen.h**

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_main_screen.h`:

```c
#ifndef __BUDDY_MAIN_SCREEN_H__
#define __BUDDY_MAIN_SCREEN_H__

#include "screen_manager.h"
#include "buddy_state.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Screen_t buddy_main_screen;

void buddy_main_screen_set_persona(buddy_persona_state_t state);
void buddy_main_screen_set_ble_status(bool connected);
void buddy_main_screen_set_message(const char *msg);

#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 2: Create buddy_main_screen.c**

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_main_screen.c`:

```c
#include "buddy_main_screen.h"
#include "lv_vendor.h"
#include "tal_log.h"
#include "tal_system.h"

#include <string.h>

/* GIF declarations from ducky animation assets */
LV_IMG_DECLARE(ducky_stand_still);
LV_IMG_DECLARE(ducky_walk);
LV_IMG_DECLARE(ducky_walk_to_left);
LV_IMG_DECLARE(ducky_blink);
LV_IMG_DECLARE(ducky_sleep);
LV_IMG_DECLARE(ducky_dance);
LV_IMG_DECLARE(ducky_eat);
LV_IMG_DECLARE(ducky_sick);
LV_IMG_DECLARE(ducky_emotion_happy);
LV_IMG_DECLARE(ducky_emotion_angry);
LV_IMG_DECLARE(ducky_emotion_cry);

#define PET_MOVEMENT_STEP    2
#define PET_MOVEMENT_LIMIT   80
#define ANIM_TIMER_MS        100
#define MOVEMENT_TIMER_MS    200

static lv_obj_t *ui_main_screen = NULL;

/* Pet GIF objects */
static lv_obj_t *gif_stand = NULL;
static lv_obj_t *gif_walk = NULL;
static lv_obj_t *gif_walk_left = NULL;
static lv_obj_t *gif_blink = NULL;
static lv_obj_t *gif_sleep = NULL;
static lv_obj_t *gif_dance = NULL;
static lv_obj_t *gif_sick = NULL;
static lv_obj_t *gif_happy = NULL;
static lv_obj_t *gif_cry = NULL;

static lv_obj_t *current_gif = NULL;

/* HUD elements */
static lv_obj_t *lbl_ble_status = NULL;
static lv_obj_t *lbl_message = NULL;
static lv_obj_t *lbl_state = NULL;

/* Animation state */
static buddy_persona_state_t s_persona = PERSONA_SLEEP;
static int16_t s_pet_x = 0;
static int8_t s_pet_dir = 1;
static bool s_walking = false;
static uint32_t s_walk_start = 0;
static uint32_t s_walk_duration = 4000;
static uint32_t s_idle_start = 0;
static uint32_t s_idle_duration = 3000;

static lv_timer_t *s_anim_timer = NULL;
static lv_timer_t *s_movement_timer = NULL;

static const char *persona_name(buddy_persona_state_t s)
{
    static const char *names[] = {
        "SLEEP", "IDLE", "BUSY", "ATTENTION", "HEART", "CELEBRATE", "DIZZY"
    };
    return (s < PERSONA_STATE_COUNT) ? names[s] : "???";
}

static void show_gif(lv_obj_t *gif)
{
    if (gif == current_gif) return;

    lv_obj_t *all[] = {gif_stand, gif_walk, gif_walk_left, gif_blink,
                       gif_sleep, gif_dance, gif_sick, gif_happy, gif_cry};

    for (int i = 0; i < (int)(sizeof(all)/sizeof(all[0])); i++) {
        if (all[i]) lv_obj_add_flag(all[i], LV_OBJ_FLAG_HIDDEN);
    }

    if (gif) lv_obj_clear_flag(gif, LV_OBJ_FLAG_HIDDEN);
    current_gif = gif;
}

static lv_obj_t *gif_for_persona(buddy_persona_state_t state)
{
    switch (state) {
    case PERSONA_SLEEP:     return gif_sleep;
    case PERSONA_IDLE:
        if (s_walking) {
            return (s_pet_dir > 0) ? gif_walk : gif_walk_left;
        }
        return gif_stand;
    case PERSONA_BUSY:      return gif_dance;
    case PERSONA_ATTENTION: return gif_sick;
    case PERSONA_HEART:     return gif_happy;
    case PERSONA_CELEBRATE: return gif_dance;
    case PERSONA_DIZZY:     return gif_cry;
    default:                return gif_stand;
    }
}

static void anim_timer_cb(lv_timer_t *timer)
{
    show_gif(gif_for_persona(s_persona));
}

static void movement_timer_cb(lv_timer_t *timer)
{
    if (s_persona != PERSONA_IDLE && s_persona != PERSONA_BUSY) {
        return;
    }

    uint32_t now = tal_system_get_millisecond();

    if (s_walking) {
        s_pet_x += s_pet_dir * PET_MOVEMENT_STEP;
        if (s_pet_x > PET_MOVEMENT_LIMIT || s_pet_x < -PET_MOVEMENT_LIMIT) {
            s_pet_dir = -s_pet_dir;
            s_pet_x += s_pet_dir * PET_MOVEMENT_STEP;
        }
        if (current_gif) {
            lv_obj_set_x(current_gif, s_pet_x);
        }
        if (now - s_walk_start > s_walk_duration) {
            s_walking = false;
            s_idle_start = now;
            s_idle_duration = 3000 + (tal_system_get_millisecond() % 7000);
        }
    } else {
        if (now - s_idle_start > s_idle_duration) {
            s_walking = true;
            s_walk_start = now;
            s_walk_duration = 2000 + (tal_system_get_millisecond() % 6000);
        }
    }
}

static lv_obj_t *create_gif(lv_obj_t *parent, const void *src)
{
    lv_obj_t *gif = lv_gif_create(parent);
    lv_gif_set_src(gif, src);
    lv_obj_center(gif);
    lv_obj_add_flag(gif, LV_OBJ_FLAG_HIDDEN);
    return gif;
}

static void keyboard_event_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    PR_DEBUG("Key: %u", key);
    /* Key handling delegated to Task 9 */
}

static void buddy_main_screen_init(void)
{
    ui_main_screen = lv_obj_create(NULL);
    lv_obj_set_size(ui_main_screen, 384, 168);
    lv_obj_set_style_bg_color(ui_main_screen, lv_color_white(), 0);

    /* Pet area container */
    lv_obj_t *pet_area = lv_obj_create(ui_main_screen);
    lv_obj_set_size(pet_area, 384, 120);
    lv_obj_set_pos(pet_area, 0, 24);
    lv_obj_set_style_bg_opa(pet_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pet_area, 0, 0);
    lv_obj_set_style_pad_all(pet_area, 0, 0);
    lv_obj_set_scrollbar_mode(pet_area, LV_SCROLLBAR_MODE_OFF);

    /* Create all GIF objects (pre-loaded) */
    gif_stand     = create_gif(pet_area, &ducky_stand_still);
    gif_walk      = create_gif(pet_area, &ducky_walk);
    gif_walk_left = create_gif(pet_area, &ducky_walk_to_left);
    gif_blink     = create_gif(pet_area, &ducky_blink);
    gif_sleep     = create_gif(pet_area, &ducky_sleep);
    gif_dance     = create_gif(pet_area, &ducky_dance);
    gif_sick      = create_gif(pet_area, &ducky_sick);
    gif_happy     = create_gif(pet_area, &ducky_emotion_happy);
    gif_cry       = create_gif(pet_area, &ducky_emotion_cry);

    /* Status bar */
    lbl_ble_status = lv_label_create(ui_main_screen);
    lv_obj_set_pos(lbl_ble_status, 5, 4);
    lv_label_set_text(lbl_ble_status, "BLE: ---");
    lv_obj_set_style_text_color(lbl_ble_status, lv_color_black(), 0);

    lbl_state = lv_label_create(ui_main_screen);
    lv_obj_align(lbl_state, LV_ALIGN_TOP_RIGHT, -5, 4);
    lv_label_set_text(lbl_state, "SLEEP");
    lv_obj_set_style_text_color(lbl_state, lv_color_black(), 0);

    /* Divider line */
    lv_obj_t *line = lv_obj_create(ui_main_screen);
    lv_obj_set_size(line, 384, 2);
    lv_obj_set_pos(line, 0, 22);
    lv_obj_set_style_bg_color(line, lv_color_black(), 0);
    lv_obj_set_style_border_width(line, 0, 0);

    /* Bottom message bar */
    lbl_message = lv_label_create(ui_main_screen);
    lv_obj_set_pos(lbl_message, 5, 148);
    lv_label_set_text(lbl_message, "Waiting for connection...");
    lv_obj_set_style_text_color(lbl_message, lv_color_hex(0x666666), 0);
    lv_label_set_long_mode(lbl_message, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_message, 374);

    /* Keyboard input */
    lv_obj_add_event_cb(ui_main_screen, keyboard_event_cb, LV_EVENT_KEY, NULL);
    lv_group_t *g = lv_group_get_default();
    if (!g) {
        g = lv_group_create();
        lv_group_set_default(g);
    }
    lv_group_add_obj(g, ui_main_screen);
    lv_group_focus_obj(ui_main_screen);

    /* Start animation timers */
    s_anim_timer = lv_timer_create(anim_timer_cb, ANIM_TIMER_MS, NULL);
    s_movement_timer = lv_timer_create(movement_timer_cb, MOVEMENT_TIMER_MS, NULL);

    /* Show initial state */
    show_gif(gif_sleep);
    s_idle_start = tal_system_get_millisecond();

    PR_NOTICE("Buddy main screen initialized");
}

static void buddy_main_screen_deinit(void)
{
    if (s_anim_timer) {
        lv_timer_delete(s_anim_timer);
        s_anim_timer = NULL;
    }
    if (s_movement_timer) {
        lv_timer_delete(s_movement_timer);
        s_movement_timer = NULL;
    }

    current_gif = NULL;
    gif_stand = gif_walk = gif_walk_left = gif_blink = NULL;
    gif_sleep = gif_dance = gif_sick = gif_happy = gif_cry = NULL;
    lbl_ble_status = lbl_message = lbl_state = NULL;

    if (ui_main_screen) {
        lv_obj_del(ui_main_screen);
        ui_main_screen = NULL;
    }
}

Screen_t buddy_main_screen = {
    .init       = buddy_main_screen_init,
    .deinit     = buddy_main_screen_deinit,
    .screen_obj = &ui_main_screen,
    .name       = "BuddyMain",
};

void buddy_main_screen_set_persona(buddy_persona_state_t state)
{
    s_persona = state;
    if (lbl_state) {
        lv_vendor_disp_lock();
        lv_label_set_text(lbl_state, persona_name(state));
        lv_vendor_disp_unlock();
    }
}

void buddy_main_screen_set_ble_status(bool connected)
{
    if (lbl_ble_status) {
        lv_vendor_disp_lock();
        lv_label_set_text(lbl_ble_status, connected ? "BLE: Connected" : "BLE: ---");
        lv_vendor_disp_unlock();
    }
}

void buddy_main_screen_set_message(const char *msg)
{
    if (lbl_message && msg) {
        lv_vendor_disp_lock();
        lv_label_set_text(lbl_message, msg);
        lv_vendor_disp_unlock();
    }
}
```

- [ ] **Step 3: Build and verify**

```bash
cd /home/share/samba/tyopen/TuyaOpen
tos build tuya_t5_pocket_buddy
```

- [ ] **Step 4: Commit main screen**

```bash
git add apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_main_screen.*
git commit -m "feat(buddy): implement main screen with persona-driven GIF animations"
```

---

## Task 8: Approval Screen

**Files:**
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_approval_screen.h`
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_approval_screen.c`

When a permission request arrives, this screen overlays on top showing the tool name and hint. Physical buttons: Enter = Approve, Esc = Reject.

- [ ] **Step 1: Create buddy_approval_screen.h**

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_approval_screen.h`:

```c
#ifndef __BUDDY_APPROVAL_SCREEN_H__
#define __BUDDY_APPROVAL_SCREEN_H__

#include "screen_manager.h"
#include "buddy_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*buddy_approval_result_cb_t)(const char *id, bool approved);

extern Screen_t buddy_approval_screen;

void buddy_approval_screen_set_request(const buddy_permission_req_t *req);
void buddy_approval_screen_set_callback(buddy_approval_result_cb_t cb);

#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 2: Create buddy_approval_screen.c**

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_approval_screen.c`:

```c
#include "buddy_approval_screen.h"
#include "lv_vendor.h"
#include "tal_log.h"

#include <string.h>

static lv_obj_t *ui_approval_screen = NULL;
static lv_obj_t *lbl_title = NULL;
static lv_obj_t *lbl_tool = NULL;
static lv_obj_t *lbl_hint = NULL;
static lv_obj_t *lbl_buttons = NULL;

static buddy_permission_req_t s_request = {0};
static buddy_approval_result_cb_t s_result_cb = NULL;

static void keyboard_event_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);

    if (key == 10) { /* KEY_ENTER = Approve */
        PR_NOTICE("Permission APPROVED: %s", s_request.id);
        if (s_result_cb) {
            s_result_cb(s_request.id, true);
        }
        screen_back();
    } else if (key == 27) { /* KEY_ESC = Reject */
        PR_NOTICE("Permission REJECTED: %s", s_request.id);
        if (s_result_cb) {
            s_result_cb(s_request.id, false);
        }
        screen_back();
    }
}

static void buddy_approval_screen_init(void)
{
    ui_approval_screen = lv_obj_create(NULL);
    lv_obj_set_size(ui_approval_screen, 384, 168);
    lv_obj_set_style_bg_color(ui_approval_screen, lv_color_hex(0xFFF3E0), 0);

    lbl_title = lv_label_create(ui_approval_screen);
    lv_obj_set_pos(lbl_title, 10, 10);
    lv_label_set_text(lbl_title, "PERMISSION REQUEST");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xE65100), 0);

    lv_obj_t *line = lv_obj_create(ui_approval_screen);
    lv_obj_set_size(line, 364, 2);
    lv_obj_set_pos(line, 10, 32);
    lv_obj_set_style_bg_color(line, lv_color_hex(0xE65100), 0);
    lv_obj_set_style_border_width(line, 0, 0);

    lbl_tool = lv_label_create(ui_approval_screen);
    lv_obj_set_pos(lbl_tool, 10, 42);
    lv_label_set_text_fmt(lbl_tool, "Tool: %s", s_request.tool);
    lv_obj_set_style_text_color(lbl_tool, lv_color_black(), 0);

    lbl_hint = lv_label_create(ui_approval_screen);
    lv_obj_set_pos(lbl_hint, 10, 66);
    lv_label_set_text_fmt(lbl_hint, "File: %s", s_request.hint);
    lv_obj_set_style_text_color(lbl_hint, lv_color_hex(0x666666), 0);
    lv_label_set_long_mode(lbl_hint, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_hint, 364);

    lbl_buttons = lv_label_create(ui_approval_screen);
    lv_obj_align(lbl_buttons, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_label_set_text(lbl_buttons, "[ENTER] Approve    [ESC] Reject");
    lv_obj_set_style_text_color(lbl_buttons, lv_color_hex(0x333333), 0);

    lv_obj_add_event_cb(ui_approval_screen, keyboard_event_cb, LV_EVENT_KEY, NULL);
    lv_group_t *g = lv_group_get_default();
    if (!g) {
        g = lv_group_create();
        lv_group_set_default(g);
    }
    lv_group_add_obj(g, ui_approval_screen);
    lv_group_focus_obj(ui_approval_screen);

    PR_NOTICE("Approval screen: tool=%s hint=%s", s_request.tool, s_request.hint);
}

static void buddy_approval_screen_deinit(void)
{
    lbl_title = lbl_tool = lbl_hint = lbl_buttons = NULL;
    if (ui_approval_screen) {
        lv_obj_del(ui_approval_screen);
        ui_approval_screen = NULL;
    }
}

Screen_t buddy_approval_screen = {
    .init       = buddy_approval_screen_init,
    .deinit     = buddy_approval_screen_deinit,
    .screen_obj = &ui_approval_screen,
    .name       = "Approval",
};

void buddy_approval_screen_set_request(const buddy_permission_req_t *req)
{
    if (req) {
        memcpy(&s_request, req, sizeof(buddy_permission_req_t));
    }
}

void buddy_approval_screen_set_callback(buddy_approval_result_cb_t cb)
{
    s_result_cb = cb;
}
```

- [ ] **Step 3: Build and verify**

```bash
cd /home/share/samba/tyopen/TuyaOpen
tos build tuya_t5_pocket_buddy
```

- [ ] **Step 4: Commit approval screen**

```bash
git add apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_approval_screen.*
git commit -m "feat(buddy): implement permission approval screen with Enter/Esc buttons"
```

---

## Task 9: Stats Screen

**Files:**
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_stats_screen.h`
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_stats_screen.c`

Displays mood, food, energy, and level as progress bars. Accessible via button press from main screen.

- [ ] **Step 1: Create buddy_stats_screen.h**

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_stats_screen.h`:

```c
#ifndef __BUDDY_STATS_SCREEN_H__
#define __BUDDY_STATS_SCREEN_H__

#include "screen_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Screen_t buddy_stats_screen;

void buddy_stats_screen_refresh(void);

#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 2: Create buddy_stats_screen.c**

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_stats_screen.c`:

```c
#include "buddy_stats_screen.h"
#include "buddy_stats.h"
#include "lv_vendor.h"
#include "tal_log.h"

static lv_obj_t *ui_stats_screen = NULL;
static lv_obj_t *bar_mood = NULL;
static lv_obj_t *bar_food = NULL;
static lv_obj_t *bar_energy = NULL;
static lv_obj_t *lbl_level = NULL;
static lv_obj_t *lbl_tokens = NULL;
static lv_obj_t *lbl_approvals = NULL;

static void keyboard_event_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == 27) { /* ESC = back */
        screen_back();
    }
}

static lv_obj_t *create_stat_row(lv_obj_t *parent, int y, const char *label_text,
                                  lv_color_t color, lv_obj_t **bar_out)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_pos(lbl, 10, y);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, lv_color_black(), 0);

    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, 100, y);
    lv_obj_set_size(bar, 200, 18);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 50, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);

    *bar_out = bar;
    return lbl;
}

static void buddy_stats_screen_init(void)
{
    ui_stats_screen = lv_obj_create(NULL);
    lv_obj_set_size(ui_stats_screen, 384, 168);
    lv_obj_set_style_bg_color(ui_stats_screen, lv_color_white(), 0);

    lv_obj_t *title = lv_label_create(ui_stats_screen);
    lv_obj_set_pos(title, 10, 5);
    lv_label_set_text(title, "BUDDY STATS");
    lv_obj_set_style_text_color(title, lv_color_black(), 0);

    create_stat_row(ui_stats_screen, 30, "Mood:",   lv_color_hex(0xFF9800), &bar_mood);
    create_stat_row(ui_stats_screen, 55, "Food:",   lv_color_hex(0x4CAF50), &bar_food);
    create_stat_row(ui_stats_screen, 80, "Energy:", lv_color_hex(0x2196F3), &bar_energy);

    lbl_level = lv_label_create(ui_stats_screen);
    lv_obj_set_pos(lbl_level, 10, 110);
    lv_obj_set_style_text_color(lbl_level, lv_color_black(), 0);

    lbl_tokens = lv_label_create(ui_stats_screen);
    lv_obj_set_pos(lbl_tokens, 200, 110);
    lv_obj_set_style_text_color(lbl_tokens, lv_color_hex(0x666666), 0);

    lbl_approvals = lv_label_create(ui_stats_screen);
    lv_obj_set_pos(lbl_approvals, 10, 135);
    lv_obj_set_style_text_color(lbl_approvals, lv_color_hex(0x666666), 0);

    buddy_stats_screen_refresh();

    lv_obj_add_event_cb(ui_stats_screen, keyboard_event_cb, LV_EVENT_KEY, NULL);
    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, ui_stats_screen);
        lv_group_focus_obj(ui_stats_screen);
    }
}

static void buddy_stats_screen_deinit(void)
{
    bar_mood = bar_food = bar_energy = NULL;
    lbl_level = lbl_tokens = lbl_approvals = NULL;
    if (ui_stats_screen) {
        lv_obj_del(ui_stats_screen);
        ui_stats_screen = NULL;
    }
}

Screen_t buddy_stats_screen = {
    .init       = buddy_stats_screen_init,
    .deinit     = buddy_stats_screen_deinit,
    .screen_obj = &ui_stats_screen,
    .name       = "Stats",
};

void buddy_stats_screen_refresh(void)
{
    const buddy_stats_t *stats = buddy_stats_get();
    if (!stats) return;

    if (bar_mood)   lv_bar_set_value(bar_mood, stats->mood, LV_ANIM_ON);
    if (bar_food)   lv_bar_set_value(bar_food, stats->food, LV_ANIM_ON);
    if (bar_energy) lv_bar_set_value(bar_energy, stats->energy, LV_ANIM_ON);

    if (lbl_level)
        lv_label_set_text_fmt(lbl_level, "Level: %u", stats->level);
    if (lbl_tokens)
        lv_label_set_text_fmt(lbl_tokens, "Tokens: %uK", stats->total_tokens / 1000);
    if (lbl_approvals)
        lv_label_set_text_fmt(lbl_approvals, "Approved: %u  Rejected: %u",
                              stats->approvals, stats->rejections);
}
```

- [ ] **Step 3: Build and verify**

```bash
cd /home/share/samba/tyopen/TuyaOpen
tos build tuya_t5_pocket_buddy
```

- [ ] **Step 4: Commit stats screen**

```bash
git add apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/display/ui/buddy_stats_screen.*
git commit -m "feat(buddy): implement stats screen with mood/food/energy bars"
```

---

## Task 10: Integration - Wire Everything Together

**Files:**
- Modify: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/tuya_main.c`
- Modify: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/include/buddy_display.h`
- Create: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/buddy_display.c`
- Modify: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/buddy_ble.c` (add connect/disconnect hooks)

This task wires all subsystems together:
- BLE recv → protocol parse → state machine → display update
- BLE connect/disconnect → state machine → display
- Approval result → protocol response → BLE send

- [ ] **Step 1: Create buddy_display.c**

Write `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/buddy_display.c`:

```c
#include "buddy_display.h"
#include "buddy_main_screen.h"
#include "buddy_approval_screen.h"
#include "buddy_stats_screen.h"
#include "screen_manager.h"
#include "lv_vendor.h"
#include "tal_log.h"

OPERATE_RET buddy_display_init(void)
{
    lv_vendor_init(DISPLAY_NAME);
    screens_init();
    screen_load(&buddy_main_screen);
    lv_vendor_start(5, 1024 * 8);
    PR_NOTICE("Buddy display initialized");
    return OPRT_OK;
}

void buddy_display_set_persona(buddy_persona_state_t state)
{
    buddy_main_screen_set_persona(state);
}

void buddy_display_set_ble_connected(bool connected)
{
    buddy_main_screen_set_ble_status(connected);
}

void buddy_display_show_approval(const buddy_permission_req_t *req)
{
    if (!req) return;
    buddy_approval_screen_set_request(req);
    lv_vendor_disp_lock();
    screen_load(&buddy_approval_screen);
    lv_vendor_disp_unlock();
}
```

- [ ] **Step 2: Update buddy_ble.c to hook connect/disconnect**

Add a connection state change callback to buddy_ble.h. Update the header:

In `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/include/buddy_ble.h`, change the init signature:

```c
#ifndef __BUDDY_BLE_H__
#define __BUDDY_BLE_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*buddy_ble_recv_cb_t)(const uint8_t *data, uint16_t len);
typedef void (*buddy_ble_conn_cb_t)(bool connected);

OPERATE_RET buddy_ble_init(buddy_ble_recv_cb_t recv_cb, buddy_ble_conn_cb_t conn_cb);
OPERATE_RET buddy_ble_send(const uint8_t *data, uint16_t len);
bool buddy_ble_is_connected(void);

#ifdef __cplusplus
}
#endif
#endif
```

Update `buddy_ble.c` — add `s_conn_cb` field and invoke it on connect/disconnect:

In the `ble_event_callback` function, after `case TAL_BLE_EVT_PERIPHERAL_CONNECT` sets `s_connected = true`:
```c
if (s_conn_cb) s_conn_cb(true);
```

After `case TAL_BLE_EVT_DISCONNECT` sets `s_connected = false`:
```c
if (s_conn_cb) s_conn_cb(false);
```

Update `buddy_ble_init` to accept and store `conn_cb`:
```c
static buddy_ble_conn_cb_t s_conn_cb = NULL;

OPERATE_RET buddy_ble_init(buddy_ble_recv_cb_t recv_cb, buddy_ble_conn_cb_t conn_cb)
{
    s_recv_cb = recv_cb;
    s_conn_cb = conn_cb;
    s_connected = false;
    return tal_ble_bt_init(TAL_BLE_ROLE_PERIPERAL, ble_event_callback);
}
```

- [ ] **Step 3: Rewrite tuya_main.c with full integration**

Replace `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/tuya_main.c`:

```c
#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tal_kv.h"
#include "tal_cli.h"
#include "tkl_output.h"
#include "board_com_api.h"

#include "buddy_ble.h"
#include "buddy_protocol.h"
#include "buddy_state.h"
#include "buddy_stats.h"
#include "buddy_display.h"
#include "buddy_approval_screen.h"
#include "buddy_main_screen.h"

#include <string.h>

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "0.0.1"
#endif

#define BLE_LINE_BUF_SIZE 512

static char s_line_buf[BLE_LINE_BUF_SIZE];
static uint16_t s_line_pos = 0;

static void on_state_change(buddy_persona_state_t new_state)
{
    buddy_display_set_persona(new_state);

    if (new_state == PERSONA_ATTENTION) {
        const buddy_permission_req_t *req = buddy_state_get_pending_permission();
        if (req) {
            buddy_display_show_approval(req);
        }
    }
}

static void on_approval_result(const char *id, bool approved)
{
    buddy_protocol_send_permission_response(id, approved ? "once" : "deny");

    if (approved) {
        buddy_state_on_permission_resolved();
    } else {
        buddy_stats_record_rejection();
        buddy_state_on_permission_resolved();
    }
}

static void process_json_line(const char *line)
{
    buddy_msg_t msg = {0};

    if (buddy_protocol_parse(line, &msg) != OPRT_OK) {
        return;
    }

    switch (msg.type) {
    case BUDDY_MSG_HEARTBEAT:
        buddy_state_on_heartbeat(&msg.heartbeat);
        if (msg.heartbeat.message[0]) {
            buddy_main_screen_set_message(msg.heartbeat.message);
        }
        break;

    case BUDDY_MSG_PERMISSION:
        buddy_state_on_permission(&msg.permission);
        break;

    default:
        break;
    }
}

static void on_ble_recv(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        char c = (char)data[i];

        if (c == '\n' || c == '\r') {
            if (s_line_pos > 0) {
                s_line_buf[s_line_pos] = '\0';
                process_json_line(s_line_buf);
                s_line_pos = 0;
            }
            continue;
        }

        if (s_line_pos < BLE_LINE_BUF_SIZE - 1) {
            s_line_buf[s_line_pos++] = c;
        }
    }
}

static void on_ble_conn(bool connected)
{
    PR_NOTICE("BLE connection: %s", connected ? "UP" : "DOWN");
    buddy_display_set_ble_connected(connected);

    if (connected) {
        buddy_state_on_ble_connected();
    } else {
        buddy_state_on_ble_disconnected();
        s_line_pos = 0;
    }
}

void user_main(void)
{
    int ret = OPRT_OK;

    cJSON_InitHooks(&(cJSON_Hooks){.malloc_fn = tal_malloc, .free_fn = tal_free});
    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, (TAL_LOG_OUTPUT_CB)tkl_log_output);

    PR_NOTICE("=== Claude Desktop Buddy ===");
    PR_NOTICE("Version:  %s", PROJECT_VERSION);
    PR_NOTICE("Platform: %s / %s", PLATFORM_CHIP, PLATFORM_BOARD);

    tal_kv_init(&(tal_kv_cfg_t){
        .seed = "buddy_kv_seed_01",
        .key  = "buddy_kv_key__01",
    });
    tal_sw_timer_init();
    tal_workq_init();
    tal_cli_init();

    ret = board_register_hardware();
    if (ret != OPRT_OK) {
        PR_ERR("board_register_hardware failed: %d", ret);
    }

    buddy_stats_init();
    buddy_state_init(on_state_change);
    buddy_display_init();
    buddy_approval_screen_set_callback(on_approval_result);

    ret = buddy_ble_init(on_ble_recv, on_ble_conn);
    if (ret != OPRT_OK) {
        PR_ERR("buddy_ble_init failed: %d", ret);
    }

    PR_NOTICE("All subsystems initialized, entering main loop");

    for (;;) {
        tal_system_sleep(1000);
    }
}

#if OPERATING_SYSTEM == SYSTEM_LINUX
void main(int argc, char *argv[])
{
    user_main();
}
#else
static THREAD_HANDLE ty_app_thread = NULL;

static void tuya_app_thread(void *arg)
{
    user_main();
    tal_thread_delete(ty_app_thread);
    ty_app_thread = NULL;
}

void tuya_app_main(void)
{
    THREAD_CFG_T thrd_param = {0};
    thrd_param.stackDepth   = 1024 * 6;
    thrd_param.priority     = THREAD_PRIO_1;
    thrd_param.thrdname     = "tuya_app_main";
    tal_thread_create_and_start(&ty_app_thread, NULL, NULL, tuya_app_thread, NULL, &thrd_param);
}
#endif
```

- [ ] **Step 4: Build the complete project**

```bash
cd /home/share/samba/tyopen/TuyaOpen
tos build tuya_t5_pocket_buddy
```

Fix any compilation errors.

- [ ] **Step 5: Commit integration**

```bash
git add apps/tuya_t5_pocket/tuya_t5_pocket_buddy/
git commit -m "feat(buddy): integrate all subsystems - BLE, protocol, state, display, stats"
```

---

## Task 11: Smoke Test via CLI Commands

**Files:**
- Modify: `apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/tuya_main.c` (add CLI test commands)

Add CLI commands to simulate BLE messages and test each subsystem independently without needing the Claude Desktop app.

- [ ] **Step 1: Add CLI test commands to tuya_main.c**

Add before `user_main()`:

```c
#include "tal_cli.h"

static void cli_buddy_test(int argc, char *argv[])
{
    if (argc < 2) {
        PR_NOTICE("Usage: buddy_test <heartbeat|permission|approve|reject|shake|stats>");
        return;
    }

    if (strcmp(argv[1], "heartbeat") == 0) {
        const char *json = "{\"total\":3,\"running\":1,\"waiting\":0,\"msg\":\"Working on feature...\",\"tokens\":12000}";
        buddy_msg_t msg = {0};
        buddy_protocol_parse(json, &msg);
        if (msg.type == BUDDY_MSG_HEARTBEAT) {
            buddy_state_on_heartbeat(&msg.heartbeat);
            buddy_main_screen_set_message(msg.heartbeat.message);
            PR_NOTICE("Simulated heartbeat");
        }
    } else if (strcmp(argv[1], "permission") == 0) {
        const char *json = "{\"prompt\":{\"id\":\"test123\",\"tool\":\"Read\",\"hint\":\"src/main.c\"}}";
        buddy_msg_t msg = {0};
        buddy_protocol_parse(json, &msg);
        if (msg.type == BUDDY_MSG_PERMISSION) {
            buddy_state_on_permission(&msg.permission);
            PR_NOTICE("Simulated permission request");
        }
    } else if (strcmp(argv[1], "approve") == 0) {
        buddy_state_on_permission_resolved();
        PR_NOTICE("Simulated approval");
    } else if (strcmp(argv[1], "reject") == 0) {
        buddy_stats_record_rejection();
        buddy_state_on_permission_resolved();
        PR_NOTICE("Simulated rejection");
    } else if (strcmp(argv[1], "shake") == 0) {
        buddy_state_on_shake();
        PR_NOTICE("Simulated shake");
    } else if (strcmp(argv[1], "connect") == 0) {
        on_ble_conn(true);
        PR_NOTICE("Simulated BLE connect");
    } else if (strcmp(argv[1], "disconnect") == 0) {
        on_ble_conn(false);
        PR_NOTICE("Simulated BLE disconnect");
    } else if (strcmp(argv[1], "stats") == 0) {
        const buddy_stats_t *s = buddy_stats_get();
        PR_NOTICE("Tokens: %u, Level: %u, Mood: %u, Food: %u, Energy: %u",
                  s->total_tokens, s->level, s->mood, s->food, s->energy);
        PR_NOTICE("Approvals: %u, Rejections: %u", s->approvals, s->rejections);
    }
}
```

In `user_main()`, after `tal_cli_init()`, add:

```c
tal_cli_cmd_register("buddy_test", cli_buddy_test);
```

- [ ] **Step 2: Build and verify**

```bash
cd /home/share/samba/tyopen/TuyaOpen
tos build tuya_t5_pocket_buddy
```

- [ ] **Step 3: Flash and test on device**

```bash
tos flash tuya_t5_pocket_buddy
```

Open serial monitor and run test commands:
```
buddy_test connect
buddy_test heartbeat
buddy_test permission
buddy_test approve
buddy_test shake
buddy_test stats
buddy_test disconnect
```

Expected behavior for each:
- `connect` → Screen shows "BLE: Connected", state changes from SLEEP to IDLE, pet starts walking
- `heartbeat` → State changes to BUSY (running=1), message bar updates
- `permission` → State changes to ATTENTION, approval screen appears
- `approve` → State changes to HEART (fast approval), returns to BUSY after 2s
- `shake` → State changes to DIZZY for 2s
- `stats` → Prints stats to serial console
- `disconnect` → State changes to SLEEP, BLE status shows "---"

- [ ] **Step 4: Commit CLI test commands**

```bash
git add apps/tuya_t5_pocket/tuya_t5_pocket_buddy/src/tuya_main.c
git commit -m "feat(buddy): add CLI test commands for smoke testing all subsystems"
```

---

## Architecture Summary

```
┌─────────────────────────────────────────────────────────┐
│ tuya_main.c                                             │
│  - Boot sequence, init all modules                      │
│  - JSON line buffer assembly                            │
│  - Wire callbacks between subsystems                    │
│  - CLI test commands                                    │
└────────┬──────────┬──────────┬───────────┬──────────────┘
         │          │          │           │
    ┌────▼────┐ ┌───▼───┐ ┌───▼───┐ ┌────▼──────┐
    │buddy_ble│ │buddy_ │ │buddy_ │ │buddy_     │
    │         │ │proto  │ │state  │ │stats      │
    │BLE init │ │JSON   │ │7-state│ │tokens,    │
    │adv,conn │ │parse  │ │machine│ │level,     │
    │send/recv│ │respond│ │derive │ │KV persist │
    └────┬────┘ └───┬───┘ └───┬───┘ └───────────┘
         │          │          │
         │     ┌────▼──────────▼────────────────┐
         │     │ buddy_display.c                 │
         │     │  - Display init                 │
         │     │  - Route state → screens        │
         │     └────┬───────────┬───────────┬───┘
         │     ┌────▼────┐ ┌───▼─────┐ ┌───▼───┐
         │     │ main    │ │approval │ │ stats │
         │     │ screen  │ │ screen  │ │screen │
         │     │GIF anim │ │approve/ │ │bars,  │
         │     │HUD,msg  │ │reject   │ │level  │
         │     └─────────┘ └─────────┘ └───────┘
         │
    ┌────▼────────────────────┐
    │ TAL BLE (tal_bluetooth) │
    │ LVGL v9 (lv_vendor)     │
    │ tal_kv, tal_sw_timer    │
    └─────────────────────────┘
```

## State-to-Animation Mapping (using existing ducky GIFs)

| PersonaState | Ducky GIF | Planned Custom GIF (future) |
|-------------|-----------|---------------------------|
| SLEEP | ducky_sleep | Closed eyes, slow breathing |
| IDLE | ducky_stand_still / walk / blink | Blinking, looking around |
| BUSY | ducky_dance | Sweating, typing fast |
| ATTENTION | ducky_sick | Alert pose, red LED |
| HEART | ducky_emotion_happy | Floating hearts |
| CELEBRATE | ducky_dance | Confetti, bouncing |
| DIZZY | ducky_emotion_cry | Spiral eyes |
