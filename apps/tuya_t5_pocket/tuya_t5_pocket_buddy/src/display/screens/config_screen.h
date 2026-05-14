/**
 * @file config_screen.h
 * @brief Claude Buddy config screen — IP address input + WS connect.
 *
 * Provides a 12-digit grouped IP entry with LEFT/RIGHT cursor navigation
 * and UP/DOWN digit increment/decrement. ENTER assembles the IP string,
 * calls buddy_ws_set_host() + buddy_ws_stop() + buddy_ws_start(), then
 * polls for connection before transitioning to main_screen.
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#ifndef CONFIG_SCREEN_H
#define CONFIG_SCREEN_H

#include "screen_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Screen_t buddy_config_screen;

void config_screen_init(void);
void config_screen_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_SCREEN_H */
