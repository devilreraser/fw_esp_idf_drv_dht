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
#define USE_GPIO_INTERRUPT  0
#else
#define USE_RMT             0
#define USE_GPIO            1
#define USE_GPIO_INTERRUPT  0
#endif


#if USE_GPIO
#include "dht.h"
#endif


#if USE_RMT
#undef USE_GPIO_INTERRUPT
#define USE_GPIO_INTERRUPT  0

#define USE_RMT_RECONFIGURE_EACH_TRANSMISSION   0
#endif


#if USE_RMT

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
#if USE_GPIO_INTERRUPT
#define COUNT_INTERRUPTS_PER_TRANSMISSION_MAX   100
#endif

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
static int64_t last_read_time = -2000000;
static struct drv_dht_reading last_read;

#if USE_GPIO_INTERRUPT
static bool b_interrupt_registered = false;
static int interrupts_count = 0;
static bool b_skip_dht_response_first_bit = false;
//static uint8_t pin_level[COUNT_INTERRUPTS_PER_TRANSMISSION_MAX] = {0};
static int delay_edge_change[COUNT_INTERRUPTS_PER_TRANSMISSION_MAX] = {0};
static int64_t last_interrupt_read_time = 0;
#endif

/* *****************************************************************************
 * Prototype of functions definitions
 **************************************************************************** */

/* *****************************************************************************
 * Functions
 **************************************************************************** */

// static int _waitOrTimeout(uint16_t microSeconds, int level) {
//     int micros_ticks = 0;
//     while(gpio_get_level(dht_gpio) == level) { 
//         if(micros_ticks++ > microSeconds) 
//             return DRV_DHT_TIMEOUT_ERROR;
//         ets_delay_us(1);
//     }
//     return micros_ticks;
// }

static int _checkCRC(uint8_t data[]) {
    if(data[4] == (uint8_t)(data[0] + data[1] + data[2] + data[3]))
        return DRV_DHT_OK;
    else
        return DRV_DHT_CRC_ERROR;
}

#if USE_GPIO_INTERRUPT
static void IRAM_ATTR dht11_gpio_isr_handler(void* arg) 
{
    int64_t curr_interrupt_read_time = esp_timer_get_time();
    // Handle interrupt here
    //ESP_LOGI(TAG, "%d", gpio_get_level(dht_gpio));
    //pin_level[interrupts_count] = gpio_get_level(dht_gpio);

    if (b_skip_dht_response_first_bit)
    {
        b_skip_dht_response_first_bit = false;
    }
    else
    {
        delay_edge_change[interrupts_count] = curr_interrupt_read_time - last_interrupt_read_time;
        interrupts_count++;
    }

    last_interrupt_read_time = curr_interrupt_read_time;
}
#endif



static void _sendStartSignal() {
    gpio_set_direction(dht_gpio, GPIO_MODE_INPUT_OUTPUT_OD);

    #if USE_GPIO_INTERRUPT
    if (b_interrupt_registered == false)
    {
        gpio_install_isr_service(ESP_INTR_FLAG_SHARED); // Choose an appropriate interrupt flag
        gpio_set_intr_type(dht_gpio, GPIO_INTR_NEGEDGE);
        gpio_isr_handler_add(dht_gpio, dht11_gpio_isr_handler, (void*) dht_gpio);
        b_interrupt_registered = true;
    }
    #endif


    #if USE_RMT
    #if CONFIG_DRV_RMT_USE
    #if USE_RMT_RECONFIGURE_EACH_TRANSMISSION
    drv_rmt_test_init_rx(); //test rmt pin and dht_gpio must match
    #else
    size_t len = drv_rmt_test_read_rx(NULL, 0, 0); 
    ESP_LOGD(TAG, "Read Buffer Before Transmission : %d ", len);
    #endif
    #else
    //to do local imlementation of rmt driver
    #endif
    #endif


    gpio_set_level(dht_gpio, 0);

    //vTaskDelay(pdMS_TO_TICKS(20));
    ets_delay_us(20 * 1000);

    #if USE_GPIO_INTERRUPT
    last_interrupt_read_time = esp_timer_get_time();

    interrupts_count = 0;
    b_skip_dht_response_first_bit = true;
    gpio_intr_enable(dht_gpio);
    #endif


    #if USE_RMT
    #if CONFIG_DRV_RMT_USE
    #if USE_RMT_RECONFIGURE_EACH_TRANSMISSION
    drv_rmt_test_start_rx();
    #else
    drv_rmt_test_start_rx();
    //drv_rmt_test_reset_rx();
    #endif
    #else
    //to do local imlementation of rmt driver
    #endif
    #endif

    gpio_set_level(dht_gpio, 1);

    //ets_delay_us(40);
}

