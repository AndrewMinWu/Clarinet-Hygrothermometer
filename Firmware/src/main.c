#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"


#define I2C_PORT    I2C_NUM_0  // Exists 0 and 1, using 0.
#define SDA_PIN     0
#define SCL_PIN     1
#define I2C_SPEED   100000 // Rate of sending data (SDA)

#define SHT31_ADDR  0x44 // I2C bus adresses
#define OLED_ADDR   0x3C 

#define OLED_WIDTH  128 
#define OLED_HEIGHT 64


static const char *TAG = "MAIN"; // Text label used to show error location (main.c, if any) 

// 1024-byte local frame buffer mapping to the 128x64 OLED resolution
static uint8_t oled_buffer[1024];

// Lookup table for 5x7 pixel ASCII characters. Char into number (from ASCII) 
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


// Configure ESP32 as I2C master with internal pull-ups enabled. ESP-IDF type of esp_err_t (success or fail)
static esp_err_t setup_i2c_bus(void) {

    // Struct for config.
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER, //ESP32 is the master chip
        .sda_io_num = SDA_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE, //Internal (albeit weak) pullups. 
        .scl_io_num = SCL_PIN,
        .scl_pullup_en = GPIO_PULLUP_ENABLE, //Internal (albeit weak) pullups. 
        .master.clk_speed = I2C_SPEED,
    };
    i2c_param_config(I2C_PORT, &conf); // Sets the i2c port (0) with the config
    return i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0); // Activates the i2c hardware (0s are for slave)
}


// Send soft reset command to clear potential stuck states from power dips
static void reset_sht31(uint8_t addr) {

    uint8_t cmd[2] = {0x30, 0xA2}; //2 byte array, from the sensor these two bytes represent a soft reset
    i2c_master_write_to_device(I2C_PORT, addr, cmd, sizeof(cmd), pdMS_TO_TICKS(100)); // 100ms timeout

    // Delay for the sensor to process the reboot
    vTaskDelay(pdMS_TO_TICKS(20));
}


// Request measurement and convert the 6-byte response to physical values
static bool read_sensor(uint8_t addr, float *temp, float *hum) {

    
    uint8_t cmd[2] = {0x24, 0x00}; //2 byte array, from the sensor, sends a reading (payload transmit)
    
    // Send measurement command, if it fails, then exist the function with false
    if (i2c_master_write_to_device(I2C_PORT, addr, cmd, sizeof(cmd), pdMS_TO_TICKS(100)) != ESP_OK) {
        return false;
    }

    // Wait for the sensor to finish processing the measurement
    vTaskDelay(pdMS_TO_TICKS(30));

    // To store incoming data (sensor outputs 6 bytes)
    uint8_t data[6];
    
    // Catching the data in a 6-byte response
    if (i2c_master_read_from_device(I2C_PORT, addr, data, sizeof(data), pdMS_TO_TICKS(100)) != ESP_OK) {
        return false;
    }

    // Combine raw bytes into 16-bit integers (since using bitwise shifts auto fills), which the sensor uses
    // Basically "rebuilding" what the data originally was from the sensor
    uint16_t raw_temp = (data[0] << 8) | data[1];
    uint16_t raw_hum  = (data[3] << 8) | data[4];

    // Calculate physical values using formulas from the SHT31 datasheet
    *temp = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);
    *hum  = 100.0f * ((float)raw_hum / 65535.0f);
    
    return true;
}


// Send a single command byte to the OLED controller
static void send_oled_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd}; //2 byte array, from the oled to do a settings command with a spesific byte
    i2c_master_write_to_device(I2C_PORT, OLED_ADDR, buf, sizeof(buf), pdMS_TO_TICKS(100));
}


// Initialization sequence containing hardware configuration bytes
static void init_screen(void) {

    // Taken from the datasheet, 25 specific hex bytes required by the OLED controller to turn on
    const uint8_t init_cmds[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF
    };


    // Sends all the bytes to the setup function 
    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        send_oled_cmd(init_cmds[i]);
    }
}


// Clear the local frame buffer
static void clear_buffer(void) {

    // C function that resets the oled screen (all to 0)
    memset(oled_buffer, 0, sizeof(oled_buffer));
}


