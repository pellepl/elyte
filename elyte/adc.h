#pragma once

#include <stdint.h>
#include "timer.h"

typedef enum {
    ADC_VOLTAGE,
    ADC_CURRENT,
    ADC_VDDA,
} adc_t;

typedef enum {
    GAIN_X1,
    GAIN_MIN = GAIN_X1,
    GAIN_X2,
    GAIN_X4,
    GAIN_X8,
    GAIN_X16,
    GAIN_MAX = GAIN_X16
} current_gain_t;

#define ADC_RAW_MAX_VAL ((1<<12) - 1)

typedef void (* adc_cb_t)(int res, adc_t adc, int32_t raw, float value);

void adc_init(void);
int32_t adc_read_voltage(adc_cb_t cb);
int32_t adc_read_current(adc_cb_t cb, current_gain_t gain);
int32_t adc_read_vdda(adc_cb_t cb);

int32_t adc_read_continuous(adc_cb_t cb, tick_t delta);
int32_t adc_stop_continuous(void);
void adc_adjust_gain_continuous(current_gain_t gain);