#if USE_RMT == 0
static void _completed_transmission()
{
    #if USE_GPIO_INTERRUPT
    gpio_intr_disable(dht_gpio);
    //gpio_set_direction(dht_gpio, GPIO_MODE_INPUT);
    ESP_LOGD(TAG, "Count Interrupts %d", interrupts_count);
    //ESP_LOG_BUFFER_HEX(TAG, pin_level, interrupts_count);

    for (int index = 0; index < interrupts_count; index++)
    {
        ESP_LOGD(TAG, "[%2d]=%6d", index, delay_edge_change[index]);
    }
    #endif
}
#endif

// static int _checkResponse() {
//     /* Wait for next step ~80us*/
//     if(_waitOrTimeout(80, 0) == DRV_DHT_TIMEOUT_ERROR)
//         return DRV_DHT_TIMEOUT_ERROR;

//     /* Wait for next step ~80us*/
//     if(_waitOrTimeout(80, 1) == DRV_DHT_TIMEOUT_ERROR) 
//         return DRV_DHT_TIMEOUT_ERROR;

//     return DRV_DHT_OK;
// }

// static struct drv_dht_reading _timeoutError() {
//     struct drv_dht_reading timeoutError = {DRV_DHT_TIMEOUT_ERROR, -1, -1};
//     return timeoutError;
// }

// static struct drv_dht_reading _crcError() {
//     struct drv_dht_reading crcError = {DRV_DHT_CRC_ERROR, -1, -1};
//     return crcError;
// }

void drv_dht_init(gpio_num_t gpio_num) 
{
    esp_log_level_set(TAG, ESP_LOG_INFO);
    /* Wait 1 seconds to make the device pass its initial unstable status */
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    dht_gpio = gpio_num;


    #if USE_RMT
    #if CONFIG_DRV_RMT_USE
    #if USE_RMT_RECONFIGURE_EACH_TRANSMISSION
    #else
    drv_rmt_test_init_rx(); //test rmt pin and dht_gpio must match
    drv_rmt_test_start_rx();
    #endif
    #else
    //to do local imlementation of rmt driver
    #endif
    #endif


}

#if USE_GPIO
struct drv_dht_reading drv_dht_read() 
{
    int16_t humidity;
    int16_t temperature;
    esp_err_t err_read = dht_read_data(dht_sensor_type_t sensor_type, dht_gpio, &humidity, &temperature);
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

    // if(_checkResponse() == DRV_DHT_TIMEOUT_ERROR)
    //     return last_read = _timeoutError();
    
    // /* Read response */
    // for(int i = 0; i < 40; i++) {
    //     /* Initial data */
    //     if(_waitOrTimeout(50, 0) == DRV_DHT_TIMEOUT_ERROR)
    //         return last_read = _timeoutError();
                
    //     if(_waitOrTimeout(70, 1) > 28) {
    //         /* Bit received was a 1 */
    //         data[i/8] |= (1 << (7-(i%8)));
    //     }
    // }




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
    len = drv_rmt_test_read_rx(data, sizeof(data), delay); 
    //len = drv_rmt_test_read_rx(data, sizeof(data), portMAX_DELAY); //at least one rtos tick


    #if USE_RMT_RECONFIGURE_EACH_TRANSMISSION
    drv_rmt_test_stop_rx();
    drv_rmt_test_deinit_rx();
    #else
    drv_rmt_test_stop_rx();
    #endif

    if (len)
    {
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_DEBUG);
    }
    else
    {
        ESP_LOGE(TAG, "Timeout RMT");
    }





    // ets_delay_us(10000);
    // gpio_set_level(dht_gpio, 0);
    // ets_delay_us(1000);
    // gpio_set_level(dht_gpio, 1);

    

    #else
    //to do local imlementation of rmt driver
    #endif

    gpio_set_direction(dht_gpio, GPIO_MODE_INPUT);
    
    #endif


#if USE_GPIO_INTERRUPT
    if (interrupts_count == 40)
    {
        for (int index = 0; index < interrupts_count; index++)
        {
            if(delay_edge_change[index] >= 120)   //50+28 for 0 | 50+70 for 1
            {
                /* Bit received was a 1 */
                data[index/8] |= (1 << (7-(index%8)));
            }
        }
        ESP_LOG_BUFFER_HEX(TAG, data, 5);
    }
    else
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
