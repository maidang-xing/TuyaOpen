/**
 * @file lcd_st7789.c
 * @brief lcd_st7789 module is used to
 * @version 0.1
 * @date 2025-05-13
 */

#include "lcd_st7789.h"

#include "board_config.h"

#include "esp_err.h"
#include "esp_log.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"

#include "gpio.h"
#include "gpio_types.h"

/***********************************************************
************************macro define************************
***********************************************************/
#define TAG "LCD_ST7789"

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct {
    esp_lcd_panel_io_handle_t panel_io;
    esp_lcd_panel_handle_t panel;
} LCD_CONFIG_T;

/***********************************************************
********************function declaration********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/
static LCD_CONFIG_T lcd_config = {0};

static int __lcd_spi_init(void)
{
    esp_err_t esp_rt = ESP_OK;

    spi_bus_config_t bus_cfg = {0};
    bus_cfg.sclk_io_num = SPI_SCLK_IO;
    bus_cfg.mosi_io_num = SPI_MOSI_IO;
    bus_cfg.miso_io_num = -1;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);

    esp_rt = spi_bus_initialize(SPI_NUM, &bus_cfg, SPI_DMA_CH_AUTO);
    if (esp_rt != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(esp_rt));
        return -1;
    }

    return 0;
}

int lcd_st7789_init(void)
{
    int rt = 0;

    rt = __lcd_spi_init();
    if (rt != 0) {
        return -1;
    }

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num     = LCD_NUM_WR,       // GPIO for DC signal, set -1 if not used
        .cs_gpio_num     = LCD_NUM_CS,       // GPIO for CS signal
        .pclk_hz         = SPI_FREQ, // SPI clock: 20MHz (ST7789 max support 62.5MHz)
        .lcd_cmd_bits    = 8,               // Command bit-width
        .lcd_param_bits  = 8,               // Parameter bit-width
        .spi_mode        = 0,               // SPI mode 0 (CPOL=0, CPHA=0)
        .trans_queue_depth = 7,             // Optimal for 800*480 resolution
    };
    

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI_NUM, &io_config, &(lcd_config.panel_io)));
    ESP_LOGI(TAG, "esp_lcd_new_panel_io_spi");

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = -1;
    panel_config.flags.reset_active_high = 1,
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = DISPLAY_COLOR_DEPH;
    panel_config.data_endian = LCD_RGB_DATA_ENDIAN_BIG;
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(lcd_config.panel_io, &panel_config, &lcd_config.panel));

    esp_lcd_panel_reset(lcd_config.panel);
    esp_lcd_panel_init(lcd_config.panel);
    esp_lcd_panel_invert_color(lcd_config.panel, DISPLAY_INVERT);
    esp_lcd_panel_swap_xy(lcd_config.panel, DISPLAY_SWAP_XY);
    esp_lcd_panel_mirror(lcd_config.panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

    ESP_LOGI(TAG, "LCD init successfully");
    return 0;
}

void *lcd_st7789_get_panel_io_handle(void)
{
    return lcd_config.panel_io;
}

void *lcd_st7789_get_panel_handle(void)
{
    return lcd_config.panel;
}
