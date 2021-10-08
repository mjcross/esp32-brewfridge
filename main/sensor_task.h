#include <freertos/queue.h>
#include <ds18x20.h>

extern QueueHandle_t temperature_queue;

void sensor_task (void *pParams);
