// temperature sensors
//
#define LOG_TAG                 "brewfridge"

#define ONEWIRE_GPIO            17
#define MAX_TEMP_SENSORS        12        // UI can show three rows of four

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
#define UI_BLINK_MS             250      // timeout on temp setting mode
#define UI_BLINKS_PER_FLASH     4
#define UI_BLINKS_PER_TIMEOUT   40
#define UI_BLINKS_PER_SLEEP     400


// power control
//
#define F1_RELAY_GPIO           32
#define F2_RELAY_GPIO           33
#define RELAY_INTERVAL_MS       30000     // minimum cooling on/off time
#define INITIAL_SET_TEMP        23.0      // initial target temp
