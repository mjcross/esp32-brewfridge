#include <stdio.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "flash.h"

#define NVS_NAMESPACE "brewfridge"
#define NVS_KEYBASE "sensor_addr_"  // ... plus the sensor index

static nvs_handle_t handle;
static bool nvs_is_open = false;


/// @brief Initialises the non-volatile storage and opens our namespace for R/W.
static void initialise() {
    // initialise NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    // open NVS for read + write
    if (nvs_open (NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        printf ("Error (%s) opening NVS namespace %s\n", esp_err_to_name(err), NVS_NAMESPACE);
    } else {
        nvs_is_open = true;
    }
}


/// @brief Initialises the sensor addresses from non-volatile storage.
/// @param sensors the array of sensor data 
/// @param num_sensors the number of sensors to be initialised 
void read_sensor_addresses (struct sensor_field_t *sensors, int num_sensors) {
    char key_name[NVS_KEY_NAME_MAX_SIZE];
    esp_err_t err;

    if (nvs_is_open == false) {     // initialise the library and if necessary, the partition
        initialise();
    }

    for (int sensor_index = 0; sensor_index < num_sensors; sensor_index += 1) {
        // create the key name for the sensor
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wformat-truncation"    // ignore the most brain-dead warning ever
        snprintf(key_name, NVS_KEY_NAME_MAX_SIZE, "%s%d", NVS_KEYBASE, sensor_index);
        #pragma GCC diagnostic pop

        // read the sensor address (if set)
        err = nvs_get_u64 (handle, key_name, &(sensors[sensor_index].addr));
        if (err == ESP_OK) {
            printf ("NVS: %s/%s = 0x%016llx\n", NVS_NAMESPACE, key_name, sensors[sensor_index].addr);
        } else if (err == ESP_ERR_NVS_NOT_FOUND) {
                printf ("NVS: %s/%s not yet initialised\n", NVS_NAMESPACE, key_name);
        } else {
            printf ("NVS: error (%s) reading %s/%s", esp_err_to_name(err), NVS_NAMESPACE, key_name);
        }
    }
}


/// @brief Writes the sensor addresses to non-volatile storage.
/// @param sensors the array of sensors
/// @param num_sensors the number of sensors to be written
void write_sensor_addresses (struct sensor_field_t *sensors, int num_sensors) {
    char key_name[NVS_KEY_NAME_MAX_SIZE];
    esp_err_t err;
    if (nvs_is_open == false) {     // initialise the library and if necessary, the partition
        initialise();
    }

    // update the address of each sensor
    for (int sensor_index = 0; sensor_index < num_sensors; sensor_index += 1) {
        // create the key name for the sensor
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wformat-truncation"    // ignore the most brain-dead warning ever
        snprintf(key_name, NVS_KEY_NAME_MAX_SIZE, "%s%d", NVS_KEYBASE, sensor_index);
        #pragma GCC diagnostic pop
        
        // write the sensor address
        err = nvs_set_u64 (handle, key_name, sensors[sensor_index].addr);
        if (err != ESP_OK) {
            printf ("NVS: error (%s) setting %s/%s\n", esp_err_to_name(err), NVS_NAMESPACE, key_name);
        }
    }

    // commit the writes
    err = nvs_commit (handle);
    if (err == ESP_OK) {
        puts ("NVS: saved sensor addresses");
    } else {
        printf ("NVS: error (%s) saving sensor addresses\n", esp_err_to_name(err));
    }
}
