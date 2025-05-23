/**
 * @file board_config.h
 * @brief board_config module is used to
 * @version 0.1
 * @date 2025-04-23
 */

#ifndef __BOARD_CONFIG_H__
#define __BOARD_CONFIG_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/
// lcd
#define DISPLAY_TYPE_UNKNOWN      0
#define DISPLAY_TYPE_OLED_SSD1306 1
#define DISPLAY_TYPE_LCD_SH8601   2
#define DISPLAY_TYPE_LCD_ST7789   3

#define BOARD_DISPLAY_TYPE DISPLAY_TYPE_LCD_ST7789


/* I2C */
#define XL9555_I2C_PORT 0
#define XL9555_INT_GPIO 40
#define XL9555_I2C_SDA 41
#define XL9555_I2C_SCL 42
#define XL9555_I2C_CLK 400 * 1000 
#define XL9555_I2C_ADDR 0x20 /* 7 位器件地址 */

/* SPI */
#define SPI_NUM      (1)
#define SPI_SCLK_IO  (12)
#define SPI_MOSI_IO  (11)
#define SPI_MISO_IO  (13)
#define SPI_FREQ     (20 * 1000 * 1000)

/* LCD */
#define DISPLAY_TYPE_LCD_ST7789V 0
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 240
#define DISPLAY_COLOR_DEPH 16

#define LCD_NUM_WR 40
#define LCD_NUM_CS 21

/* LVGL */
#define DISPLAY_BUFFER_SIZE (DISPLAY_WIDTH * 10)
#define DISPLAY_MONOCHROME false

/* rotation */
#define DISPLAY_SWAP_XY  true
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_INVERT   true

#define DISPLAY_COLOR_FORMAT LV_COLOR_FORMAT_RGB565
#define DISPLAY_BUFF_DMA   1
#define DISPLAY_SWAP_BYTES 1

// io expander
#define IO_EXPANDER_TYPE_UNKNOWN 0
#define IO_EXPANDER_TYPE_TCA9554 1

#define BOARD_IO_EXPANDER_TYPE IO_EXPANDER_TYPE_UNKNOWN

/* Example configurations */
#define I2S_INPUT_SAMPLE_RATE     (16000)
#define I2S_OUTPUT_SAMPLE_RATE     (16000)

/* I2C port and GPIOs */
#define I2C_NUM         (0)
#define I2C_SCL_IO      (42)
#define I2C_SDA_IO      (41)

/* I2S port and GPIOs */
#define I2S_NUM         (0)
#define I2S_MCK_IO      (3)
#define I2S_BCK_IO      (46)
#define I2S_WS_IO       (9)

#define I2S_DO_IO       (10)
#define I2S_DI_IO       (14)

#define AUDIO_CODEC_DMA_DESC_NUM    (6)
#define AUDIO_CODEC_DMA_FRAME_NUM   (240)
#define AUDIO_CODEC_ES8388_ADDR     (0x20)

/* XL9555 IO functions */
#define AP_INT_IO   0x0001  /* AP3216C interrupt pin P00       */
#define QMA_INT_IO  0x0002  /* QMA6100P interrupt pin P01      */
#define SPK_EN_IO   0x0004  /* Amplifier enable pin P02        */
#define BEEP_IO     0x0008  /* Buzzer control pin P03          */
#define OV_PWDN_IO  0x0010  /* Camera standby pin P04          */
#define OV_RESET_IO 0x0020  /* Camera reset pin P05            */
#define GBC_LED_IO  0x0040  /* ATK_MODULE LED pin P06          */
#define GBC_KEY_IO  0x0080  /* ATK_MODULE KEY pin P07          */
#define LCD_BL_IO   0x0100  /* RGB LCD backlight control P10   */
#define CT_RST_IO   0x0200  /* Touch screen interrupt pin P11  */
#define SLCD_RST_IO 0x0400  /* SPI_LCD reset pin P12           */
#define SLCD_PWR_IO 0x0800  /* SPI_LCD backlight control P13   */
#define KEY3_IO     0x1000  /* Button 3 pin P14                */
#define KEY2_IO     0x2000  /* Button 2 pin P15                */
#define KEY1_IO     0x4000  /* Button 1 pin P16                */
#define KEY0_IO     0x8000  /* Button 0 pin P17                */
/***********************************************************
***********************typedef define***********************
***********************************************************/

/***********************************************************
********************function declaration********************
***********************************************************/

int board_display_init(void);

void *board_display_get_panel_io_handle(void);

void *board_display_get_panel_handle(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_CONFIG_H__ */
