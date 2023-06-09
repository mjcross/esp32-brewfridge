// temperature sensors
//
#define LOG_TAG                 "brewfridge"

#define ONEWIRE_GPIO            17
#define MAX_TEMP_SENSORS        12      // LCD can show three rows of four

// LCD display
//
#define SDA_GPIO                16
#define SCL_GPIO                4
#define I2C_ADDR                0x27

// rotary encoder
//
#define RE_A_GPIO               26
#define RE_B_GPIO               25
#define RE_BTN_GPIO             27
#define RE_EVENT_QUEUE_SIZE     10

// UI
//
#define UI_BLINK_MS             250     // timeout on temp setting mode
#define UI_BLINKS_PER_FLASH     4
#define UI_BLINKS_PER_TIMEOUT   30
#define UI_BLINKS_PER_SLEEP     400


// power control
//
#define F1_RELAY_GPIO           32
#define F2_RELAY_GPIO           33
#define F1_SSR_GPIO             18
#define F2_SSR_GPIO             19


// power control timeouts in ms
#define MIN_OFF_TIME            (2 * 60 * 1000) / portTICK_PERIOD_MS        // 2 mins recovery time after heating/cooling
#define MIN_COOLING_TIME        (30 * 1000)  / portTICK_PERIOD_MS           // keep fridge on for at least 30 sec
#define MAX_COOLING_TIME        (60 * 60 * 1000)  / portTICK_PERIOD_MS      // run fridge for max 1hr at a time
