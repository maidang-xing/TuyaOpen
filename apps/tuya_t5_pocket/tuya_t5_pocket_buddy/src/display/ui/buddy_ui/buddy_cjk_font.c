/**
 * @file buddy_cjk_font.c
 * @brief CJK fallback font wrappers for buddy UI labels
 * @version 1.0
 * @date 2026-04-27
 * @copyright Copyright (c) Tuya Inc.
 */
#include "buddy_cjk_font.h"

/* ---------------------------------------------------------------------------
 * External font symbols
 * --------------------------------------------------------------------------- */
LV_FONT_DECLARE(lv_font_terminusTTF_Bold_14)
LV_FONT_DECLARE(lv_font_terminusTTF_Bold_16)
LV_FONT_DECLARE(lv_font_terminusTTF_Bold_18)
LV_FONT_DECLARE(ui_font_puhui_18_2)
LV_FONT_DECLARE(lv_font_montserrat_14)

/* ---------------------------------------------------------------------------
 * Public fonts (RAM copies, populated by buddy_cjk_font_init)
 *
 * Fallback chain: terminus -> puhui (CJK) -> montserrat_14 (FontAwesome
 * symbols like LV_SYMBOL_WIFI / LV_SYMBOL_BLUETOOTH / LV_SYMBOL_BATTERY_*).
 * We need a RAM copy of puhui too so we can chain its `.fallback`.
 * --------------------------------------------------------------------------- */
lv_font_t buddy_font_s;
lv_font_t buddy_font_m;
lv_font_t buddy_font_l;
STATIC lv_font_t s_buddy_puhui_ram;

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */
/**
 * @brief Initialize buddy CJK fallback fonts
 * @return none
 */
void buddy_cjk_font_init(void)
{
    static bool s_inited = false;
    if (s_inited) {
        return;
    }
    s_inited = true;

    s_buddy_puhui_ram = ui_font_puhui_18_2;
    s_buddy_puhui_ram.fallback = &lv_font_montserrat_14;

    buddy_font_s = lv_font_terminusTTF_Bold_14;
    buddy_font_s.fallback = &s_buddy_puhui_ram;

    buddy_font_m = lv_font_terminusTTF_Bold_16;
    buddy_font_m.fallback = &s_buddy_puhui_ram;

    buddy_font_l = lv_font_terminusTTF_Bold_18;
    buddy_font_l.fallback = &s_buddy_puhui_ram;
}
