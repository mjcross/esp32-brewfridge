enum ui_mode_t {
    UI_MODE_SPLASH = 0,
    UI_MODE_SLEEP,
    UI_MODE_STATUS,
    UI_MODE_TEMP_ITEM,
    UI_MODE_TEMP_EDIT,
    UI_MODE_SENSOR_ITEM,
    UI_MODE_SENSOR_EDIT
};

void ui_test(void);
void ui_task(void *pParams);
