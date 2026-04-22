/**
 * @file buddy_data.h
 * @brief Shared data type for the Claude Desktop Buddy UI bridge.
 *
 * The simplified UI only renders fields sourced from the Claude desktop
 * over BLE (Nordic UART Service).  No local persona / mood / stats are
 * tracked on device; everything visible on screen must come from a JSON
 * line received from the desktop client.
 *
 * Fields map 1:1 onto the claude-desktop-buddy heartbeat / prompt frames:
 *   { "total":4, "running":3, "waiting":1, "tokens":..., "tokens_today":...,
 *     "msg":"...", "prompt": { "id":"...", "tool":"Read", "hint":"src/..." } }
 *   { "cmd":"owner", "name":"alice" }
 */

#ifndef BUDDY_DATA_H
#define BUDDY_DATA_H

#include <stdint.h>
#include <stdbool.h>

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
/* Entries ring (heartbeat `entries[]` running transcript).
 *   - BUDDY_ENTRY_MAX_CHARS: per-entry text cap in bytes, excluding NUL.
 *   - BUDDY_ENTRIES_RING:    number of slots retained on device. */
#define BUDDY_ENTRY_MAX_CHARS 79
#define BUDDY_ENTRIES_RING    8

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
/**
 * @brief Single line of the heartbeat `entries[]` running transcript.
 */
typedef struct {
    char text[BUDDY_ENTRY_MAX_CHARS + 1];
} buddy_entry_t;

/**
 * @brief Snapshot of everything the UI currently knows about Claude.
 *
 * All char arrays are UTF-8, NUL-terminated, and bounded.  Fields that
 * have not been reported yet are left as zeroes / empty strings so the
 * UI can render a "waiting" state without special-casing NULLs.
 */
typedef struct {
    bool     ble_connected;         /* GATT link to the desktop is up            */
    bool     recently_completed;    /* heartbeat says a session just finished    */
    bool     has_prompt;            /* an approval request is currently pending  */

    uint8_t  sessions_total;        /* "total"   from heartbeat frame            */
    uint8_t  sessions_running;      /* "running" from heartbeat frame            */
    uint8_t  sessions_waiting;      /* "waiting" from heartbeat frame            */
    uint32_t tokens;                /* cumulative tokens reported by desktop     */
    uint32_t tokens_today;          /* tokens used in the current calendar day   */

    char     msg[64];               /* free-form status string ("Working on...")  */
    char     owner_name[32];        /* set via {"cmd":"owner"}                   */
    char     device_name[20];       /* advertised BLE name (Claude_XXXX)         */

    char     prompt_id[40];         /* id echoed in permission decisions          */
    char     prompt_tool[32];       /* tool being requested (Read / Write / ...) */
    char     prompt_hint[64];       /* argument hint (file path or summary)      */

    /* Running transcript ring: newest at head, oldest at tail.
     * `entries_count` is how many slots are populated, <= BUDDY_ENTRIES_RING.
     * `entries_head` points at the most recent entry, wrapping modulo the
     * ring size. */
    buddy_entry_t entries[BUDDY_ENTRIES_RING];
    uint8_t       entries_count;
    uint8_t       entries_head;

    /* Wall-clock sync from `{"time":[epoch, tz]}`.  Zeroed means "not
     * synced yet"; the UI must render "--:--" in that case.  All time
     * math uses int64_t to avoid 32-bit wrap. */
    int64_t  wall_epoch_s;          /* epoch seconds captured at rx               */
    int16_t  wall_tz_min;           /* signed tz offset in minutes                */
    uint64_t wall_local_ms_at_rx;   /* tal_system_get_millisecond() at rx         */
} buddy_tama_state_t;

#endif /* BUDDY_DATA_H */
