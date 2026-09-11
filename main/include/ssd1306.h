/**
 * @file ssd1306.h
 * @brief Driver for SSD1306-based 128x64 OLED displays over I2C.
 *
 * The driver keeps an internal 1 KB framebuffer. Drawing calls mutate the
 * buffer and @ref ssd1306_update() pushes it to the panel in small chunks.
 * The panel can optionally be powered by the host using the VDD/GND GPIO
 * pins described in ssd1306_config_t.
 */

#ifndef SSD1306_H
#define SSD1306_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif



/* 5x7 font: one text row is 8 px tall; 8 rows fit on the 64px panel. */
#define DISPLAY_ORIGIN_X 0
#define ROW_BANNER       0
#define ROW_OWN_IP       8
#define ROW_PEER         16
#define ROW_CURRENT      24
#define ROW_AVERAGE      32
#define ROW_PEAK         40
#define ROW_COUNTS       48
#define ROW_SERVER       56




/* App-level text-render target: a fixed 8x24 grid of rows on the panel. */
#define OLED_NUM_ROWS 8
#define OLED_ROW_LEN  24

/* Display geometry (1 bit per pixel). */
#define SSD1306_WIDTH           128  /**< Panel width in pixels. */
#define SSD1306_HEIGHT          64   /**< Panel height in pixels. */
#define SSD1306_PIXELS_PER_PAGE 8    /**< Pixel rows per display page. */
#define SSD1306_BUFFER_SIZE     (SSD1306_WIDTH * SSD1306_HEIGHT / SSD1306_PIXELS_PER_PAGE)

/* Built-in 5x7 font metrics. */
#define SSD1306_FONT_START_CHAR ' '  /**< First glyph index (ASCII 32). */
#define SSD1306_FONT_END_CHAR   '~'  /**< Last glyph index (ASCII 126). */
#define SSD1306_FONT_WIDTH      5    /**< Glyph width in pixels. */
#define SSD1306_FONT_HEIGHT     7    /**< Glyph height in pixels. */

/** Default I2C address; override via ssd1306_config_t::i2c_address. */
#define SSD1306_DEFAULT_I2C_ADDR 0x3C

/**
 * @brief Pixel colour operations for drawing calls.
 */
typedef enum {
    SSD1306_COLOR_BLACK = 0,  /**< Turn the pixel off. */
    SSD1306_COLOR_WHITE = 1,  /**< Turn the pixel on. */
    SSD1306_COLOR_INVERT = 2  /**< Toggle the pixel. */
} ssd1306_color_t;

/**
 * @brief Hardware configuration for the display panel.
 */
typedef struct {
    gpio_num_t sda_pin;        /**< I2C data line. */
    gpio_num_t scl_pin;        /**< I2C clock line. */
    gpio_num_t vdd_pin;        /**< Optional: GPIO_NUM_NC if hardware-powered. */
    gpio_num_t gnd_pin;        /**< Optional: GPIO_NUM_NC if hardware-powered. */
    uint8_t    i2c_address;    /**< Default: SSD1306_DEFAULT_I2C_ADDR. */
    i2c_port_num_t i2c_port;   /**< I2C peripheral to use. */
} ssd1306_config_t;

typedef struct ssd1306_dev_t *ssd1306_handle_t;

/* Initialization & teardown */
esp_err_t ssd1306_init(const ssd1306_config_t *config,
                       ssd1306_handle_t *out_handle);
esp_err_t ssd1306_del(ssd1306_handle_t handle);

/* Display management */
esp_err_t ssd1306_update(ssd1306_handle_t handle);
void ssd1306_clear(ssd1306_handle_t handle, ssd1306_color_t color);
esp_err_t ssd1306_set_contrast(ssd1306_handle_t handle, uint8_t contrast);
esp_err_t ssd1306_set_display_on(ssd1306_handle_t handle, bool enable);

/* App-level convenience renderer: draws up to OLED_NUM_ROWS text rows. */
void oled_render(char rows[OLED_NUM_ROWS][OLED_ROW_LEN]);

/* Drawing API */
void ssd1306_draw_pixel(ssd1306_handle_t handle, int x, int y,
                        ssd1306_color_t color);
void ssd1306_draw_char(ssd1306_handle_t handle, int x, int y, char c,
                       ssd1306_color_t color);
void ssd1306_draw_string(ssd1306_handle_t handle, int x, int y,
                         const char *str, ssd1306_color_t color);
void ssd1306_draw_rect(ssd1306_handle_t handle, int x, int y, int w, int h,
                       ssd1306_color_t color, bool fill);

#ifdef __cplusplus
}
#endif

#endif /* SSD1306_H */