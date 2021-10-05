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


void lcd_test(void *p_param)
{
    lcd_init();
    ui_test();
    while(true) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}


void app_main()
{
    ESP_LOGI(TAG, "starting tasks");
    if (xTaskCreate(lcd_test, "lcd test", configMINIMAL_STACK_SIZE * 4, NULL, 10, NULL) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate() failed: lcd task");
    }
    if (xTaskCreate(ui_task, "ui_task", configMINIMAL_STACK_SIZE * 4, NULL, 7, NULL) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate() failed: ui task");
    }
}