// Push the entire 1024-byte buffer to display memory
static void push_to_screen(void) {

    // OLED harware commands, setting column and page range (column/range, then the range from pixel to pixel)  
    send_oled_cmd(0x21); send_oled_cmd(0); send_oled_cmd(127);
    send_oled_cmd(0x22); send_oled_cmd(0); send_oled_cmd(7);

    uint8_t tx_buf[1025]; // Transmit data
    tx_buf[0] = 0x40; // To draw onto the screen, unlike 0x00 which is for hardware setup
    memcpy(&tx_buf[1], oled_buffer, 1024); // Updates the array, starting at slot 1 (since 0 is used for 0x40)

    // Draw onto oled
    i2c_master_write_to_device(I2C_PORT, OLED_ADDR, tx_buf, sizeof(tx_buf), pdMS_TO_TICKS(200));    
}


// Helper function to set a single pixel in memory 
static void draw_pixel(int x, int y) {

    // Ensure it is within screen boundaries
    if (x >= 0 && x < OLED_WIDTH && y >= 0 && y < OLED_HEIGHT) {
        
        // Calculate which 8-pixel vertical page the y-coordinate falls into (since vertical bytes)
        int page = y / 8;
    
        // Calculate the specific bit within that page (since modulo returns remainder)
        int bit = y % 8; 
        
        // Set the bit to turn the pixel on
        // Find the start of the page (row), then get to the byte (since its vertical), then find the spesific bit
        oled_buffer[x + (page * OLED_WIDTH)] |= (1 << bit);
    }
}


// Draw text scaled up by a factor of 2x for better visibility
static void draw_text_2x(int start_x, int start_y, const char *str) {

    // Tracks the horizontal position
    int current_x = start_x;
    
    while (*str) {

        // Converts the letter into ASCII integer value 
        uint8_t letter = *str;
        
        if (letter <= 127) {
            
            // Iterate through the 5 columns of the font array
            for (int col = 0; col < 5; col++) {

                // font5x7 array is used (row from ASCII, then column), each iteration changes the column
                uint8_t col_data = font5x7[letter][col];
                
                // Iterate through the 7 rows in the current column
                for (int row = 0; row < 7; row++) {
                    
                    // Check if the current bit in the array is active (matching the ASCII table)
                    if (col_data & (1 << row)) {
                        
                        // Calculate the starting pixel coordinates by applying the 2x scaling factor
                        int px = current_x + (col * 2);
                        int py = start_y + (row * 2);
                        
                        // Draw a 2x2 square to represent a single scaled pixel
                        draw_pixel(px, py);
                        draw_pixel(px + 1, py);
                        draw_pixel(px, py + 1);
                        draw_pixel(px + 1, py + 1);
                    }
                }
            }
            
            // Advance the X coordinate for the next character
            current_x += 12; 
        }
        

        // Moves along the string (or array/char pointer, by one)
        str++;
    }
}


// Main program
void app_main(void) {
    
    // Setup the i2c
    setup_i2c_bus();
    vTaskDelay(pdMS_TO_TICKS(100));

    // Reset the temp/humidity sensor
    reset_sht31(SHT31_ADDR);

    // Initialize the screen hardware config
    init_screen();
    
    // Clears the screen
    clear_buffer();
    push_to_screen();

    // Variables to store sensor readings
    float temp = 0.0f;
    float hum = 0.0f;

    // Final readings, i.e. "T: 22.4 C"
    char temp_str[16];
    char hum_str[16];

    // Constant checking loop
    while (1) {
        
        // Attempt to read from the primary address
        if (read_sensor(0x44, &temp, &hum)) {
            
            // Format floats into strings with one decimal place
            snprintf(temp_str, sizeof(temp_str), "T: %.1f C", temp);
            snprintf(hum_str, sizeof(hum_str), "H: %.1f %%", hum);

            // MUST clear before rewriting (else it would be a mess)
            clear_buffer(); 
            
            // Render text to the buffer
            draw_text_2x(0, 0, temp_str);
            draw_text_2x(0, 32, hum_str);

            // Updates the screen
            push_to_screen();
            
        } else {
            ESP_LOGE(TAG, "Sensor read failed");
        }
        
        // Suspend task for 500ms to yield CPU time and maintain a 2Hz polling rate
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}