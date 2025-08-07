/**
 * @file lv_demo_ai_pocket_pet.c
 * AI Pocket Pet Demo for LVGL
 *
 * This module implements a Tamagotchi-style virtual pet interface using LVGL.
 * Features include pet care, statistics tracking, and menu navigation.
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_demo_ai_pocket_pet.h"
#include "lv_keyboard_widget.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*********************
 *      DEFINES
 *********************/
#define STATUS_BAR_HEIGHT  20
#define BOTTOM_MENU_HEIGHT 40
#define PET_AREA_HEIGHT    (AI_PET_SCREEN_HEIGHT - STATUS_BAR_HEIGHT - BOTTOM_MENU_HEIGHT)

// UI Constants
#define MENU_BUTTON_COUNT     5
#define MENU_BUTTON_SIZE      24
#define MENU_BUTTON_SPACING   25
#define MENU_BUTTON_START_X   (AI_PET_SCREEN_WIDTH - 280)
#define SUB_MENU_PADDING      10
#define SUB_MENU_TITLE_OFFSET 10
#define SUB_MENU_LIST_OFFSET  40
#define STAT_CONTAINER_HEIGHT 30
#define STAT_CONTAINER_WIDTH  (AI_PET_SCREEN_WIDTH - 40)
#define SEPARATOR_HEIGHT      2

// LVGL key codes
#define KEY_UP    17  // LV_KEY_UP
#define KEY_LEFT  20  // LV_KEY_LEFT
#define KEY_DOWN  18  // LV_KEY_DOWN
#define KEY_RIGHT 19  // LV_KEY_RIGHT
#define KEY_ENTER 10  // LV_KEY_ENTER
#define KEY_ESC   27  // LV_KEY_ESC
#define KEY_I     105 // 'i' key

// Pet animation constants
#define PET_ANIMATION_INTERVAL 2000
#define PET_MOVEMENT_INTERVAL  3000
#define PET_MOVEMENT_STEP      10
#define PET_MOVEMENT_LIMIT     150

// Pet stats constants
#define MAX_STAT_VALUE   100
#define MIN_WEIGHT_KG    1.0f
#define MAX_WEIGHT_KG    5.0f
#define WEIGHT_INCREMENT 0.1f

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *status_bar;
    lv_obj_t *wifi_icon;
    lv_obj_t *network_icon;
    lv_obj_t *battery_icon;
    lv_obj_t *pet_area;
    lv_obj_t *pet_sprite;
    lv_obj_t *bottom_menu;
    lv_obj_t *menu_buttons[MENU_BUTTON_COUNT];
    lv_obj_t *sub_menu;
    lv_obj_t *sub_menu_list;

    ai_pet_state_t pet_state;
    ai_pet_menu_t current_menu;
    uint8_t selected_button;
    uint8_t sub_menu_selection;

    ai_pet_stats_t pet_stats;

    lv_timer_t *pet_animation_timer;
    lv_timer_t *pet_movement_timer;
} ai_pet_demo_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

// UI Creation Functions
static void create_status_bar(ai_pet_demo_t *demo);
static void create_pet_area(ai_pet_demo_t *demo);
static void create_bottom_menu(ai_pet_demo_t *demo);
static void create_sub_menu(ai_pet_demo_t *demo);

// Animation Functions
static void pet_animation_cb(lv_timer_t *timer);
static void pet_movement_cb(lv_timer_t *timer);

// Event Handler Functions
static void menu_button_event_cb(lv_event_t *e);
static void sub_menu_event_cb(lv_event_t *e);
static void keyboard_event_cb(lv_event_t *e);
static void keyboard_callback(keyboard_result_t result, const char *text, void *user_data);

// Menu Management Functions
static void show_info_menu(ai_pet_demo_t *demo);
static void show_food_menu(ai_pet_demo_t *demo);
static void show_bath_menu(ai_pet_demo_t *demo);
static void show_health_menu(ai_pet_demo_t *demo);
static void show_sleep_menu(ai_pet_demo_t *demo);
static void hide_sub_menu(ai_pet_demo_t *demo);
static void show_keyboard_for_pet_name(ai_pet_demo_t *demo);

// Info Menu Helper Functions
static void create_pet_name_display(ai_pet_demo_t *demo);
static void create_pet_stats_displays(ai_pet_demo_t *demo);
static void create_separator(void);
static void create_actions_section(void);

// Initialization Functions
static void init_demo_data(void);
static void create_main_screen(void);
static void start_animation_timers(void);

