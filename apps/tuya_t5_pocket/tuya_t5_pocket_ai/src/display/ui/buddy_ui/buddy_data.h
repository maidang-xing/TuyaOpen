// Buddy UI shared data types
// Defines the core data structures for buddy persona state, BLE session data, and persistent statistics

#ifndef BUDDY_DATA_H
#define BUDDY_DATA_H

#include <stdint.h>
#include <stdbool.h>

// Persona state enum - maps to visual animation
typedef enum {
    BUDDY_PERSONA_SLEEP = 0,    // no BLE connection
    BUDDY_PERSONA_IDLE,         // connected, no active sessions
    BUDDY_PERSONA_BUSY,         // sessions running (>=3)
    BUDDY_PERSONA_ATTENTION,    // approval pending
    BUDDY_PERSONA_CELEBRATE,    // level-up / recently completed
    BUDDY_PERSONA_DIZZY,        // shake detected
    BUDDY_PERSONA_HEART,        // quick approval (<5s)
    BUDDY_PERSONA_COUNT
} buddy_persona_t;

// Session and token data received from Claude Desktop via BLE
typedef struct {
    uint8_t sessions_total;
    uint8_t sessions_running;
    uint8_t sessions_waiting;
    bool recently_completed;
    uint32_t tokens;
    uint32_t tokens_today;
    char msg[64];
    bool ble_connected;
    char prompt_id[40];
    char prompt_tool[32];
    char prompt_hint[64];
    bool has_prompt;
} buddy_tama_state_t;

// Persistent statistics
typedef struct {
    uint32_t tokens_total;
    uint16_t level;          // tokens_total / 50000
    uint16_t approvals;
    uint16_t denials;
    uint8_t mood;            // 0-4 (based on approval rate)
    uint8_t fed;             // 0-10 (tokens % 50K / 5K)
    uint8_t energy;          // 0-5
} buddy_stats_t;

#endif // BUDDY_DATA_H
