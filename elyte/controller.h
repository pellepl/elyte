#pragma once

#include <stdbool.h>
#include "bmtypes.h"


typedef struct {
    uint16_t dac;
    float current_cap;
    float voltage_cap;
    float current;
    float voltage;
} status_event_info_t;

void ctrl_init(void);
void ctrl_panic(void);
bool ctrl_is_panicking(void);
bool ctrl_is_enabled(void);
void ctrl_start(void);
void ctrl_stop(void);
void ctrl_set_dac(uint16_t dac);