// Input Handling Functions
static void handle_main_menu_navigation(uint32_t key);
static void handle_sub_menu_navigation(uint32_t key);
static void handle_menu_selection(void);
static void handle_sub_menu_selection(void);
static void handle_ai_function(void);

// Utility Functions
static void update_button_selection(uint8_t old_selection, uint8_t new_selection);
static void init_pet_stats(ai_pet_stats_t *stats);
// static void update_pet_stats_display(ai_pet_demo_t *demo);
static void update_sub_menu_selection(uint8_t old_selection, uint8_t new_selection);
static uint32_t find_action_items_start(void);
static void create_stat_display_item(lv_obj_t *parent, const char *label, const char *value);
static void highlight_first_sub_menu_item(ai_pet_demo_t *demo);
static void create_sub_menu_with_items(ai_pet_demo_t *demo, const char *title, const char *symbols[],
                                       const char *items[], uint8_t item_count);

/**********************
 *  STATIC VARIABLES
 **********************/
static ai_pet_demo_t demo_data;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Initializes the demo data structure with default values
 */
static void init_demo_data(void)
{
    memset(&demo_data, 0, sizeof(ai_pet_demo_t));
    demo_data.pet_state = AI_PET_STATE_IDLE;
    demo_data.current_menu = AI_PET_MENU_MAIN;
    demo_data.selected_button = 0;
    demo_data.sub_menu_selection = 0;
    init_pet_stats(&demo_data.pet_stats);
}

/**
 * Creates and configures the main screen
 */
