#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "events.h"
#include "controller.h"
#include "timer.h"
#include "minio.h"
#include "board.h"
#include "gpio_driver.h"
#include "gpio_driver.h"
#include "assert.h"

typedef struct
{
    float set, act, output, output_max, output_min;
    float error_epsilon;
    float error_prev;
    float integral;
    float k_p, k_i, k_d;
} pid_t;

static struct
{
    volatile bool panic;
    volatile control_state_t state;
    float kiln_temp_manual;
    uint32_t now;
    uint32_t kiln_start_time;
    volatile uint32_t kiln_full_power_time;
    uint8_t kiln_power_manual;
    uint8_t kiln_cycle_ix;
    uint8_t kiln_power_active_cycles;
    timer_t kiln_timer;
    pid_t pid;
    uint64_t on_timestamp;
} me;

static event_t ev_panic;

void ctrl_start(void)
{
}

void ctrl_stop(void)
{
}

void ctrl_manual_set_temp(float temp)
{
}

void ctrl_manual_set_power(uint8_t power)
{
}

void ctrl_pid_set_epsilon(uint32_t div_by_10000)
{
}
void ctrl_pid_set_k_p(uint32_t div_by_10000)
{
}
void ctrl_pid_set_k_i(uint32_t div_by_10000)
{
}
void ctrl_pid_set_k_d(uint32_t div_by_10000)
{
}
void ctrl_elements_off(void)
{
}

void ctrl_init(void)
{
}

void ctrl_panic(void)
{
    if (!me.panic)
    {
        ctrl_elements_off();
        me.panic = true;
        event_add(&ev_panic, EVENT_ATTENTION, NULL);
    }
}

bool ctrl_is_panicking(void)
{
    return 0;
}

bool ctrl_is_enabled(void)
{
    return 0;
}

uint32_t ctrl_get_enabled_time_s(void)
{
    return 0;
}

uint32_t ctrl_get_enable_timestamp_s(void)
{
    return 0;
}

static void ctrl_event_handler(uint32_t type, void *arg)
{
    switch (type)
    {
    case EVENT_SECOND_TICK:
        break;
    default:
        break;
    }
}
EVENT_HANDLER(ctrl_event_handler);
