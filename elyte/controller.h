#ifndef _CONTROLLER_H
#define _CONTROLLER_H

#include <stdbool.h>
#include "bmtypes.h"

#ifndef CTRL_MAX_CYCLES
#define CTRL_MAX_CYCLES 10
#endif

typedef enum
{
    CONTROL_OFF = 0,
    CONTROL_MANUAL,
    CONTROL_PROGRAM
} control_state_t;

typedef struct {
    control_state_t state;
    int current_temp;
    int target_temp;
    int power;
} status_event_info_t;

void ctrl_init(void);
void ctrl_panic(void);
bool ctrl_is_panicking(void);
bool ctrl_is_enabled(void);
void ctrl_start(void);
void ctrl_stop(void);
void ctrl_elements_off(void);
void ctrl_manual_set_temp(float temp);
void ctrl_manual_set_power(uint8_t power);
void ctrl_pid_set_epsilon(uint32_t div_by_10000);
void ctrl_pid_set_k_p(uint32_t div_by_10000);
void ctrl_pid_set_k_i(uint32_t div_by_10000);
void ctrl_pid_set_k_d(uint32_t div_by_10000);
uint32_t ctrl_get_enabled_time_s(void);
uint32_t ctrl_get_enable_timestamp_s(void);

#endif
