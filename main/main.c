#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "esp_log.h"

#include "defines.h"
#include "types.h"
#include "lcd.h"
#include "ui_task.h"
#include "sensor_task.h"
#include "power.h"

const char* TAG = LOG_TAG;

void app_main()
{
    puts("OK");
    power_init();

    temperature_queue = xQueueCreate(1, sizeof(void *));
    if (!temperature_queue) {
        ESP_LOGE(TAG, "can't create temperature queue");
        vTaskDelay(pdMS_TO_TICKS(1000));
        abort();
    }

    if (xTaskCreate(ui_task, "ui_task", configMINIMAL_STACK_SIZE * 4, NULL, 10, NULL) != pdPASS) {
        ESP_LOGE(TAG, "can't create ui task");
        vTaskDelay(pdMS_TO_TICKS(1000));
        abort();
    }

    if (xTaskCreate(sensor_task, "sensor_task", configMINIMAL_STACK_SIZE * 4, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "can't create sensor task");
        vTaskDelay(pdMS_TO_TICKS(1000));
        abort();
    }

}
