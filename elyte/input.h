#pragma once

#include <stdbool.h>
#include <stdint.h>

#define INPUT_ROTARY_DIVISOR 2
#define INPUT_LONG_PRESS_SEC 3

typedef enum
{
    INPUT_BUTTON_ROTARY,
    INPUT_BUTTON_BACK,
    _INPUT_BUTTON_COUNT
} input_button_t;

void input_init(void);
bool input_is_button_pressed(input_button_t button);
void input_handle_rotary(void);
