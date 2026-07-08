#include <stdio.h>              // Standard I/O functions
#include <string.h>             // For string manipulation
#include "dht11.h"              // DHT11 sensor driver (user-defined or library)
#include "freertos/FreeRTOS.h"  // Core FreeRTOS definitions
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gptimer.h"
#include "esp_log.h"
#include "esp_timer.h"

// GPIO pin connected to DHT11 data line
#define CONFIG_DHT11_PIN GPIO_NUM_4

// Timeout threshold for DHT11 reading in seconds
#define CONFIG_CONNECTION_TIMEOUT 5

static const char *TAG = "example";

#include "sh1107.h"
//#include "font8x8_basic.h"

#define TAG_DP "SH1107"

typedef struct {
    uint64_t event_count;
} example_queue_element_t;

typedef enum {
    BURST,
    WAIT
} alarm_status_t;

typedef struct {
    QueueHandle_t queue_handle;
    SH1107_t* display_handle;
} task_arg_t;


// alarm_status_t ele =  WAIT;
// static QueueHandle_t queue = NULL;

static bool IRAM_ATTR example_timer_on_alarm_cb_v2(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_awoken = pdFALSE;
    QueueHandle_t queue = (QueueHandle_t)user_data;
    if (queue == NULL) return false;
    // Retrieve count value and send to queue
    // example_queue_element_t ele = {
    //     .event_count = edata->count_value
    // };
    alarm_status_t ele = BURST;
    xQueueSendFromISR(queue, &ele, &high_task_awoken);
    // return whether we need to yield at the end of ISR
    return (high_task_awoken == pdTRUE);
}

static void periodic_timer_callback(void *arg){
    ESP_LOGI(TAG, "ESP SW TIMER");
    // ele = BURST;
}

static void turn_on_DHT11(void* arg){
    task_arg_t *args = (task_arg_t*)arg;
    QueueHandle_t queue = args->queue_handle;
    SH1107_t* dev = args->display_handle;
    alarm_status_t ele;

    // Buffers to hold temperature and humidity strings for display
    char temp_str[20];
    char humidity_str[25];

    // Declare and configure the DHT11 sensor structure
    dht11_t dht11_sensor;
    dht11_sensor.dht11_pin = CONFIG_DHT11_PIN;
    ESP_LOGI(TAG, "DHT11 ready");

    for(;;){
        // ESP_LOGI(TAG, "check queue");
        if (xQueueReceive(queue, &ele, portMAX_DELAY)) {
            // Static variables to store previous temperature and humidity
            static uint8_t prev_temp = 0xFF; // 0xFF indicates undefined initial state
            static uint8_t prev_hum  = 0xFF;

            // Read sensor values; returns 0 on success
            // if (ele == WAIT) continue;
            // if (ele == BURST) {
                ele =  WAIT;
                if (!dht11_read(&dht11_sensor, CONFIG_CONNECTION_TIMEOUT)) {
                    // Cast sensor readings to 8-bit unsigned integers
                    uint8_t curr_temp = (uint8_t)dht11_sensor.temperature;
                    uint8_t curr_hum  = (uint8_t)dht11_sensor.humidity;

                    // Update display only if values have changed
                    if (curr_temp != prev_temp || curr_hum != prev_hum) {

                        // Format temperature and humidity strings
                        sprintf(temp_str,     "Temp: %dC", curr_temp);
                        sprintf(humidity_str, "Humidity: %d%%", curr_hum);

                        ESP_LOGI(TAG, "%s", temp_str);
                        ESP_LOGI(TAG, "%s", humidity_str);

                        sh1107_display_text(dev, 5, 0, temp_str, strlen(temp_str), false);
                        sh1107_display_text(dev, 6, 0, humidity_str, strlen(humidity_str), false);
                        // sh1107_display_text(dev, 8, 0, "     GoodBye    ", 16, false);

                        // Store current values for future comparison
                        prev_temp = curr_temp;
                        prev_hum  = curr_hum;
                    }
                }
            // }
        }
    }
}

// Main application entry point
void app_main() {

    QueueHandle_t queue = xQueueCreate(10, sizeof(alarm_status_t));
    if (!queue) {
        ESP_LOGE(TAG, "Creating queue failed");
        return;
    }

    static SH1107_t dev;
    task_arg_t pass_to_task_arg = {
        .queue_handle = queue,
        .display_handle = &dev,
    };
    xTaskCreate(turn_on_DHT11, "turn on DHT11", 8192, &pass_to_task_arg, 10, NULL);

#if CONFIG_I2C_INTERFACE
	ESP_LOGI(TAG_DP, "INTERFACE is i2c");
	ESP_LOGI(TAG_DP, "CONFIG_SDA_GPIO=%d",CONFIG_SDA_GPIO);
	ESP_LOGI(TAG_DP, "CONFIG_SCL_GPIO=%d",CONFIG_SCL_GPIO);
	ESP_LOGI(TAG_DP, "CONFIG_RESET_GPIO=%d",CONFIG_I2C_RESET_GPIO);
	i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, CONFIG_I2C_RESET_GPIO);
#endif // CONFIG_I2C_INTERFACE

#if CONFIG_SPI_INTERFACE
	ESP_LOGI(TAG_DP, "INTERFACE is SPI");
	ESP_LOGI(TAG_DP, "CONFIG_MOSI_GPIO=%d",CONFIG_MOSI_GPIO);
	ESP_LOGI(TAG_DP, "CONFIG_SCLK_GPIO=%d",CONFIG_SCLK_GPIO);
	ESP_LOGI(TAG_DP, "CONFIG_CS_GPIO=%d",CONFIG_CS_GPIO);
	ESP_LOGI(TAG_DP, "CONFIG_DC_GPIO=%d",CONFIG_DC_GPIO);
	ESP_LOGI(TAG_DP, "CONFIG_RESET_GPIO=%d",CONFIG_SPI_RESET_GPIO);
	spi_master_init(&dev, CONFIG_MOSI_GPIO, CONFIG_SCLK_GPIO, CONFIG_CS_GPIO, CONFIG_DC_GPIO, CONFIG_SPI_RESET_GPIO);
