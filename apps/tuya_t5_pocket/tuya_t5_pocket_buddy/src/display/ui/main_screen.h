/**
 * @file main_screen.h
 * @brief Buddy project main screen declaration - thin redirect to buddy_main_screen.
 *
 * @copyright Copyright (c) 2025 Tuya Inc.
 */

#ifndef MAIN_SCREEN_H
#define MAIN_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "screen_manager.h"
#include "buddy_main_screen.h"

/* For compatibility with screen_manager.c, main_screen is an alias to buddy_main_screen */
#define main_screen buddy_main_screen

#ifdef __cplusplus
}
#endif

#endif /* MAIN_SCREEN_H */
