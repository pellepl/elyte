#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "timer.h"

#define INPUT_ROTARY_DIVISOR 1
#define INPUT_LONG_PRESS_SEC 3

typedef enum
{
    INPUT_BUTTON_ROTARY,
    INPUT_BUTTON_BACK,
    _INPUT_BUTTON_COUNT
} input_button_t;

#define SCROLL_ACCELERATOR_COOLDOWN TIMER_MS_TO_TICKS(300)
#define SCROLL_ACCELERATOR_ACC_TIME_LIMIT TIMER_MS_TO_TICKS(100)
#define SCROLL_ACCELERATOR_DEC_TIME_LIMIT TIMER_MS_TO_TICKS(200)
#define SCROLL_ACCELERATOR_MAX 32

void input_init(void);
bool input_is_button_pressed(input_button_t button);
int input_rotation_accelerator(void);
