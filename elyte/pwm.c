#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include "board.h"
#include "cli.h"
#include "gpio_driver.h"
#include "minio.h"
#include "pwm.h"
#include "stm32f3xx_ll_bus.h"
#include "stm32f3xx_ll_gpio.h"
#include "stm32f3xx_ll_rcc.h"
#include "stm32f3xx_ll_tim.h"

#define PWM_PERIOD_US_DEFAULT 20000U
#define PWM_PULSE_US_DEFAULT 0U
#define PWM_COUNTER_HZ 1000000U
#define PWM_MAX_COUNTS 0x10000U

extern void gpio_hal_stm32f3_af(uint16_t pin, uint8_t af);

static struct
{
    bool initialized;
    uint32_t timer_hz;
    uint32_t period_us;
    uint32_t pulse_us;
    uint32_t period_counts;
    uint32_t pulse_counts;
} me;

static uint32_t pwm_get_timer_clock_hz(void)
{
    LL_RCC_ClocksTypeDef clocks;
    LL_RCC_GetSystemClocksFreq(&clocks);

    uint32_t tim_clk = clocks.PCLK1_Frequency;
    if (LL_RCC_GetAPB1Prescaler() != LL_RCC_APB1_DIV_1)
    {
        tim_clk *= 2U;
    }
    return tim_clk;
}

static uint32_t pwm_us_to_counts(uint32_t us)
{
    uint64_t counts = ((uint64_t)us * me.timer_hz + 500000U) / 1000000U;
    if (counts > PWM_MAX_COUNTS)
    {
        return PWM_MAX_COUNTS;
    }
    return (uint32_t)counts;
}

static uint32_t pwm_arg_to_u32(const char *arg)
{
    long value = strtol(arg, NULL, 0);
    return value < 0 ? 0 : (uint32_t)value;
}

void pwm_set_us(uint32_t period_us, uint32_t pulse_us)
{
    if (!me.initialized)
    {
        pwm_init();
    }

    if (period_us == 0)
    {
        period_us = PWM_PERIOD_US_DEFAULT;
    }
    if (pulse_us > period_us)
    {
        pulse_us = period_us;
    }

    me.period_us = period_us;
    me.pulse_us = pulse_us;
    me.period_counts = pwm_us_to_counts(period_us);
    if (me.period_counts == 0)
    {
        me.period_counts = 1;
    }
    me.pulse_counts = pwm_us_to_counts(pulse_us);
    if (me.pulse_counts > me.period_counts)
    {
        me.pulse_counts = me.period_counts;
    }

    LL_TIM_SetAutoReload(TIM3, me.period_counts - 1U);
    LL_TIM_OC_SetCompareCH4(TIM3, me.pulse_counts);
    LL_TIM_GenerateEvent_UPDATE(TIM3);
}

uint32_t pwm_get_period_us(void)
{
    return me.period_us;
}

uint32_t pwm_get_pulse_us(void)
{
    return me.pulse_us;
}

void pwm_init(void)
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM3);
    LL_TIM_DisableCounter(TIM3);
    LL_TIM_CC_DisableChannel(TIM3, LL_TIM_CHANNEL_CH4);

    uint32_t prescaler_div = pwm_get_timer_clock_hz() / PWM_COUNTER_HZ;
    if (prescaler_div == 0)
    {
        prescaler_div = 1;
    }
    LL_TIM_SetPrescaler(TIM3, prescaler_div - 1U);
    me.timer_hz = pwm_get_timer_clock_hz() / prescaler_div;

    LL_TIM_SetClockDivision(TIM3, LL_TIM_CLOCKDIVISION_DIV1);
    LL_TIM_SetCounterMode(TIM3, LL_TIM_COUNTERMODE_UP);
    LL_TIM_SetUpdateSource(TIM3, LL_TIM_UPDATESOURCE_COUNTER);
    LL_TIM_EnableARRPreload(TIM3);
    LL_TIM_OC_SetMode(TIM3, LL_TIM_CHANNEL_CH4, LL_TIM_OCMODE_PWM1);
    LL_TIM_OC_SetPolarity(TIM3, LL_TIM_CHANNEL_CH4, LL_TIM_OCPOLARITY_HIGH);
    LL_TIM_OC_EnablePreload(TIM3, LL_TIM_CHANNEL_CH4);

    me.initialized = true;
    pwm_set_us(PWM_PERIOD_US_DEFAULT, PWM_PULSE_US_DEFAULT);

    gpio_config(PIN_TEMP_SW, GPIO_DIRECTION_FUNCTION_OUT, GPIO_PULL_NONE);
    gpio_hal_stm32f3_af(PIN_TEMP_SW, LL_GPIO_AF_2);

    LL_TIM_CC_EnableChannel(TIM3, LL_TIM_CHANNEL_CH4);
    LL_TIM_SetCounter(TIM3, 0);
    LL_TIM_EnableCounter(TIM3);
}

static int cli_pwm(int argc, const char **argv)
{
    if (!me.initialized)
    {
        pwm_init();
    }
    if (argc > 0)
    {
        uint32_t pulse_us = pwm_arg_to_u32(argv[0]);
        uint32_t period_us = argc > 1 ? pwm_arg_to_u32(argv[1]) : me.period_us;
        pwm_set_us(period_us, pulse_us);
    }

    printf("period:%d us pulse:%d us counts:%d/%d timer:%d Hz\n",
           (int)me.period_us,
           (int)me.pulse_us,
           (int)me.pulse_counts,
           (int)me.period_counts,
           (int)me.timer_hz);
    return 0;
}
CLI_FUNCTION(cli_pwm, "pwm", "<pulse_us> [period_us]: set TIM3_CH4 PWM on PIN_TEMP_SW");
