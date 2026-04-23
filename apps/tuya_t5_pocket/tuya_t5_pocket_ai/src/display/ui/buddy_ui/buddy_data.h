/**
 * @file buddy_data.h
 * @brief Claude Desktop Buddy UI 桥层共享数据类型。
 *
 * 设备端只渲染来自 Claude 桌面/CLI 通过 BLE（NUS）推送的字段；本机不
 * 追踪本地 persona/mood/stats。屏幕上可见的一切都来自最近一次 JSON 帧。
 *
 * 字段与心跳/prompt 协议 1:1 对应：
 *   { "total":4, "running":3, "waiting":1, "tokens":..., "tokens_today":...,
 *     "msg":"...", "prompt": { "id":"...", "tool":"Read", "hint":"src/..." } }
 *   { "cmd":"owner", "name":"alice" }
 *
 * v1.1 新增：entries[] 滚动转写、{"time":[epoch,tz]} UI 侧墙钟。
 *
 * M1-UI 新增：
 *   - buddy_persona_state_e   从协议字段推导出的 7 个人格状态
 *   - buddy_led_state_e       LED 视觉状态枚举（由 persona 状态驱动）
 *   - persona_id / led_state  仅做缓存，实际值由 buddy_main_screen 推导
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#ifndef BUDDY_DATA_H
#define BUDDY_DATA_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
/* Entries ring（心跳 entries[] 滚动转写）
 *   - BUDDY_ENTRY_MAX_CHARS: 每条文本上限（字节，含 NUL 前）
 *   - BUDDY_ENTRIES_RING:    设备端保留的槽位数 */
#define BUDDY_ENTRY_MAX_CHARS 79
#define BUDDY_ENTRIES_RING    8

/* 人格注册表长度；与 persona_registry 中的条目一一对应。 */
#define BUDDY_PERSONA_COUNT   18

/* Sessions list — 来自心跳 "sessions" 数组 */
#define BUDDY_SESSIONS_MAX       6
#define BUDDY_SESSION_NAME_LEN   28   /* 会话名字符上限（字节） */
#define BUDDY_MODEL_LEN          22   /* 模型名字符上限（字节） */

/* Per-model token statistics — 来自心跳 "mstats" 数组 */
#define BUDDY_MSTATS_MAX         4

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
/**
 * @brief 从协议字段推导出的人格状态。
 *
 * 每个状态对应一组 ASCII 帧动画（见 ascii_persona.h / persona_registry.c）。
 * 映射规则（buddy_main_screen 中实现）：
 *   !ble_connected                 -> BUDDY_PERSONA_STATE_SLEEP
 *   has_prompt                     -> BUDDY_PERSONA_STATE_ATTENTION
 *   running > 0                    -> BUDDY_PERSONA_STATE_BUSY
 *   recently_completed             -> BUDDY_PERSONA_STATE_CELEBRATE（瞬态，≤3s）
 *   quick-approve after ATTENTION  -> BUDDY_PERSONA_STATE_HEART（瞬态，≤2s）
 *   external shake event           -> BUDDY_PERSONA_STATE_DIZZY（瞬态，≤2s）
 *   otherwise                      -> BUDDY_PERSONA_STATE_IDLE
 */
typedef enum {
    BUDDY_PERSONA_STATE_SLEEP = 0,
    BUDDY_PERSONA_STATE_IDLE,
    BUDDY_PERSONA_STATE_BUSY,
    BUDDY_PERSONA_STATE_ATTENTION,
    BUDDY_PERSONA_STATE_CELEBRATE,
    BUDDY_PERSONA_STATE_DIZZY,
    BUDDY_PERSONA_STATE_HEART,
    BUDDY_PERSONA_STATE_COUNT
} buddy_persona_state_e;

/**
 * @brief LED 视觉状态枚举。
 *
 * 映射（buddy_main_screen 中实现，buddy_led 执行）：
 *   SLEEP                 -> BUDDY_LED_STATE_OFF
 *   IDLE                  -> BUDDY_LED_STATE_ON_DIM（常亮弱光）
 *   BUSY                  -> BUDDY_LED_STATE_BLINK_SLOW（慢闪 1 Hz）
 *   ATTENTION             -> BUDDY_LED_STATE_BLINK_FAST（快闪 4 Hz）
 *   CELEBRATE / HEART     -> BUDDY_LED_STATE_FLASH_ONCE（短亮 200 ms 后回前态）
 *   DIZZY                 -> BUDDY_LED_STATE_BLINK_FAST
 */
typedef enum {
    BUDDY_LED_STATE_OFF = 0,
    BUDDY_LED_STATE_ON_DIM,
    BUDDY_LED_STATE_BLINK_SLOW,
    BUDDY_LED_STATE_BLINK_FAST,
    BUDDY_LED_STATE_FLASH_ONCE,
    BUDDY_LED_STATE_COUNT
} buddy_led_state_e;

