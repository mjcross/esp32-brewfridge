#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <ds18x20.h>
#include "esp_log.h"

#include "defines.h"
#include "globals.h"
#include "types.h"

QueueHandle_t temperature_queue;

void sensor_task(void *pParams) {
    // create double buffers on heap
    //
    struct temp_data_t *pTempData_A = malloc(sizeof(struct temp_data_t));
    struct temp_data_t *pTempData_B = malloc(sizeof(struct temp_data_t));

    if (pTempData_A == NULL || pTempData_B == NULL) {
        ESP_LOGE(TAG, "can't malloc temp double buffer");
        vTaskDelay(pdMS_TO_TICKS(1000));
        abort();
    }

    struct temp_data_t *pBuf = pTempData_A;

    // continuously scan bus and send readings to queue
    //
    for(;;) {

        // scan bus for sensors
        //
        if (ds18x20_scan_devices(ONEWIRE_GPIO, pBuf->addr, MAX_TEMP_SENSORS, &(pBuf->num_sensors)) == ESP_OK) {
            if (pBuf->num_sensors > MAX_TEMP_SENSORS) {
                pBuf->num_sensors = MAX_TEMP_SENSORS;
            }
            // read sensors
            //
            if (ds18x20_measure_and_read_multi(ONEWIRE_GPIO,
                                               pBuf->addr,
                                               pBuf->num_sensors,
                                               pBuf->temp) != ESP_OK) {
                // couldn't read sensors
                //
                pBuf->num_sensors = 0;
                vTaskDelay(pdMS_TO_TICKS(750));
            }

        } else {
            // couldn't scan bus
            //
            pBuf->num_sensors = 0;
            vTaskDelay(pdMS_TO_TICKS(750));
        }

        // send buffer pointer to queue
        //
        if (xQueueSend(temperature_queue, (void *)&pBuf, 0) == pdTRUE) {
            // successful send - flip buffers
            //
            if (pBuf == pTempData_A) {
                pBuf = pTempData_B;
            } else {
                pBuf = pTempData_A;
            }
        } else {
            ESP_LOGW(TAG, "temp data queue full");
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

