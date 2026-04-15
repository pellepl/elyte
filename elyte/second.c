#include <stdint.h>
#include _CORTEX_CORE_HEADER
#include "board.h"
#include "cpu.h"
#include "event.h"
#include "events.h"
#include "gpio_driver.h"
#include "second.h"
#include "timer.h"

static struct
{
    event_t ev_second;
    volatile uint32_t uptime_seconds;
} me;

void second_init(void)
{
    uint32_t systick_hz = cpu_core_clock_freq() / 8u;

    me.uptime_seconds = 0;
    SysTick->CTRL = 0;
    SysTick->LOAD = systick_hz - 1u;
    SysTick->VAL = 0u;
    SysTick->CTRL = SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}

void SysTick_Handler(void)
{
    me.uptime_seconds++;
    event_add(&me.ev_second, EVENT_SECOND_TICK, (void *)(uintptr_t)(uint32_t)(timer_uptime_ms()/1000));
}

static void event_handler(uint32_t type, void *arg)
{
    if (type == EVENT_SECOND_TICK)
    {
        gpio_set(PIN_LED_G, 0);
        timer_halt_ms(1);
        gpio_set(PIN_LED_G, 1);
    }
};
EVENT_HANDLER(event_handler);