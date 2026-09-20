#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"

// Hardware and pin definitions
#define I2C_PORT             I2C_NUM_0
#define SDA_PIN              0
#define SCL_PIN              1
#define I2C_SPEED            100000

#define SHT31_ADDR           0x44
#define OLED_ADDR            0x3C

#define OLED_WIDTH           128
#define OLED_HEIGHT          64

static const char *TAG = "MAIN";

// Local memory buffer matching the 128x64 OLED resolution (1024 bytes)
static uint8_t oled_buffer[1024];

// Look-up table for 5x7 pixel ASCII characters
static const uint8_t font5x7[128][5] = {
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00},
    [':'] = {0x00, 0x36, 0x36, 0x00, 0x00},
    ['.'] = {0x00, 0x60, 0x60, 0x00, 0x00},
    ['%'] = {0x23, 0x13, 0x08, 0x64, 0x62},
    ['-'] = {0x08, 0x08, 0x08, 0x08, 0x08},
    ['0'] = {0x3E, 0x51, 0x49, 0x45, 0x3E},
    ['1'] = {0x00, 0x42, 0x7F, 0x40, 0x00},
    ['2'] = {0x42, 0x61, 0x51, 0x49, 0x46},
    ['3'] = {0x21, 0x41, 0x45, 0x4B, 0x31},
    ['4'] = {0x18, 0x14, 0x12, 0x7F, 0x10},
    ['5'] = {0x27, 0x45, 0x45, 0x45, 0x39},
    ['6'] = {0x3C, 0x4A, 0x49, 0x49, 0x30},
    ['7'] = {0x01, 0x71, 0x09, 0x05, 0x03},
    ['8'] = {0x36, 0x49, 0x49, 0x49, 0x36},
    ['9'] = {0x06, 0x49, 0x49, 0x29, 0x1E},
    ['C'] = {0x3E, 0x41, 0x41, 0x41, 0x22},
    ['H'] = {0x7F, 0x08, 0x08, 0x08, 0x7F},
    ['T'] = {0x01, 0x01, 0x7F, 0x01, 0x01},
};

// Configure ESP32 as I2C master with internal pull-ups enabled
static esp_err_t setup_i2c_bus(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SDA_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = SCL_PIN,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_SPEED,
    };
    i2c_param_config(I2C_PORT, &conf);
    return i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
}

// Send soft reset command to clear potential stuck states from power dips
static void reset_sht31(uint8_t addr) {
    uint8_t cmd[2] = {0x30, 0xA2}; 
    i2c_master_write_to_device(I2C_PORT, addr, cmd, sizeof(cmd), pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(20));
}

// Request measurement and convert the 6-byte response to physical values
static bool read_sensor(uint8_t addr, float *temp, float *hum) {
    uint8_t cmd[2] = {0x24, 0x00}; 
    
    if (i2c_master_write_to_device(I2C_PORT, addr, cmd, sizeof(cmd), pdMS_TO_TICKS(100)) != ESP_OK) {
        return false;
    }

    // Wait for the sensor to finish processing the measurement
    vTaskDelay(pdMS_TO_TICKS(30));

    uint8_t data[6];
    if (i2c_master_read_from_device(I2C_PORT, addr, data, sizeof(data), pdMS_TO_TICKS(100)) != ESP_OK) {
        return false;
    }

    // Combine raw bytes and calculate real values using datasheet formulas
    uint16_t raw_temp = (data[0] << 8) | data[1];
    uint16_t raw_hum  = (data[3] << 8) | data[4];

    *temp = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);
    *hum  = 100.0f * ((float)raw_hum / 65535.0f);

    return true;
}

// Send a single command byte to the OLED controller
static void send_oled_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    i2c_master_write_to_device(I2C_PORT, OLED_ADDR, buf, sizeof(buf), pdMS_TO_TICKS(100));
}

// Initialization sequence containing hardware configuration bytes for the SSD1306
static void init_screen(void) {
    const uint8_t init_cmds[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF
    };
    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        send_oled_cmd(init_cmds[i]);
    }
}

// Clear the local frame buffer
static void clear_buffer(void) {
    memset(oled_buffer, 0, sizeof(oled_buffer));
}

// Push the entire 1024-byte buffer to display memory in one transaction
static void push_to_screen(void) {
    // Set horizontal addressing bounds
    send_oled_cmd(0x21); send_oled_cmd(0); send_oled_cmd(127);
    send_oled_cmd(0x22); send_oled_cmd(0); send_oled_cmd(7);

    // Prepend the data stream identifier byte
    uint8_t tx_buf[1025];
    tx_buf[0] = 0x40; 
    memcpy(&tx_buf[1], oled_buffer, 1024);
    
    i2c_master_write_to_device(I2C_PORT, OLED_ADDR, tx_buf, sizeof(tx_buf), pdMS_TO_TICKS(200));
}

// Draw characters scaled by a factor of 2
// The math maps standard 2D coordinates into the 1D page-based OLED buffer format.
static void draw_text_2x(int x, int y, const char *str) {
    while (*str) {
        uint8_t uc = (uint8_t)*str;
        if (uc <= 127) {
            const uint8_t *glyph = font5x7[uc];
            
            // Iterate through the font glyph matrix
            for (int col = 0; col < 5; col++) {
                for (int row = 0; row < 7; row++) {
                    
                    // If the current pixel in the font matrix is active
                    if (glyph[col] & (1 << row)) {
                        
                        // Draw a 2x2 block for scaling
                        for (int dx = 0; dx < 2; dx++) {
                            for (int dy = 0; dy < 2; dy++) {
                                int px = x + (col * 2) + dx;
                                int py = y + (row * 2) + dy;

                                if (px >= 0 && px < OLED_WIDTH && py >= 0 && py < OLED_HEIGHT) {
                                    // Calculate the exact bit in the buffer array and set it
                                    oled_buffer[px + ((py / 8) * OLED_WIDTH)] |= (1 << (py % 8));
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // Advance X position for the next character
        x += 12; 
        str++;
    }
}

// Main application entry point managed by FreeRTOS
void app_main(void) {
    ESP_LOGI(TAG, "Initializing hardware");
    
    setup_i2c_bus();
    vTaskDelay(pdMS_TO_TICKS(100));

    reset_sht31(SHT31_ADDR);
    
    init_screen();
    clear_buffer();
    push_to_screen();

    char temp_str[16];
    char hum_str[16];

    // Infinite task loop
    while (1) {
        float temp = 0.0f, hum = 0.0f;

        // Try primary address, fallback to secondary if it fails
        if (read_sensor(0x44, &temp, &hum) || read_sensor(0x45, &temp, &hum)) {
            
            // Format float values into display strings
            snprintf(temp_str, sizeof(temp_str), "T: %.1f C", temp);
            snprintf(hum_str, sizeof(hum_str), "H: %.1f %%", hum);

            // Render updated text to the buffer and push
            clear_buffer(); 
            draw_text_2x(0, 0, temp_str);
            draw_text_2x(0, 32, hum_str);
            push_to_screen();

        } else {
            ESP_LOGE(TAG, "Sen  sor read failed");
        }

        // Suspend task for 2 seconds to yield CPU time
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}