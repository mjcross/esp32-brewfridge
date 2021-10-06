#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "esp_log.h"

#include "globals.h"
#include "lcd.h"
#include "defines.h"
#include "ui_task.h"

const char* TAG = LOG_TAG;

void app_main()
{
    if (xTaskCreate(ui_task, "ui_task", configMINIMAL_STACK_SIZE * 4, NULL, 7, NULL) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate() failed: ui task");
    }
}
