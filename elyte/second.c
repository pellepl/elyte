#include <stdint.h>
#include "cpu.h"
#include _CORTEX_CORE_HEADER
#include "event.h"
#include "events.h"
#include "second.h"

static struct
{
    event_t ev_second;
    volatile uint32_t uptime_seconds;
} me;

static void delay_cycles(uint32_t cycles)
{
    uint32_t end = DWT->CYCCNT + cycles;
    while ((int32_t)(DWT->CYCCNT - end) < 0)
        ;
}

static void dwt_init(void)
{
    static int dwt_initialized = 0;

    if (dwt_initialized)
        return;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0)
    {
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
    dwt_initialized = 1;
}

void cpu_halt(uint32_t ms)
{
    const uint32_t cycles_per_ms = cpu_core_clock_freq() / 1000u;

    dwt_init();
    while (ms--)
    {
        delay_cycles(cycles_per_ms);
    }
}

void cpu_halt_us(uint32_t us)
{
    const uint32_t cycles_per_us = cpu_core_clock_freq() / 1000000u;
    uint64_t total_cycles = (uint64_t)us * (uint64_t)cycles_per_us;

    if (cycles_per_us == 0 || us == 0)
        return;

    dwt_init();
    while (total_cycles)
    {
        uint32_t chunk = total_cycles > 0x7FFFFFFFULL ? 0x7FFFFFFFu : (uint32_t)total_cycles;
        delay_cycles(chunk);
        total_cycles -= chunk;
    }
}

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
    event_add(&me.ev_second, EVENT_SECOND_TICK, (void *)(uintptr_t)me.uptime_seconds);
}
