#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    // the first settins up to SETTING_COUNT is shown in ui_view_settings list
    SETTING_SCREEN_ALIVE_S,
    SETTING_CURR_CYCLE_LIMIT_MV,
    SETTING_CURR_CYCLE_PERIOD_S,
    SETTING_CURR_CYCLE_DUTY_S,
    SETTING_SERVO_DELTA_S,
    SETTING_SHORT_MV_AT_10_MA,
    SETTING_COUNT,
    SETTING_PRIVATE_PROFILE_RESISTANCE = SETTING_COUNT,
    SETTING_PRIVATE_PROFILE_VOLTAGE,
    SETTING_COUNT_ALL
} setting_id_t;

typedef struct {
    setting_id_t id;
    int32_t min;
    int32_t max;
    int32_t def;
    const char *name;
    const char *descr;
    const char *unit;
    uint16_t tag;
    int8_t e;
} setting_def_t;

typedef struct {
    int32_t value;
    const setting_def_t *def;
} setting_t;

int setting_set(setting_id_t id, int val);
setting_t *setting_get(setting_id_t id, setting_t *s);
float setting_get_val(setting_id_t id);
void settings_init(void);
