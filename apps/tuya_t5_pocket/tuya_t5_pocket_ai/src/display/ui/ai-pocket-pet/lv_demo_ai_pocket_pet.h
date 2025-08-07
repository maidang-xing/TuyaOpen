/**
 * @file lv_demo_ai_pocket_pet.h
 * AI Pocket Pet Demo for LVGL
 */

#ifndef LV_DEMO_AI_POCKET_PET_H
#define LV_DEMO_AI_POCKET_PET_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "../lvgl/lvgl.h"

/*********************
 *      DEFINES
 *********************/
#define AI_PET_SCREEN_WIDTH  384
#define AI_PET_SCREEN_HEIGHT 168

/**********************
 *      TYPEDEFS
 **********************/

typedef enum {
    AI_PET_STATE_IDLE,
    AI_PET_STATE_WALKING,
    AI_PET_STATE_EATING,
    AI_PET_STATE_SLEEPING,
    AI_PET_STATE_PLAYING
} ai_pet_state_t;

typedef enum {
    AI_PET_MENU_MAIN,
    AI_PET_MENU_INFO,
    AI_PET_MENU_FOOD,
    AI_PET_MENU_BATH,
    AI_PET_MENU_HEALTH,
    AI_PET_MENU_SLEEP
} ai_pet_menu_t;

typedef struct {
    uint8_t health;    // 0-100
    uint8_t hungry;    // 0-100
    uint8_t happy;     // 0-100
    uint16_t age_days; // Age in days
    float weight_kg;   // Weight in kg (decimal)
    char name[16];     // Pet name
} ai_pet_stats_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Initialize and start the AI Pocket Pet demo
 */
void lv_demo_ai_pocket_pet(void);

/**
 * Handle input events (joystick/keyboard simulation)
 * @param key The key pressed
 */
void lv_demo_ai_pocket_pet_handle_input(uint32_t key);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LV_DEMO_AI_POCKET_PET_H */
