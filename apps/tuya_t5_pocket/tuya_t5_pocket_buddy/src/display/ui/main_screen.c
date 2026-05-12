/**
 * @file main_screen.c
 * @brief Buddy project main screen - thin wrapper that redirects to buddy_main_screen.
 *
 * @copyright Copyright (c) 2025 Tuya Inc.
 */

#include "main_screen.h"

/* The actual main screen is buddy_main_screen. This file ensures that
   references to "main_screen" resolve to buddy_main_screen via the
   macro in main_screen.h. All buddy-specific logic lives in buddy_ui/. */
