#pragma once

#include <stdint.h>

void pwm_init(void);
void pwm_set_us(uint32_t period_us, uint32_t pulse_us);
uint32_t pwm_get_period_us(void);
uint32_t pwm_get_pulse_us(void);