static void create_main_screen(void)
{
    demo_data.screen = lv_obj_create(NULL);
    lv_obj_set_size(demo_data.screen, AI_PET_SCREEN_WIDTH, AI_PET_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(demo_data.screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(demo_data.screen, LV_OPA_COVER, 0);
    lv_screen_load(demo_data.screen);

    // Add keyboard event handler to the screen
    lv_obj_add_event_cb(demo_data.screen, keyboard_event_cb, LV_EVENT_KEY, NULL);

    // Make sure the screen can receive keyboard focus
    lv_group_add_obj(lv_group_get_default(), demo_data.screen);
}

/**
 * Starts the pet animation timers
 */
static void start_animation_timers(void)
{
    demo_data.pet_animation_timer = lv_timer_create(pet_animation_cb, PET_ANIMATION_INTERVAL, &demo_data);
    demo_data.pet_movement_timer = lv_timer_create(pet_movement_cb, PET_MOVEMENT_INTERVAL, &demo_data);
}

/**
 * Main demo initialization function
 */
void lv_demo_ai_pocket_pet(void)
{
    // Initialize demo data
    init_demo_data();

    // Initialize keyboard widget
    lv_keyboard_widget_init();

    // Create main screen
    create_main_screen();

    // Create UI components
    create_status_bar(&demo_data);
    create_pet_area(&demo_data);
    create_bottom_menu(&demo_data);
    create_sub_menu(&demo_data);

    // Start pet animation timers
    start_animation_timers();
}

void lv_demo_ai_pocket_pet_handle_input(uint32_t key)
{
    // Check if keyboard widget is active - if so, route input to keyboard
    if (lv_keyboard_widget_is_active()) {
        lv_keyboard_widget_handle_input(key);
        return;
    }

    printf("Key pressed: %d (UP:%d LEFT:%d DOWN:%d RIGHT:%d ENTER:%d ESC:%d I:%d)\n", key, KEY_UP, KEY_LEFT, KEY_DOWN,
           KEY_RIGHT, KEY_ENTER, KEY_ESC, KEY_I);

    switch (key) {
    case KEY_UP:
        printf("UP key pressed - navigating up\n");
        if (demo_data.current_menu == AI_PET_MENU_MAIN) {
            handle_main_menu_navigation(key);
        } else {
            handle_sub_menu_navigation(key);
        }
        break;

    case KEY_DOWN:
        printf("DOWN key pressed - navigating down\n");
        if (demo_data.current_menu == AI_PET_MENU_MAIN) {
            handle_main_menu_navigation(key);
        } else {
            handle_sub_menu_navigation(key);
        }
        break;

    case KEY_LEFT:
        printf("LEFT/A key pressed - navigating left\n");
        handle_main_menu_navigation(key);
        break;

    case KEY_RIGHT:
        printf("RIGHT/D key pressed - navigating right\n");
        handle_main_menu_navigation(key);
        break;

    case KEY_ENTER:
        if (demo_data.current_menu == AI_PET_MENU_MAIN) {
            handle_menu_selection();
        } else {
            handle_sub_menu_selection();
        }
        break;

    case KEY_ESC:
        if (demo_data.current_menu != AI_PET_MENU_MAIN) {
            hide_sub_menu(&demo_data);
        }
        break;

    case KEY_I:
        printf("I key pressed - AI function invoked\n");
        handle_ai_function();
        break;

    default:
        printf("Unhandled key: %d\n", key);
        if (key > 0) {
            printf("Key press detected but not handled: %d\n", key);
        }
        break;
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Creates the status bar with WiFi, network, and battery icons
 */
static void create_status_bar(ai_pet_demo_t *demo)
{
    demo->status_bar = lv_obj_create(demo->screen);
    lv_obj_set_size(demo->status_bar, AI_PET_SCREEN_WIDTH, STATUS_BAR_HEIGHT);
    lv_obj_align(demo->status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(demo->status_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(demo->status_bar, 0, 0);
    lv_obj_set_style_pad_all(demo->status_bar, 2, 0);

    // WiFi icon
    demo->wifi_icon = lv_label_create(demo->status_bar);
    lv_label_set_text(demo->wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_align(demo->wifi_icon, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_text_color(demo->wifi_icon, lv_color_black(), 0);

    // Network icon (4G)
    demo->network_icon = lv_label_create(demo->status_bar);
    lv_label_set_text(demo->network_icon, "4G");
    lv_obj_align(demo->network_icon, LV_ALIGN_LEFT_MID, 30, 0);
    lv_obj_set_style_text_color(demo->network_icon, lv_color_black(), 0);

    // Battery icon
    demo->battery_icon = lv_label_create(demo->status_bar);
    lv_label_set_text(demo->battery_icon, LV_SYMBOL_BATTERY_FULL);
    lv_obj_align(demo->battery_icon, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_text_color(demo->battery_icon, lv_color_black(), 0);
}

/**
 * Creates the pet display area with the pet sprite
 */
static void create_pet_area(ai_pet_demo_t *demo)
{
    demo->pet_area = lv_obj_create(demo->screen);
    lv_obj_set_size(demo->pet_area, AI_PET_SCREEN_WIDTH, PET_AREA_HEIGHT);
    lv_obj_align(demo->pet_area, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT);
    lv_obj_set_style_bg_opa(demo->pet_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(demo->pet_area, 0, 0);
    lv_obj_set_style_pad_all(demo->pet_area, 5, 0);

    // Create pet sprite
    demo->pet_sprite = lv_obj_create(demo->pet_area);
    lv_obj_set_size(demo->pet_sprite, 40, 40);
    lv_obj_align(demo->pet_sprite, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(demo->pet_sprite, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(demo->pet_sprite, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(demo->pet_sprite, 20, 0);

    // Add pet label
    lv_obj_t *pet_label = lv_label_create(demo->pet_sprite);
    lv_label_set_text(pet_label, "🐾");
    lv_obj_align(pet_label, LV_ALIGN_CENTER, 0, 0);
}

/**
 * Creates the bottom menu with navigation buttons
 */
static void create_bottom_menu(ai_pet_demo_t *demo)
{
    demo->bottom_menu = lv_obj_create(demo->screen);
    lv_obj_set_size(demo->bottom_menu, AI_PET_SCREEN_WIDTH, BOTTOM_MENU_HEIGHT);
    lv_obj_align(demo->bottom_menu, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(demo->bottom_menu, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(demo->bottom_menu, 0, 0);
    lv_obj_set_style_pad_all(demo->bottom_menu, 2, 0);

    const char *menu_symbols[] = {LV_SYMBOL_DIRECTORY, LV_SYMBOL_EDIT, LV_SYMBOL_REFRESH, LV_SYMBOL_POWER,
                                  LV_SYMBOL_CLOSE};

    for (int i = 0; i < MENU_BUTTON_COUNT; i++) {
        demo->menu_buttons[i] = lv_btn_create(demo->bottom_menu);
        lv_obj_set_size(demo->menu_buttons[i], MENU_BUTTON_SIZE, MENU_BUTTON_SIZE);
        lv_obj_align(demo->menu_buttons[i], LV_ALIGN_BOTTOM_RIGHT, -(MENU_BUTTON_START_X - i * MENU_BUTTON_SPACING), 0);

        // Set default button style
        lv_obj_set_style_bg_color(demo->menu_buttons[i], lv_color_white(), 0);
        lv_obj_set_style_bg_opa(demo->menu_buttons[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(demo->menu_buttons[i], 0, 0);
        lv_obj_set_style_radius(demo->menu_buttons[i], 3, 0);
        lv_obj_set_style_shadow_width(demo->menu_buttons[i], 0, 0);
        lv_obj_set_style_shadow_opa(demo->menu_buttons[i], LV_OPA_TRANSP, 0);

        lv_obj_t *label = lv_label_create(demo->menu_buttons[i]);
        lv_label_set_text(label, menu_symbols[i]);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_color(label, lv_color_black(), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);

        lv_obj_add_event_cb(demo->menu_buttons[i], menu_button_event_cb, LV_EVENT_CLICKED, demo);
    }

    // Highlight first button
    update_button_selection(0, 0);
}

/**
 * Creates the sub menu container
 */
static void create_sub_menu(ai_pet_demo_t *demo)
{
    demo->sub_menu = lv_obj_create(demo->screen);
    lv_obj_set_size(demo->sub_menu, AI_PET_SCREEN_WIDTH, AI_PET_SCREEN_HEIGHT);
    lv_obj_align(demo->sub_menu, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(demo->sub_menu, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(demo->sub_menu, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(demo->sub_menu, 0, 0);
    lv_obj_set_style_pad_all(demo->sub_menu, SUB_MENU_PADDING, 0);

    // Title at the top
    lv_obj_t *title = lv_label_create(demo->sub_menu);
    lv_label_set_text(title, "Menu");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, SUB_MENU_TITLE_OFFSET);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_black(), 0);

    // List for sub menu items
    demo->sub_menu_list = lv_list_create(demo->sub_menu);
    lv_obj_set_size(demo->sub_menu_list, AI_PET_SCREEN_WIDTH - 20, AI_PET_SCREEN_HEIGHT - 60);
    lv_obj_align(demo->sub_menu_list, LV_ALIGN_TOP_MID, 0, SUB_MENU_LIST_OFFSET);
    lv_obj_add_flag(demo->sub_menu_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(demo->sub_menu_list, LV_DIR_VER);
    lv_obj_add_event_cb(demo->sub_menu_list, sub_menu_event_cb, LV_EVENT_CLICKED, demo);

    // Initially hide sub menu
    lv_obj_add_flag(demo->sub_menu, LV_OBJ_FLAG_HIDDEN);
}

/**
 * Pet animation callback - changes pet appearance based on state
 */
static void pet_animation_cb(lv_timer_t *timer)
{
    lv_color_t colors[] = {
        lv_color_black(), // Idle
        lv_color_black(), // Walking
        lv_color_black(), // Eating
        lv_color_black(), // Sleeping
        lv_color_black()  // Playing
    };

    lv_obj_set_style_bg_color(demo_data.pet_sprite, colors[demo_data.pet_state], 0);
}

/**
 * Pet movement callback - simple horizontal movement
 */
static void pet_movement_cb(lv_timer_t *timer)
{
    static int8_t direction = 1;
    static int16_t x_pos = 0;

    x_pos += direction * PET_MOVEMENT_STEP;
    if (x_pos > PET_MOVEMENT_LIMIT || x_pos < -PET_MOVEMENT_LIMIT) {
        direction *= -1;
    }

    lv_obj_set_x(demo_data.pet_sprite, x_pos);
}

/**
 * Menu button click event handler
 */
static void menu_button_event_cb(lv_event_t *e)
{
    ai_pet_demo_t *demo = (ai_pet_demo_t *)lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target(e);

    // Find which button was clicked
    for (int i = 0; i < MENU_BUTTON_COUNT; i++) {
        if (demo->menu_buttons[i] == btn) {
            demo->selected_button = i;
            break;
        }
    }
}

/**
 * Sub menu item click event handler
 */
static void sub_menu_event_cb(lv_event_t *e)
{
    ai_pet_demo_t *demo = (ai_pet_demo_t *)lv_event_get_user_data(e);
    lv_obj_t *target = lv_event_get_target(e);

    printf("Sub menu item selected\n");

    if (demo->current_menu == AI_PET_MENU_INFO) {
        uint32_t child_count = lv_obj_get_child_cnt(demo->sub_menu_list);
        uint32_t action_items_start = find_action_items_start();

        // Find which action item was clicked
        for (uint32_t i = action_items_start; i < child_count; i++) {
            lv_obj_t *child = lv_obj_get_child(demo->sub_menu_list, i);
            if (child == target) {
                uint32_t action_index = i - action_items_start;
                if (action_index == 0) { // "Edit Pet Name" option
                    show_keyboard_for_pet_name(demo);
                }
                break;
            }
        }
    }
}

/**
 * Keyboard event handler
 */
static void keyboard_event_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    printf("Keyboard event received: key=%d\n", key);
    lv_demo_ai_pocket_pet_handle_input(key);
}

/**
 * Keyboard callback for pet name editing
 */
static void keyboard_callback(keyboard_result_t result, const char *text, void *user_data)
{
    ai_pet_demo_t *demo = (ai_pet_demo_t *)user_data;

    switch (result) {
    case KEYBOARD_RESULT_OK:
        if (text && strlen(text) > 0) {
            strncpy(demo->pet_stats.name, text, sizeof(demo->pet_stats.name) - 1);
            demo->pet_stats.name[sizeof(demo->pet_stats.name) - 1] = '\0';
            printf("Pet name updated to: %s\n", demo->pet_stats.name);

            if (demo->current_menu == AI_PET_MENU_INFO) {
                show_info_menu(demo);
            }
        }
        break;

    case KEYBOARD_RESULT_CANCEL:
        printf("Keyboard input cancelled\n");
        break;

    case KEYBOARD_RESULT_MENU:
        printf("Menu key pressed in keyboard\n");
        break;
    }

    // Reload the main screen to return from keyboard
    lv_screen_load(demo->screen);
    highlight_first_sub_menu_item(demo);
}

/**
 * Handles main menu navigation (up/down/left/right)
 */
static void handle_main_menu_navigation(uint32_t key)
{
    uint8_t old_selection = demo_data.selected_button;
    uint8_t new_selection = old_selection;

    switch (key) {
    case KEY_UP:
    case KEY_LEFT:
        if (demo_data.selected_button > 0) {
            new_selection = demo_data.selected_button - 1;
        }
        break;

    case KEY_DOWN:
    case KEY_RIGHT:
        if (demo_data.selected_button < MENU_BUTTON_COUNT - 1) {
            new_selection = demo_data.selected_button + 1;
        }
        break;
    }

    if (new_selection != old_selection) {
        demo_data.selected_button = new_selection;
        update_button_selection(old_selection, new_selection);
    }
}

/**
 * Handles sub menu navigation (up/down)
 */
static void handle_sub_menu_navigation(uint32_t key)
{
    uint32_t child_count = lv_obj_get_child_cnt(demo_data.sub_menu_list);
    if (child_count == 0)
        return;

    uint8_t old_selection = demo_data.sub_menu_selection;
    uint8_t new_selection = old_selection;

    switch (key) {
    case KEY_UP:
        if (demo_data.sub_menu_selection > 0) {
            new_selection = demo_data.sub_menu_selection - 1;
        }
        break;

    case KEY_DOWN:
        if (demo_data.sub_menu_selection < child_count - 1) {
            new_selection = demo_data.sub_menu_selection + 1;
        }
        break;
    }

    if (new_selection != old_selection) {
        update_sub_menu_selection(old_selection, new_selection);
        demo_data.sub_menu_selection = new_selection;
    }
}

/**
 * Handles main menu selection (ENTER key)
 */
static void handle_menu_selection(void)
{
    switch (demo_data.selected_button) {
    case 0: // Info
        show_info_menu(&demo_data);
        break;
    case 1: // Food
        show_food_menu(&demo_data);
        break;
    case 2: // Bath
        show_bath_menu(&demo_data);
        break;
    case 3: // Health
        show_health_menu(&demo_data);
        break;
    case 4: // Sleep
        show_sleep_menu(&demo_data);
        break;
    }
}

/**
 * Handles sub menu selection (ENTER key)
 */
static void handle_sub_menu_selection(void)
{
    if (demo_data.current_menu == AI_PET_MENU_INFO) {
        uint32_t action_items_start = find_action_items_start();
        uint32_t action_index = demo_data.sub_menu_selection - action_items_start;

        if (action_index == 0) {
            show_keyboard_for_pet_name(&demo_data);
        }
    }
}

/**
 * Handles AI function (I key)
 */
static void handle_ai_function(void)
{
    if (demo_data.current_menu == AI_PET_MENU_MAIN) {
        // Toggle pet state for demonstration
        demo_data.pet_state = (demo_data.pet_state + 1) % 5;
        printf("AI Function invoked! Pet state changed to: %d\n", demo_data.pet_state);

        // Update pet stats for testing
        demo_data.pet_stats.health = (demo_data.pet_stats.health + 5) % (MAX_STAT_VALUE + 1);
        demo_data.pet_stats.hungry = (demo_data.pet_stats.hungry + 10) % (MAX_STAT_VALUE + 1);
        demo_data.pet_stats.happy = (demo_data.pet_stats.happy + 3) % (MAX_STAT_VALUE + 1);
        demo_data.pet_stats.age_days++;
        demo_data.pet_stats.weight_kg += WEIGHT_INCREMENT;

        if (demo_data.pet_stats.weight_kg > MAX_WEIGHT_KG) {
            demo_data.pet_stats.weight_kg = MIN_WEIGHT_KG;
        }

        printf("Pet stats updated - Health: %d, Hungry: %d, Happy: %d, Age: %d days, Weight: %.1f kg\n",
               demo_data.pet_stats.health, demo_data.pet_stats.hungry, demo_data.pet_stats.happy,
               demo_data.pet_stats.age_days, demo_data.pet_stats.weight_kg);
    }
}

/**
 * Shows the info menu with pet statistics
 */
/**
 * Creates the pet name display container
 */
static void create_pet_name_display(ai_pet_demo_t *demo)
{
    lv_obj_t *name_container = lv_obj_create(demo->sub_menu_list);
    lv_obj_set_size(name_container, STAT_CONTAINER_WIDTH, 40);
    lv_obj_set_style_bg_opa(name_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(name_container, 0, 0);
    lv_obj_set_style_pad_all(name_container, 2, 0);

    lv_obj_t *name_label = lv_label_create(name_container);
    lv_label_set_text_fmt(name_label, "Name: %s", demo->pet_stats.name);
    lv_obj_align(name_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(name_label, lv_color_black(), 0);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14, 0);
}

/**
 * Creates all pet statistics displays
 */
static void create_pet_stats_displays(ai_pet_demo_t *demo)
{
    char value_str[16];

    snprintf(value_str, sizeof(value_str), "%d/100", demo->pet_stats.health);
    create_stat_display_item(demo->sub_menu_list, "Health:", value_str);

    snprintf(value_str, sizeof(value_str), "%d/100", demo->pet_stats.hungry);
    create_stat_display_item(demo->sub_menu_list, "Hungry:", value_str);

    snprintf(value_str, sizeof(value_str), "%d/100", demo->pet_stats.happy);
    create_stat_display_item(demo->sub_menu_list, "Happy:", value_str);

    snprintf(value_str, sizeof(value_str), "%d days", demo->pet_stats.age_days);
    create_stat_display_item(demo->sub_menu_list, "Age:", value_str);

    snprintf(value_str, sizeof(value_str), "%.1f kg", demo->pet_stats.weight_kg);
    create_stat_display_item(demo->sub_menu_list, "Weight:", value_str);
}

/**
 * Creates the separator line
 */
static void create_separator(void)
{
    lv_obj_t *separator = lv_obj_create(demo_data.sub_menu_list);
    lv_obj_set_size(separator, STAT_CONTAINER_WIDTH, SEPARATOR_HEIGHT);
    lv_obj_set_style_bg_color(separator, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(separator, LV_OPA_50, 0);
}

/**
 * Creates the actions section with title and buttons
 */
static void create_actions_section(void)
{
    // Add actions subtitle
    lv_obj_t *action_title = lv_label_create(demo_data.sub_menu_list);
    lv_label_set_text(action_title, "Actions:");
    lv_obj_align(action_title, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_text_color(action_title, lv_color_black(), 0);
    lv_obj_set_style_text_font(action_title, &lv_font_montserrat_14, 0);
    lv_obj_add_flag(action_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(action_title, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    // Add action buttons
    lv_list_add_btn(demo_data.sub_menu_list, LV_SYMBOL_EDIT, "Edit Pet Name");
    lv_list_add_btn(demo_data.sub_menu_list, LV_SYMBOL_SETTINGS, "View Statistics");
    lv_list_add_btn(demo_data.sub_menu_list, LV_SYMBOL_EDIT, "View Pet History");
    lv_list_add_btn(demo_data.sub_menu_list, LV_SYMBOL_EDIT, "DEV: Randomize STAT Data");
}

/**
 * Shows the info menu with pet statistics and actions
 */
static void show_info_menu(ai_pet_demo_t *demo)
{
    demo->current_menu = AI_PET_MENU_INFO;
    lv_obj_clear_flag(demo->sub_menu, LV_OBJ_FLAG_HIDDEN);

    // Update title
    lv_obj_t *title = lv_obj_get_child(demo->sub_menu, 0);
    lv_label_set_text(title, "Pet Information");

    // Clear existing items
    lv_obj_clean(demo->sub_menu_list);

    // Create all UI components
    create_pet_name_display(demo);
    create_pet_stats_displays(demo);
    create_separator();
    create_actions_section();

    highlight_first_sub_menu_item(demo);
}

/**
 * Shows the food menu
 */
static void show_food_menu(ai_pet_demo_t *demo)
{
    const char *symbols[] = {LV_SYMBOL_EDIT, LV_SYMBOL_EDIT, LV_SYMBOL_EDIT, LV_SYMBOL_EDIT, LV_SYMBOL_EDIT};
    const char *items[] = {"Feed Dry Food", "Feed Wet Food", "Give Treats", "Special Meal", "Set Feeding Schedule"};

    create_sub_menu_with_items(demo, "Food & Nutrition", symbols, items, 5);
}

/**
 * Shows the bath menu
 */
static void show_bath_menu(ai_pet_demo_t *demo)
{
    const char *symbols[] = {LV_SYMBOL_REFRESH, LV_SYMBOL_REFRESH, LV_SYMBOL_REFRESH, LV_SYMBOL_REFRESH,
                             LV_SYMBOL_REFRESH};
    const char *items[] = {"Quick Wash", "Full Bath", "Brush Fur", "Spa Treatment", "Nail Trim"};

    create_sub_menu_with_items(demo, "Grooming & Care", symbols, items, 5);
}

/**
 * Shows the health menu
 */
static void show_health_menu(ai_pet_demo_t *demo)
{
    const char *symbols[] = {LV_SYMBOL_POWER, LV_SYMBOL_POWER, LV_SYMBOL_POWER, LV_SYMBOL_POWER, LV_SYMBOL_POWER};
    const char *items[] = {"Health Check", "Vaccination", "Give Medicine", "Exercise Time", "View Health Records"};

    create_sub_menu_with_items(demo, "Health & Wellness", symbols, items, 5);
}

/**
 * Shows the sleep menu
 */
static void show_sleep_menu(ai_pet_demo_t *demo)
{
    const char *symbols[] = {LV_SYMBOL_CLOSE, LV_SYMBOL_CLOSE, LV_SYMBOL_CLOSE, LV_SYMBOL_CLOSE, LV_SYMBOL_CLOSE};
    const char *items[] = {"Put to Sleep", "Wake Up Pet", "Set Bedtime", "Sleep Schedule", "Sleep Quality"};

    create_sub_menu_with_items(demo, "Sleep & Rest", symbols, items, 5);
}

/**
 * Hides the sub menu and returns to main menu
 */
static void hide_sub_menu(ai_pet_demo_t *demo)
{
    demo->current_menu = AI_PET_MENU_MAIN;
    lv_obj_add_flag(demo->sub_menu, LV_OBJ_FLAG_HIDDEN);
}

/**
 * Shows keyboard for pet name editing
 */
static void show_keyboard_for_pet_name(ai_pet_demo_t *demo)
{
    lv_keyboard_widget_show(demo->pet_stats.name, keyboard_callback, demo);
}

/**
 * Updates button selection visual style
 */
static void update_button_selection(uint8_t old_selection, uint8_t new_selection)
{
    // Reset old button style
    lv_obj_set_style_bg_color(demo_data.menu_buttons[old_selection], lv_color_white(), 0);
    lv_obj_set_style_border_width(demo_data.menu_buttons[old_selection], 0, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(demo_data.menu_buttons[old_selection], 0), lv_color_black(), 0);
    lv_obj_set_style_shadow_width(demo_data.menu_buttons[old_selection], 0, 0);

    // Set new button style
    lv_obj_set_style_bg_color(demo_data.menu_buttons[new_selection], lv_color_black(), 0);
    lv_obj_set_style_border_color(demo_data.menu_buttons[new_selection], lv_color_black(), 0);
    lv_obj_set_style_border_width(demo_data.menu_buttons[new_selection], 2, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(demo_data.menu_buttons[new_selection], 0), lv_color_white(), 0);
    lv_obj_set_style_shadow_width(demo_data.menu_buttons[new_selection], 0, 0);
}

/**
 * Updates sub menu item selection visual style
 */
static void update_sub_menu_selection(uint8_t old_selection, uint8_t new_selection)
{
    uint32_t child_count = lv_obj_get_child_cnt(demo_data.sub_menu_list);

    if (old_selection < child_count) {
        lv_obj_set_style_bg_color(lv_obj_get_child(demo_data.sub_menu_list, old_selection), lv_color_white(), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(demo_data.sub_menu_list, old_selection), lv_color_black(), 0);
    }

    if (new_selection < child_count) {
        lv_obj_set_style_bg_color(lv_obj_get_child(demo_data.sub_menu_list, new_selection), lv_color_black(), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(demo_data.sub_menu_list, new_selection), lv_color_white(), 0);
        lv_obj_scroll_to_view(lv_obj_get_child(demo_data.sub_menu_list, new_selection), LV_ANIM_ON);
    }
}

/**
 * Initializes pet statistics with default values
 */
static void init_pet_stats(ai_pet_stats_t *stats)
{
    stats->health = 85;
    stats->hungry = 60;
    stats->happy = 90;
    stats->age_days = 15;
    stats->weight_kg = 1.2f;
    strcpy(stats->name, "Ducky");
}

/**
 * Updates pet stats display (currently only updates info menu if shown)
 */
// static void update_pet_stats_display(ai_pet_demo_t *demo)
// {
//     if(demo->current_menu == AI_PET_MENU_INFO) {
//         show_info_menu(demo);
//     }
// }

/**
 * Finds the starting index of action items in the sub menu
 */
static uint32_t find_action_items_start(void)
{
    uint32_t child_count = lv_obj_get_child_cnt(demo_data.sub_menu_list);

    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(demo_data.sub_menu_list, i);

        // Check if this child is a label (Actions: label)
        if (lv_obj_check_type(child, &lv_label_class)) {
            const char *text = lv_label_get_text(child);
            if (text && strstr(text, "Actions:") != NULL) {
                return i + 1;
            }
        }

        // Also check if this child has a label child (for containers)
        lv_obj_t *label = lv_obj_get_child(child, 0);
        if (label && lv_obj_check_type(label, &lv_label_class)) {
            const char *text = lv_label_get_text(label);
            if (text && strstr(text, "Actions:") != NULL) {
                return i + 1;
            }
        }
    }

    return 0;
}

/**
 * Creates a stat display item with label and value
 */
static void create_stat_display_item(lv_obj_t *parent, const char *label, const char *value)
{
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_size(container, STAT_CONTAINER_WIDTH, STAT_CONTAINER_HEIGHT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 2, 0);

    lv_obj_t *label_obj = lv_label_create(container);
    lv_label_set_text(label_obj, label);
    lv_obj_align(label_obj, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_text_color(label_obj, lv_color_black(), 0);

    lv_obj_t *value_obj = lv_label_create(container);
    lv_label_set_text(value_obj, value);
    lv_obj_align(value_obj, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_text_color(value_obj, lv_color_black(), 0);
}

/**
 * Highlights the first item in the sub menu
 */
static void highlight_first_sub_menu_item(ai_pet_demo_t *demo)
{
    demo->sub_menu_selection = 0;
    if (lv_obj_get_child_cnt(demo->sub_menu_list) > 0) {
        lv_obj_set_style_bg_color(lv_obj_get_child(demo->sub_menu_list, 0), lv_color_black(), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(demo->sub_menu_list, 0), lv_color_white(), 0);
        lv_obj_scroll_to_view(lv_obj_get_child(demo->sub_menu_list, 0), LV_ANIM_ON);
    }
}

/**
 * Creates a sub menu with the given title and items
 */
static void create_sub_menu_with_items(ai_pet_demo_t *demo, const char *title, const char *symbols[],
                                       const char *items[], uint8_t item_count)
{
    demo->current_menu = AI_PET_MENU_INFO; // This will be overridden by specific menu functions
    lv_obj_clear_flag(demo->sub_menu, LV_OBJ_FLAG_HIDDEN);

    // Update title
    lv_obj_t *title_obj = lv_obj_get_child(demo->sub_menu, 0);
    lv_label_set_text(title_obj, title);

    lv_obj_clean(demo->sub_menu_list);

    // Add menu items
    for (uint8_t i = 0; i < item_count; i++) {
        lv_list_add_btn(demo->sub_menu_list, symbols[i], items[i]);
    }

    highlight_first_sub_menu_item(demo);
}
