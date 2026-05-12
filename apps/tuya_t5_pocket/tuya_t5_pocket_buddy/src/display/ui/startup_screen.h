/**
 * @file startup_screen.h
 * @brief Buddy project startup screen declaration.
 *
 * @copyright Copyright (c) 2025 Tuya Inc.
 */

#ifndef STARTUP_SCREEN_H
#define STARTUP_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "screen_manager.h"

extern Screen_t startup_screen;

void startup_screen_init(void);
void startup_screen_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* STARTUP_SCREEN_H */
