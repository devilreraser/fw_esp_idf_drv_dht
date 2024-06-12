/* *****************************************************************************
 * File:   drv_dht.c
 * Author: XX
 *
 * Created on YYYY MM DD
 * 
 * Description: ...
 * 
 **************************************************************************** */

/* *****************************************************************************
 * Header Includes
 **************************************************************************** */
#include "drv_dht.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"



/* *****************************************************************************
 * Configuration Definitions
 **************************************************************************** */
#define TAG "drv_dht"

/* priority high to low */
#if CONFIG_DRV_DHT_RMT_MODE
#define USE_RMT             1
#define USE_GPIO            0
#else
#define USE_RMT             0
#define USE_GPIO            1
#endif


#if USE_GPIO
#include "dht.h"
#endif


#if USE_RMT

#define USE_RMT_RECONFIGURE_EACH_TRANSMISSION   0

#if CONFIG_DRV_RMT_USE

#include "drv_rmt.h"

#else

#define FORCE_LEGACY_FOR_RMT_ESP_IDF_VERSION_5    1
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0) && FORCE_LEGACY_FOR_RMT_ESP_IDF_VERSION_5 == 0
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#else
//#pragma GCC diagnostic ignored "-Wcpp"
#include "driver/rmt.h"
//#pragma GCC diagnostic pop
#endif

#endif

#endif

/* *****************************************************************************
 * Constants and Macros Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Enumeration Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Type Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Function-Like Macros
 **************************************************************************** */

/* *****************************************************************************
 * Variables Definitions
 **************************************************************************** */
static gpio_num_t dht_gpio;
static struct drv_dht_reading last_read;
static int64_t last_read_time = -2000000;

/* *****************************************************************************
 * Prototype of functions definitions
 **************************************************************************** */

/* *****************************************************************************
 * Functions
 **************************************************************************** */

#if USE_RMT

static int _checkCRC(uint8_t data[]) {
    if(data[4] == (uint8_t)(data[0] + data[1] + data[2] + data[3]))
        return DRV_DHT_OK;
    else
        return DRV_DHT_CRC_ERROR;
}

static void _sendStartSignal() {
    gpio_set_level(dht_gpio, 1);
    gpio_set_direction(dht_gpio, GPIO_MODE_INPUT_OUTPUT_OD);

    #if CONFIG_DRV_RMT_USE
    #if USE_RMT_RECONFIGURE_EACH_TRANSMISSION
    drv_rmt_init_rx(); //test rmt pin and dht_gpio must match
    #else
    ESP_LOGD(TAG, "Read Buffer Before Transmission Start");
    size_t len = drv_rmt_read_rx(NULL, 0, 0); 
    ESP_LOGD(TAG, "Read Buffer Before Transmission %d bytes", len);
    #endif
    #endif

    gpio_set_level(dht_gpio, 0);
    
    #if CONFIG_DRV_RMT_USE
    drv_rmt_start_rx();
    #endif

    ets_delay_us(20 * 1000);

    gpio_set_level(dht_gpio, 1);

}

#endif

void drv_dht_init(gpio_num_t gpio_num) 
{
    #if USE_GPIO
    esp_log_level_set("dht", ESP_LOG_INFO);
    #endif
    esp_log_level_set(TAG, ESP_LOG_INFO);
    /* Wait 1 seconds to make the device pass its initial unstable status */
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    dht_gpio = gpio_num;


    #if USE_RMT
    #if CONFIG_DRV_RMT_USE
    #if USE_RMT_RECONFIGURE_EACH_TRANSMISSION
    #else
    drv_rmt_init_rx();
    #endif
    #endif
    #endif


}

#if USE_GPIO
struct drv_dht_reading drv_dht_read() 
{
    int16_t humidity;
    int16_t temperature;
    esp_err_t err_read = dht_read_data(DHT_TYPE_DHT11, dht_gpio, &humidity, &temperature);
    last_read.temperature = temperature / 10;
    last_read.humidity = humidity / 10;
    last_read.status = (err_read == ESP_OK) ? DRV_DHT_OK : DRV_DHT_TIMEOUT_ERROR;
    return last_read;
}

#else

struct drv_dht_reading drv_dht_read() 
{
    /* Tried to sense too soon since last read (dht11 needs ~2 seconds to make a new read) */
    if(esp_timer_get_time() - 2000000 < last_read_time) {
        return last_read;
    }

    last_read_time = esp_timer_get_time();

    uint8_t data[5] = {0,0,0,0,0};

    _sendStartSignal();


    #if USE_RMT == 0
    ets_delay_us(5 * 1000 + 1000); //at least > 4.8 ms (120*40 + 180)
    _completed_transmission();
    gpio_set_direction(dht_gpio, GPIO_MODE_INPUT);
    #endif


    #if USE_RMT
    #if CONFIG_DRV_RMT_USE
    size_t len = 0;
    #define pdMS_TO_TICKS_FIX(x) (pdMS_TO_TICKS(x) > 0) ? pdMS_TO_TICKS(x) : 1  //at least one rtos tick
    TickType_t delay = pdMS_TO_TICKS_FIX(80);  //at least one rtos tick
    ESP_LOGD(TAG, "Read Buffer After Transmission Start");
    len = drv_rmt_read_rx(data, sizeof(data), delay); 
    ESP_LOGD(TAG, "Read Buffer After Transmission %d bytes", len);
    drv_rmt_stop_rx();

    #if USE_RMT_RECONFIGURE_EACH_TRANSMISSION
    drv_rmt_deinit_rx();
    #endif

    if (len)
    {
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_DEBUG);
    }
    else
    {
        ESP_LOGE(TAG, "Timeout RMT");
    }


    #endif

    gpio_set_direction(dht_gpio, GPIO_MODE_INPUT);
    
    #endif

#if USE_RMT
    if (len == 5)
    {

    }
    else
#endif
    {
        //last_read = _timeoutError();
        last_read.status = DRV_DHT_TIMEOUT_ERROR;
        return last_read;
    }
    

    if(_checkCRC(data) != DRV_DHT_CRC_ERROR) {
        last_read.status = DRV_DHT_OK;
        last_read.temperature = data[2];
        last_read.humidity = data[0];
        return last_read;
    } else {
        //last_read = _crcError();
        last_read.status = DRV_DHT_CRC_ERROR;
        //last_read.temperature = data[2];
        //last_read.humidity = data[0];
        return last_read;
    }
}

#endif
