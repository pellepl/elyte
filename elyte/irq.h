#pragma once
#include <stdint.h>
#include _CORTEX_CORE_HEADER

static inline uint32_t cpu_primask_save_and_disable(void)
{
    uint32_t state = __get_PRIMASK();
    __disable_irq();
    return state;
}

static inline void cpu_primask_restore(uint32_t state)
{
    __set_PRIMASK(state);
}
