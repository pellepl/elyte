#pragma once

#include <stdint.h>

typedef enum {
    ADC_VOLTAGE,
    ADC_CURRENT,
    ADC_VDDA,
} adc_t;

typedef enum {
    GAIN_X1,
    GAIN_X2,
    GAIN_X4,
    GAIN_X8,
    GAIN_X16,
} current_gain_t;

typedef void (* adc_cb_t)(adc_t adc, int32_t value);

void adc_init(void);
int32_t adc_read_voltage(adc_cb_t cb);
int32_t adc_read_current(adc_cb_t cb, current_gain_t gain);
int32_t adc_read_vdda(adc_cb_t cb);
float adc_convert_to_volt(adc_t adc, int32_t value);