#endif
	sh1107_init(&dev, 128, 128);
	sh1107_contrast(&dev, 0xff);

    // vTaskDelay(100 / portTICK_PERIOD_MS);

    sh1107_clear_screen(&dev, false);
    sh1107_direction(&dev, DIRECTION0);
    sh1107_contrast(&dev, 0xff);
    sh1107_display_text(&dev, 2, 0, "     Hello      ", 16, false);
    sh1107_display_text(&dev, 3, 0, "     ESP32      ", 16, false);

    // vTaskDelay(100 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Create timer handle");
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1MHz, 1 tick=1us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    // set a new callback function
    gptimer_event_callbacks_t cbs;
    cbs.on_alarm = example_timer_on_alarm_cb_v2;
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, queue));
    ESP_LOGI(TAG, "Enable timer");
    ESP_ERROR_CHECK(gptimer_enable(gptimer));

    ESP_LOGI(TAG, "Start timer, auto-reload at alarm event");
    gptimer_alarm_config_t alarm_config2 = {
        .reload_count = 0,
        .alarm_count = 2000000, // period = 2s
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config2));
    ESP_ERROR_CHECK(gptimer_start(gptimer));
    ESP_LOGI(TAG, "Timer started");
    // int record = 4;
    // while (record) {
    //     if (xQueueReceive(queue, &ele, pdMS_TO_TICKS(2000))) {
    //         ESP_LOGI(TAG, "Timer reloaded, count=%llu", ele.event_count);
    //         record--;
    //     } else {
    //         ESP_LOGW(TAG, "Missed one count event");
    //     }
    // }

    const esp_timer_create_args_t periodic_timer_args = {
            .callback = &periodic_timer_callback,
            /* name is optional, but may help identify the timer when debugging */
            .name = "periodic"
    };

    esp_timer_handle_t periodic_timer;
    // ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    /* The timer has been created but is not running yet */

    /* start the timer */
    // ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 2000000));
/* 
    // Declare and configure the DHT11 sensor structure
    dht11_t dht11_sensor;
    dht11_sensor.dht11_pin = CONFIG_DHT11_PIN;

    // Initialize the SSD1306 OLED display
    //   init_ssd1306();

    // Buffers to hold temperature and humidity strings for display
    char temp_str[20];
    char humidity_str[25];

    // Infinite loop to continuously read and display data
    while (1) {
        // Static variables to store previous temperature and humidity
        static uint8_t prev_temp = 0xFF; // 0xFF indicates undefined initial state
        static uint8_t prev_hum  = 0xFF;

        // Read sensor values; returns 0 on success
        if (ele == WAIT) continue;
        if (ele == BURST) {
            ele =  WAIT;
            if (!dht11_read(&dht11_sensor, CONFIG_CONNECTION_TIMEOUT)) {
                // Cast sensor readings to 8-bit unsigned integers
                uint8_t curr_temp = (uint8_t)dht11_sensor.temperature;
                uint8_t curr_hum  = (uint8_t)dht11_sensor.humidity;

                // Update display only if values have changed
                if (curr_temp != prev_temp || curr_hum != prev_hum) {
                    // Clear previous content from OLED
                    // ssd1306_clear();

                    // Format temperature and humidity strings
                    sprintf(temp_str,     "Temp: %dC", curr_temp);
                    sprintf(humidity_str, "Humidity: %d%%", curr_hum);

                    printf("%s\n", temp_str);
                    printf("%s\n", humidity_str);

                    // Print strings on specific coordinates of the OLED
                    // ssd1306_print_str(25, 18, temp_str, false);     // Temperature at mid-screen
                    // ssd1306_print_str(8,  28, humidity_str, false); // Humidity below it

                    // Push buffer content to OLED
                    // ssd1306_display();
                    // sh1107_clear_screen(&dev, false);
                    // sh1107_direction(&dev, DIRECTION0);
                    // sh1107_contrast(&dev, 0xff);
                    // int center_line = 7;
                    sh1107_display_text(&dev, 5, 0, temp_str, strlen(temp_str), false);
                    sh1107_display_text(&dev, 6, 0, humidity_str, strlen(humidity_str), false);
                    sh1107_display_text(&dev, 8, 0, "GoodBye", 7, false);

                    // Store current values for future comparison
                    prev_temp = curr_temp;
                    prev_hum  = curr_hum;
                }
            }
        }

    // Delay between sensor reads to avoid over-sampling
    // vTaskDelay(100 / portTICK_PERIOD_MS); // 2 seconds
    }
*/

    // ESP_LOGI(TAG, "Stop timer");
    // ESP_ERROR_CHECK(gptimer_stop(gptimer));

    // ESP_LOGI(TAG, "Disable timer");
    // ESP_ERROR_CHECK(gptimer_disable(gptimer));

    // ESP_LOGI(TAG, "Delete timer");
    // ESP_ERROR_CHECK(gptimer_del_timer(gptimer));

    // ESP_ERROR_CHECK(esp_timer_stop(periodic_timer));
    // ESP_ERROR_CHECK(esp_timer_delete(periodic_timer));
    // ESP_LOGI(TAG, "Stopped and deleted SW timer");
}