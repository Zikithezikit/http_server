/**
 * @file ssd1306.c
 * @brief I2C driver for SSD1306-based 128x64 OLED displays.
 *
 * All drawing calls mutate the internal framebuffer; everything reaches the
 * panel only through ssd1306_update(), which transfers the frame in small
 * chunks to keep transactions short. The panel may be powered from two host
 * GPIO pins (VDD/GND); those are configured as push-pull outputs during init
 * when set to anything other than GPIO_NUM_NC.
 */

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ssd1306.h"

/** Diagnostic tag used by ESP_LOG* macros. */
static const char *TAG = "SSD1306";

/* Single shared instances (declared extern in common.h). */
ssd1306_handle_t s_oled;
SemaphoreHandle_t s_display_lock;

/* Control-byte prefixes for I2C packets. */
#define SSD1306_CTRL_CMD_PREFIX        0x00 /**< Following bytes are commands. */
#define SSD1306_CTRL_DATA_PREFIX       0x40 /**< Following bytes are display data. */

/* I2C bus settings. */
#define SSD1306_I2C_FREQ_HZ            400000 /**< SCL clock frequency in Hz. */
#define SSD1306_CMD_TIMEOUT_MS         100 /**< Per-command transaction timeout. */
#define SSD1306_UPDATE_TIMEOUT_MS      500 /**< Per-chunk update timeout. */

/* Frame transfer: short transactions instead of one long burst. */
#define SSD1306_TX_CHUNK_SIZE          32

/* Init retry behaviour. */
#define SSD1306_INIT_ATTEMPTS          2
#define SSD1306_INIT_RETRY_DELAY_MS    100

/* Power and electrical settings. */
#define SSD1306_POWER_UP_DELAY_MS      500 /**< Time to let the power rail settle. */
#define SSD1306_GLITCH_IGNORE_CNT      7 /**< I2C input glitch filter width (0..15). */

/* Repainting takes a short mutex lock; drop the frame if it is busy. */
#define SSD1306_RENDER_LOCK_TIMEOUT_MS 100

/* Panel command bytes used by this driver. */
#define SSD1306_CMD_DISPLAY_OFF        0xAE
#define SSD1306_CMD_DISPLAY_ON         0xAF
#define SSD1306_CMD_SET_CONTRAST       0x81
#define SSD1306_CMD_SET_COL_ADDR       0x21
#define SSD1306_CMD_SET_PAGE_ADDR      0x22
#define SSD1306_CMD_SET_CLOCK_DIV      0xD5
#define SSD1306_CMD_SET_MULTIPLEX      0xA8
#define SSD1306_CMD_SET_DISPLAY_OFFSET 0xD3
#define SSD1306_CMD_SET_START_LINE     0x40
#define SSD1306_CMD_CHARGE_PUMP        0x8D
#define SSD1306_CMD_SET_MEM_ADDR_MODE  0x20
#define SSD1306_CMD_SEGMENT_REMAP      0xA1
#define SSD1306_CMD_COM_SCAN_DIR       0xC8
#define SSD1306_CMD_SET_COM_PINS       0xDA
#define SSD1306_CMD_SET_PRECHARGE      0xD9
#define SSD1306_CMD_SET_VCOMH          0xDB
#define SSD1306_CMD_DISPLAY_RAM        0xA4
#define SSD1306_CMD_DISPLAY_NORMAL     0xA6

/* Configuration values written after the SET_* commands above. */
#define SSD1306_CLOCK_DIV_DEFAULT      0x80
#define SSD1306_MULTIPLEX_64           0x3F
#define SSD1306_DISPLAY_OFFSET_0       0x00
#define SSD1306_CHARGE_PUMP_ON         0x14
#define SSD1306_MEM_MODE_HORIZONTAL    0x00
#define SSD1306_COM_PINS_ALTERNATE     0x12
#define SSD1306_CONTRAST_DEFAULT       0xCF
#define SSD1306_PRECHARGE_DEFAULT      0xF1
#define SSD1306_VCOMH_DEFAULT          0x40

/** Horizontal spacing between glyphs (font width + 1 blank column). */
#define SSD1306_FONT_SPACING           (SSD1306_FONT_WIDTH + 1)

/**
 * Minimal 5x7 glyph set covering ASCII ' ' (32) through '~' (126).
 * Each glyph is an array of 5 column bitmasks, MSB on top.
 */