/**
 * @brief 心跳 entries[] 滚动转写的一行。
 */
typedef struct {
    char text[BUDDY_ENTRY_MAX_CHARS + 1];
} buddy_entry_t;

/**
 * @brief 单个 Claude Code 会话快照（来自心跳 sessions[] 数组）。
 *
 * 字段 key 映射（JSON 短名 → 结构体字段）：
 *   "id" → sid      短会话 ID（截取前 11 字符）
 *   "n"  → name     会话名（来自第一条用户 prompt，已截断）
 *   "m"  → model    模型短名（如 "sonnet-4-6"）
 *   "ti" → tokens_in   输入 token 累计
 *   "to" → tokens_out  输出 token 累计
 *   "r"  → is_running  当前是否正在生成
 */
typedef struct {
    char     sid[12];                           /* session id 前 11 字符 + NUL */
    char     name[BUDDY_SESSION_NAME_LEN + 1];  /* 会话名 */
    char     model[BUDDY_MODEL_LEN + 1];        /* 模型名 */
    uint32_t tokens_out;                        /* 输出 token（REFERENCE.md 定义） */
    bool     is_running;                        /* 是否正在生成 */
} buddy_session_t;

/**
 * @brief 单个模型的输出 token 统计（来自心跳 "mstats" 数组）。
 */
typedef struct {
    char     model[BUDDY_MODEL_LEN + 1];
    uint32_t tokens_out;
} buddy_mstat_t;

/**
 * @brief UI 当前知道的所有 Claude 状态快照。
 *
 * 所有字符数组均为 UTF-8、NUL 终止且长度受限。尚未汇报的字段留为 0 /
 * 空串，UI 可以渲染"等待中"状态而无需特判 NULL。
 */
typedef struct {
    bool     ble_connected;         /* GATT 链路是否建立                         */
    bool     recently_completed;    /* 心跳显示会话刚刚结束（CELEBRATE 提示）     */
    bool     has_prompt;            /* 是否有待审批请求                           */

    uint8_t  sessions_total;        /* "total"   字段                             */
    uint8_t  sessions_running;      /* "running" 字段                             */
    uint8_t  sessions_waiting;      /* "waiting" 字段                             */
    uint32_t tokens;                /* 桌面端累计 token                           */
    uint32_t tokens_today;          /* 本日 token                                 */

    char     msg[64];               /* 自由文本状态（"Working on..."）             */
    char     owner_name[32];        /* {"cmd":"owner"} 设置                        */
    char     device_name[20];       /* 广播名（Claude_XXXX）                       */

    char     prompt_id[40];         /* 权限响应中逐字节回显                         */
    char     prompt_tool[32];       /* 被请求的工具（Read / Write / ...）          */
    char     prompt_hint[64];       /* 参数摘要（文件路径或简述）                    */

    /* 转写环：最新在 head，最旧在 tail。
     * entries_count 为已填充槽位数（≤ BUDDY_ENTRIES_RING）。
     * entries_head 指向最新条目，按环长取模回绕。 */
    buddy_entry_t entries[BUDDY_ENTRIES_RING];
    uint8_t       entries_count;
    uint8_t       entries_head;

    /* {"time":[epoch, tz]} 时钟同步。全 0 表示"尚未同步"，UI 须渲染
     * "--:--"。所有时间计算均用 int64_t，避免 32 位溢出。 */
    int64_t  wall_epoch_s;          /* 收到帧时的 epoch 秒                          */
    int16_t  wall_tz_min;           /* 时区偏移（UTC 东向分钟，带符号）               */
    uint64_t wall_local_ms_at_rx;   /* 收到帧时的 tal_system_get_millisecond()      */

    /* M2-UI 新增：模型名 + 会话列表（来自心跳 "model" / "sessions" 字段）。 */
    char            model[BUDDY_MODEL_LEN + 1];     /* 当前模型名（如 "sonnet-4-6"） */
    buddy_session_t sessions[BUDDY_SESSIONS_MAX];   /* 会话快照数组（最多 6 条）     */
    uint8_t         sessions_count;                 /* 本次心跳收到的会话数          */

    /* M3-UI 新增：per-model token 统计（来自心跳 "mstats" 字段）。 */
    buddy_mstat_t   mstats[BUDDY_MSTATS_MAX];       /* 模型用量数组（最多 4 条）     */
    uint8_t         mstats_count;

    /* M1-UI 派生字段；仅由 UI 层写入，不影响协议。 */
    uint8_t                persona_id;      /* 当前 species 索引，0..BUDDY_PERSONA_COUNT-1 */
    buddy_persona_state_e  persona_state;   /* 由上述字段推导的人格状态                  */
    buddy_led_state_e      led_state;       /* 由 persona_state 推导的 LED 视觉状态      */
} buddy_tama_state_t;

#ifdef __cplusplus
}
#endif

#endif /* BUDDY_DATA_H */
