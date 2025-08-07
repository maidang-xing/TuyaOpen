/**
 * @file keyboard_demo.c
 * Demo for the Custom Keyboard Widget
 */

#include "lv_keyboard_widget.h"
#include <stdio.h>

// Example callback function for keyboard results
static void demo_keyboard_callback(keyboard_result_t result, const char *text, void *user_data)
{
    switch (result) {
    case KEYBOARD_RESULT_OK:
        printf("Keyboard input confirmed: '%s'\n", text ? text : "");
        break;

    case KEYBOARD_RESULT_CANCEL:
        printf("Keyboard input cancelled\n");
        break;

    case KEYBOARD_RESULT_MENU:
        printf("Menu key pressed in keyboard\n");
        break;
    }
}

// Example function to demonstrate keyboard usage
void demo_keyboard_usage(void)
{
    // Initialize the keyboard widget
    lv_keyboard_widget_init();

    // Show keyboard with initial text
    lv_keyboard_widget_show("Hello", demo_keyboard_callback, NULL);

    // The keyboard will handle input until:
    // - User presses ENTER (confirms input)
    // - User presses ESC (cancels input)
    // - User presses 'm' key (menu function)

    // To hide the keyboard programmatically:
    // lv_keyboard_widget_hide();
}

// Example of how to check if keyboard is active
void demo_check_keyboard_status(void)
{
    if (lv_keyboard_widget_is_active()) {
        printf("Keyboard is currently active\n");
    } else {
        printf("Keyboard is not active\n");
    }
}

// Example of how to handle input in your main loop
void demo_handle_input(uint32_t key)
{
    // Check if keyboard is active first
    if (lv_keyboard_widget_is_active()) {
        lv_keyboard_widget_handle_input(key);
        return;
    }

    // Handle other input for your main application
    switch (key) {
    case 'k': // 'k' key to show keyboard
        lv_keyboard_widget_show(NULL, demo_keyboard_callback, NULL);
        break;

    default:
        // Handle other keys
        break;
    }
}