static const uint8_t font5x7[SSD1306_FONT_END_CHAR - SSD1306_FONT_START_CHAR + 1][SSD1306_FONT_WIDTH] = {
    /* ' ' */  {0x00, 0x00, 0x00, 0x00, 0x00},
    /* '!' */  {0x00, 0x00, 0x5F, 0x00, 0x00},
    /* '"' */  {0x00, 0x07, 0x00, 0x07, 0x00},
    /* '#' */  {0x14, 0x7F, 0x14, 0x7F, 0x14},
    /* '$' */  {0x24, 0x2A, 0x7F, 0x2A, 0x12},
    /* '%' */  {0x23, 0x13, 0x08, 0x64, 0x62},
    /* '&' */  {0x36, 0x49, 0x55, 0x22, 0x50},
    /* ''' */  {0x00, 0x05, 0x03, 0x00, 0x00},
    /* '(' */  {0x00, 0x1C, 0x22, 0x41, 0x00},
    /* ')' */  {0x00, 0x41, 0x22, 0x1C, 0x00},
    /* '*' */  {0x14, 0x08, 0x3E, 0x08, 0x14},
    /* '+' */  {0x08, 0x08, 0x3E, 0x08, 0x08},
    /* ',' */  {0x00, 0x50, 0x30, 0x00, 0x00},
    /* '-' */  {0x08, 0x08, 0x08, 0x08, 0x08},
    /* '.' */  {0x00, 0x60, 0x60, 0x00, 0x00},
    /* '/' */  {0x20, 0x10, 0x08, 0x04, 0x02},
    /* '0' */  {0x3E, 0x51, 0x49, 0x45, 0x3E},
    /* '1' */  {0x00, 0x42, 0x7F, 0x40, 0x00},
    /* '2' */  {0x42, 0x61, 0x51, 0x49, 0x46},
    /* '3' */  {0x21, 0x41, 0x45, 0x4B, 0x31},
    /* '4' */  {0x18, 0x14, 0x12, 0x7F, 0x10},
    /* '5' */  {0x27, 0x45, 0x45, 0x45, 0x39},
    /* '6' */  {0x3C, 0x4A, 0x49, 0x49, 0x30},
    /* '7' */  {0x01, 0x71, 0x09, 0x05, 0x03},
    /* '8' */  {0x36, 0x49, 0x49, 0x49, 0x36},
    /* '9' */  {0x06, 0x49, 0x49, 0x29, 0x1E},
    /* ':' */  {0x00, 0x36, 0x36, 0x00, 0x00},
    /* ';' */  {0x00, 0x56, 0x36, 0x00, 0x00},
    /* '<' */  {0x08, 0x14, 0x22, 0x41, 0x00},
    /* '=' */  {0x14, 0x14, 0x14, 0x14, 0x14},
    /* '>' */  {0x00, 0x41, 0x22, 0x14, 0x08},
    /* '?' */  {0x02, 0x01, 0x51, 0x09, 0x06},
    /* '@' */  {0x32, 0x49, 0x79, 0x41, 0x3E},
    /* 'A' */  {0x7E, 0x11, 0x11, 0x11, 0x7E},
    /* 'B' */  {0x7F, 0x49, 0x49, 0x49, 0x36},
    /* 'C' */  {0x3E, 0x41, 0x41, 0x41, 0x22},
    /* 'D' */  {0x7F, 0x41, 0x41, 0x22, 0x1C},
    /* 'E' */  {0x7F, 0x49, 0x49, 0x49, 0x41},
    /* 'F' */  {0x7F, 0x09, 0x09, 0x09, 0x01},
    /* 'G' */  {0x3E, 0x41, 0x49, 0x49, 0x7A},
    /* 'H' */  {0x7F, 0x08, 0x08, 0x08, 0x7F},
    /* 'I' */  {0x00, 0x41, 0x7F, 0x41, 0x00},
    /* 'J' */  {0x20, 0x40, 0x41, 0x3F, 0x01},
    /* 'K' */  {0x7F, 0x08, 0x14, 0x22, 0x41},
    /* 'L' */  {0x7F, 0x40, 0x40, 0x40, 0x40},
    /* 'M' */  {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    /* 'N' */  {0x7F, 0x04, 0x08, 0x10, 0x7F},
    /* 'O' */  {0x3E, 0x41, 0x41, 0x41, 0x3E},
    /* 'P' */  {0x7F, 0x09, 0x09, 0x09, 0x06},
    /* 'Q' */  {0x3E, 0x41, 0x51, 0x21, 0x5E},
    /* 'R' */  {0x7F, 0x09, 0x19, 0x29, 0x46},
    /* 'S' */  {0x46, 0x49, 0x49, 0x49, 0x31},
    /* 'T' */  {0x01, 0x01, 0x7F, 0x01, 0x01},
    /* 'U' */  {0x3F, 0x40, 0x40, 0x40, 0x3F},
    /* 'V' */  {0x1F, 0x20, 0x40, 0x20, 0x1F},
    /* 'W' */  {0x3F, 0x40, 0x38, 0x40, 0x3F},
    /* 'X' */  {0x63, 0x14, 0x08, 0x14, 0x63},
    /* 'Y' */  {0x07, 0x08, 0x70, 0x08, 0x07},
    /* 'Z' */  {0x61, 0x51, 0x49, 0x45, 0x43},
    /* '[' */  {0x00, 0x7F, 0x41, 0x41, 0x00},
    /* '\' */  {0x02, 0x04, 0x08, 0x10, 0x20},
    /* ']' */  {0x00, 0x41, 0x41, 0x7F, 0x00},
    /* '^' */  {0x04, 0x02, 0x01, 0x02, 0x04},
    /* '_' */  {0x40, 0x40, 0x40, 0x40, 0x40},
    /* '`' */  {0x00, 0x01, 0x02, 0x04, 0x00},
    /* 'a' */  {0x20, 0x54, 0x54, 0x54, 0x78},
    /* 'b' */  {0x7F, 0x48, 0x44, 0x44, 0x38},
    /* 'c' */  {0x38, 0x44, 0x44, 0x44, 0x20},
    /* 'd' */  {0x38, 0x44, 0x44, 0x48, 0x7F},
    /* 'e' */  {0x38, 0x54, 0x54, 0x54, 0x18},
    /* 'f' */  {0x08, 0x7E, 0x09, 0x01, 0x02},
    /* 'g' */  {0x0C, 0x52, 0x52, 0x52, 0x3E},
    /* 'h' */  {0x7F, 0x08, 0x04, 0x04, 0x78},
    /* 'i' */  {0x00, 0x44, 0x7D, 0x40, 0x00},
    /* 'j' */  {0x20, 0x40, 0x44, 0x3D, 0x00},
    /* 'k' */  {0x7F, 0x10, 0x28, 0x44, 0x00},
    /* 'l' */  {0x00, 0x41, 0x7F, 0x40, 0x00},
    /* 'm' */  {0x7C, 0x04, 0x18, 0x04, 0x78},
    /* 'n' */  {0x7C, 0x08, 0x04, 0x04, 0x78},
    /* 'o' */  {0x38, 0x44, 0x44, 0x44, 0x38},
    /* 'p' */  {0x7C, 0x14, 0x14, 0x14, 0x08},
    /* 'q' */  {0x08, 0x14, 0x14, 0x18, 0x7C},
    /* 'r' */  {0x7C, 0x08, 0x04, 0x04, 0x08},
    /* 's' */  {0x48, 0x54, 0x54, 0x54, 0x20},
    /* 't' */  {0x04, 0x3F, 0x44, 0x40, 0x20},
    /* 'u' */  {0x3C, 0x40, 0x40, 0x20, 0x7C},
    /* 'v' */  {0x1C, 0x20, 0x40, 0x20, 0x1C},
    /* 'w' */  {0x3C, 0x40, 0x30, 0x40, 0x3C},
    /* 'x' */  {0x44, 0x28, 0x10, 0x28, 0x44},
    /* 'y' */  {0x0C, 0x50, 0x50, 0x50, 0x3C},
    /* 'z' */  {0x44, 0x64, 0x54, 0x4C, 0x44},
    /* '{' */  {0x08, 0x36, 0x41, 0x00, 0x00},
    /* '|' */  {0x00, 0x00, 0x77, 0x00, 0x00},
    /* '}' */  {0x00, 0x41, 0x36, 0x08, 0x00},
    /* '~' */  {0x02, 0x01, 0x02, 0x04, 0x02},
};

/**
 * @brief Device handle: I2C handles plus the internal framebuffer.
 */
struct ssd1306_dev_t {
    i2c_master_bus_handle_t bus_handle;  /**< I2C master bus owned by this device. */
    i2c_master_dev_handle_t dev_handle;  /**< Attached device on the bus. */
    uint8_t buffer[SSD1306_BUFFER_SIZE]; /**< 1-bit-per-pixel framebuffer. */
};

/**
 * @brief Sends a single command byte to the panel.
 *
 * @param handle Initialised device handle.
 * @param cmd    Command byte (prefixed with the command control byte).
 * @return ESP_OK on success, otherwise the I2C error code.
 */
static esp_err_t ssd1306_send_cmd(ssd1306_handle_t handle, uint8_t cmd)
{
    uint8_t pkt[2] = {SSD1306_CTRL_CMD_PREFIX, cmd};
    return i2c_master_transmit(handle->dev_handle, pkt, sizeof(pkt),
                               SSD1306_CMD_TIMEOUT_MS);
}

/**
 * @brief Configures the VDD/GND GPIO pins as push-pull power rail outputs.
 *
 * @param vdd_pin VDD pin, or GPIO_NUM_NC to skip.
 * @param gnd_pin GND pin, or GPIO_NUM_NC to skip.
 */
static void configure_power_pins(gpio_num_t vdd_pin, gpio_num_t gnd_pin)
{
    if (gnd_pin != GPIO_NUM_NC) {
        gpio_config_t cfg = {
            .pin_bit_mask = (1ULL << gnd_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
        };
        if (gpio_config(&cfg) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to configure GND pin %d", gnd_pin);
        } else {
            gpio_set_level(gnd_pin, 0);
        }
    }

    if (vdd_pin != GPIO_NUM_NC) {
        gpio_config_t cfg = {
            .pin_bit_mask = (1ULL << vdd_pin),
            .mode = GPIO_MODE_OUTPUT,
        };
        if (gpio_config(&cfg) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to configure VDD pin %d", vdd_pin);
        } else {
            gpio_set_level(vdd_pin, 1);
        }
    }

    /* Give the panel's power rail time to stabilise before talking to it. */
    vTaskDelay(pdMS_TO_TICKS(SSD1306_POWER_UP_DELAY_MS));
}

/**
 * @brief Sends the full SSD1306 init command sequence to the panel.
 *
 * @param handle Initialised device handle.
 * @return ESP_OK if every command was acknowledged, otherwise the first
 *         failing error code.
 */
static esp_err_t send_init_commands(ssd1306_handle_t handle)
{
    static const uint8_t init_cmds[] = {
        SSD1306_CMD_DISPLAY_OFF,          // Display off
        SSD1306_CMD_SET_CLOCK_DIV, SSD1306_CLOCK_DIV_DEFAULT,
        SSD1306_CMD_SET_MULTIPLEX, SSD1306_MULTIPLEX_64,
        SSD1306_CMD_SET_DISPLAY_OFFSET, SSD1306_DISPLAY_OFFSET_0,
        SSD1306_CMD_SET_START_LINE,       // Start line 0
        SSD1306_CMD_CHARGE_PUMP, SSD1306_CHARGE_PUMP_ON,
        SSD1306_CMD_SET_MEM_ADDR_MODE, SSD1306_MEM_MODE_HORIZONTAL,
        SSD1306_CMD_SEGMENT_REMAP,        // Reflect X
        SSD1306_CMD_COM_SCAN_DIR,         // Reflect Y
        SSD1306_CMD_SET_COM_PINS, SSD1306_COM_PINS_ALTERNATE,
        SSD1306_CMD_SET_CONTRAST, SSD1306_CONTRAST_DEFAULT,
        SSD1306_CMD_SET_PRECHARGE, SSD1306_PRECHARGE_DEFAULT,
        SSD1306_CMD_SET_VCOMH, SSD1306_VCOMH_DEFAULT,
        SSD1306_CMD_DISPLAY_RAM,          // Output follows RAM content
        SSD1306_CMD_DISPLAY_NORMAL,       // Normal (non-inverted) display
        SSD1306_CMD_DISPLAY_ON,           // Display on
    };

    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        esp_err_t err = ssd1306_send_cmd(handle, init_cmds[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Init command %zu (0x%02x) failed: 0x%x", i,
                     init_cmds[i], err);
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t ssd1306_init(const ssd1306_config_t *config,
                       ssd1306_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_handle = NULL;

    ssd1306_handle_t dev = calloc(1, sizeof(struct ssd1306_dev_t));
    if (dev == NULL) {
        return ESP_ERR_NO_MEM;
    }

    configure_power_pins(config->vdd_pin, config->gnd_pin);

    i2c_master_bus_config_t bus_config = {
        .i2c_port = config->i2c_port,
        .sda_io_num = config->sda_pin,
        .scl_io_num = config->scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = SSD1306_GLITCH_IGNORE_CNT,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &dev->bus_handle);
    if (ret != ESP_OK) {
        free(dev);
        return ret;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address =
            config->i2c_address ? config->i2c_address : SSD1306_DEFAULT_I2C_ADDR,
        .scl_speed_hz = SSD1306_I2C_FREQ_HZ,
    };

    ret = i2c_master_bus_add_device(dev->bus_handle, &dev_config,
                                    &dev->dev_handle);
    if (ret != ESP_OK) {
        i2c_del_master_bus(dev->bus_handle);
        free(dev);
        return ret;
    }

    /* Some panels do not answer for a moment after power-up; retry the full
     * command sequence before giving up. */
    for (int attempt = 0; attempt < SSD1306_INIT_ATTEMPTS; attempt++) {
        ret = send_init_commands(dev);
        if (ret == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(SSD1306_INIT_RETRY_DELAY_MS));
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Panel not ACKing init; check power/wiring (SDA/SCL)");
        i2c_master_bus_rm_device(dev->dev_handle);
        i2c_del_master_bus(dev->bus_handle);
        free(dev);
        return ret;
    }

    *out_handle = dev;
    ESP_LOGI(TAG, "Initialized successfully");
    return ESP_OK;
}

esp_err_t ssd1306_del(ssd1306_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    i2c_master_bus_rm_device(handle->dev_handle);
    i2c_del_master_bus(handle->bus_handle);
    free(handle);
    return ESP_OK;
}

esp_err_t ssd1306_update(ssd1306_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Point the panel at the full frame: columns 0..WIDTH-1, all pages. */
    const uint8_t window_cmds[] = {
        SSD1306_CMD_SET_COL_ADDR, 0, SSD1306_WIDTH - 1,
        SSD1306_CMD_SET_PAGE_ADDR, 0,
        SSD1306_HEIGHT / SSD1306_PIXELS_PER_PAGE - 1,
    };
    for (size_t i = 0; i < sizeof(window_cmds); i++) {
        esp_err_t err = ssd1306_send_cmd(handle, window_cmds[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Update window cmd %zu failed: 0x%x", i, err);
            return err;
        }
    }

    /* Transfer the frame in several short I2C transactions instead of one
     * long burst: some panels/clones NACK (0x108) past a few dozen bytes,
     * and a small stack buffer is cheaper than copying the whole frame. */
    uint8_t tx_buf[1 + SSD1306_TX_CHUNK_SIZE];
    tx_buf[0] = SSD1306_CTRL_DATA_PREFIX; /* Controls non-command bytes. */

    for (size_t offset = 0; offset < SSD1306_BUFFER_SIZE;
         offset += SSD1306_TX_CHUNK_SIZE) {
        size_t len = SSD1306_BUFFER_SIZE - offset;
        if (len > SSD1306_TX_CHUNK_SIZE) {
            len = SSD1306_TX_CHUNK_SIZE;
        }
        memcpy(&tx_buf[1], &handle->buffer[offset], len);

        esp_err_t err = i2c_master_transmit(handle->dev_handle, tx_buf,
                                            1 + len, SSD1306_UPDATE_TIMEOUT_MS);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Frame chunk @%zu failed: 0x%x", offset, err);
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t ssd1306_set_contrast(ssd1306_handle_t handle, uint8_t contrast)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ssd1306_send_cmd(handle, SSD1306_CMD_SET_CONTRAST);
    if (err != ESP_OK) {
        return err;
    }
    return ssd1306_send_cmd(handle, contrast);
}

esp_err_t ssd1306_set_display_on(ssd1306_handle_t handle, bool enable)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return ssd1306_send_cmd(handle,
                            enable ? SSD1306_CMD_DISPLAY_ON
                                   : SSD1306_CMD_DISPLAY_OFF);
}

void ssd1306_clear(ssd1306_handle_t handle, ssd1306_color_t color)
{
    if (handle == NULL) {
        return;
    }
    /* A white clear turns every pixel on; anything else blanks the panel. */
    uint8_t fill = (color == SSD1306_COLOR_WHITE) ? 0xFF : 0x00;
    memset(handle->buffer, fill, SSD1306_BUFFER_SIZE);
}

void ssd1306_draw_pixel(ssd1306_handle_t handle, int x, int y,
                        ssd1306_color_t color)
{
    if (handle == NULL ||
        x < 0 || x >= SSD1306_WIDTH ||
        y < 0 || y >= SSD1306_HEIGHT) {
        return;
    }

    /* Frame rows are packed 8 pixels (one page) per byte. */
    size_t index = x + (y / SSD1306_PIXELS_PER_PAGE) * SSD1306_WIDTH;
    uint8_t bit = 1 << (y % SSD1306_PIXELS_PER_PAGE);

    switch (color) {
        case SSD1306_COLOR_WHITE:
            handle->buffer[index] |= bit;
            break;
        case SSD1306_COLOR_BLACK:
            handle->buffer[index] &= ~bit;
            break;
        case SSD1306_COLOR_INVERT:
            handle->buffer[index] ^= bit;
            break;
    }
}

void ssd1306_draw_char(ssd1306_handle_t handle, int x, int y, char c,
                       ssd1306_color_t color)
{
    if (handle == NULL ||
        c < SSD1306_FONT_START_CHAR || c > SSD1306_FONT_END_CHAR) {
        return;
    }

    const uint8_t *glyph =
        font5x7[c - SSD1306_FONT_START_CHAR];
    for (int col = 0; col < SSD1306_FONT_WIDTH; col++) {
        uint8_t line = glyph[col];
        for (int row = 0; row < SSD1306_FONT_HEIGHT; row++) {
            if (line & (1 << row)) {
                ssd1306_draw_pixel(handle, x + col, y + row, color);
            }
        }
    }
}

void ssd1306_draw_string(ssd1306_handle_t handle, int x, int y, const char *str,
                         ssd1306_color_t color)
{
    if (handle == NULL || str == NULL) {
        return;
    }
    while (*str != '\0') {
        ssd1306_draw_char(handle, x, y, *str, color);
        x += SSD1306_FONT_SPACING;
        str++;
    }
}

void ssd1306_draw_rect(ssd1306_handle_t handle, int x, int y, int w, int h,
                       ssd1306_color_t color, bool fill)
{
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            bool on_frame = i == 0 || i == w - 1 || j == 0 || j == h - 1;
            if (fill || on_frame) {
                ssd1306_draw_pixel(handle, x + i, y + j, color);
            }
        }
    }
}

/**
 * @brief Repaints the whole panel from a fixed grid of text rows.
 *
 * Clears the framebuffer and draws each row top-to-bottom as a single line
 * of white text. Empty rows draw nothing. The display lock is held for the
 * duration of the repaint so concurrent drawing from other tasks cannot
 * interleave.
 *
 * @param rows OLED_NUM_ROWS rows of up to OLED_ROW_LEN chars each.
 */
void oled_render(char rows[OLED_NUM_ROWS][OLED_ROW_LEN])
{
    if (s_oled == NULL || s_display_lock == NULL) {
        return;
    }
    if (xSemaphoreTake(s_display_lock,
                       SSD1306_RENDER_LOCK_TIMEOUT_MS / portTICK_PERIOD_MS)
        != pdTRUE) {
        return;
    }

    ssd1306_clear(s_oled, SSD1306_COLOR_BLACK);
    int y = ROW_BANNER;
    for (int i = 0; i < OLED_NUM_ROWS; i++) {
        ssd1306_draw_string(s_oled, DISPLAY_ORIGIN_X, y, rows[i],
                            SSD1306_COLOR_WHITE);
        y += SSD1306_PIXELS_PER_PAGE;
    }
    ssd1306_update(s_oled);

    xSemaphoreGive(s_display_lock);
}
