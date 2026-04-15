#include <stddef.h>
#include "timer.h"
#include "minio.h"
#include "board.h"
#include "stm32f3xx_ll_bus.h"
#include "stm32f3xx_ll_exti.h"
#include "stm32f3xx_ll_gpio.h"
#include "stm32f3xx_ll_tim.h"
#include "stm32f3xx_ll_system.h"
#include "input.h"
#include "events.h"

#define CLICK_COOLDOWN_TICKS 1000

static struct
{
    uint32_t button_mask;
    uint32_t button_during_rotation_mask;
    tick_t button_release_tick[_INPUT_BUTTON_COUNT];
    event_t ev_hw_button;
    event_t ev_button;
    event_t ev_hw_scroll;
    event_t ev_scroll;
    int16_t rot_prev;
} me;

static void rotary_init(void)
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);

    // PA0 = TIM2_CH1, PA1 = TIM2_CH2
    // Input floating equivalent on F3: input mode + no pull
    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_0, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_0_7(GPIOA, LL_GPIO_PIN_0, LL_GPIO_AF_1);
    LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_0, LL_GPIO_PULL_UP);

    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_1, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_0_7(GPIOA, LL_GPIO_PIN_1, LL_GPIO_AF_1);
    LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_1, LL_GPIO_PULL_UP);

    // Configure TIM2 for encoder mode
    LL_TIM_SetEncoderMode(TIM2, LL_TIM_ENCODERMODE_X4_TI12);
    LL_TIM_IC_SetPolarity(TIM2, LL_TIM_CHANNEL_CH1, LL_TIM_IC_POLARITY_RISING);
    LL_TIM_IC_SetPolarity(TIM2, LL_TIM_CHANNEL_CH2, LL_TIM_IC_POLARITY_RISING);
    LL_TIM_IC_SetFilter(TIM2, LL_TIM_CHANNEL_CH1, LL_TIM_IC_FILTER_FDIV16_N8);
    LL_TIM_IC_SetFilter(TIM2, LL_TIM_CHANNEL_CH2, LL_TIM_IC_FILTER_FDIV16_N8);

    // 16-bit counter range
    LL_TIM_SetAutoReload(TIM2, 0xFFFF);
    LL_TIM_SetCounter(TIM2, 0);

    // Enable input capture interrupts
    LL_TIM_EnableIT_CC1(TIM2);
    LL_TIM_EnableIT_CC2(TIM2);

    NVIC_SetPriority(TIM2_IRQn, 2);
    NVIC_EnableIRQ(TIM2_IRQn);

    LL_TIM_EnableCounter(TIM2);

    // rotary pins
    LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTA, LL_SYSCFG_EXTI_LINE0);
    LL_EXTI_EnableIT_0_31(LL_EXTI_LINE_0);
    LL_EXTI_EnableRisingTrig_0_31(LL_EXTI_LINE_0);
    LL_EXTI_EnableFallingTrig_0_31(LL_EXTI_LINE_0);
    LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTA, LL_SYSCFG_EXTI_LINE1);
    LL_EXTI_EnableIT_0_31(LL_EXTI_LINE_1);
    LL_EXTI_EnableRisingTrig_0_31(LL_EXTI_LINE_1);
    LL_EXTI_EnableFallingTrig_0_31(LL_EXTI_LINE_1);

    NVIC_SetPriority(EXTI0_IRQn, 2);
    NVIC_EnableIRQ(EXTI0_IRQn);
    NVIC_SetPriority(EXTI1_IRQn, 2);
    NVIC_EnableIRQ(EXTI1_IRQn);
}

static void buttons_init(void)
{
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);

    // PA2 = rotary push button
    // Keep same active-low behavior as old code:
    // released -> pin reads high
    // pressed  -> pin reads low
    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_2, LL_GPIO_MODE_INPUT);
    LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_2, LL_GPIO_PULL_UP);
    // Route EXTI2 to PA2
    LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTA, LL_SYSCFG_EXTI_LINE2);
    LL_EXTI_EnableIT_0_31(LL_EXTI_LINE_2);
    LL_EXTI_EnableRisingTrig_0_31(LL_EXTI_LINE_2);
    LL_EXTI_EnableFallingTrig_0_31(LL_EXTI_LINE_2);

    NVIC_SetPriority(EXTI2_TSC_IRQn, 2);
    NVIC_EnableIRQ(EXTI2_TSC_IRQn);

    // PC8 = board button, active low with pull-up on PCB
    LL_GPIO_SetPinMode(GPIOC, LL_GPIO_PIN_8, LL_GPIO_MODE_INPUT);
    LL_GPIO_SetPinPull(GPIOC, LL_GPIO_PIN_8, LL_GPIO_PULL_NO);
    // Route EXTI8 to PC8
    LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTC, LL_SYSCFG_EXTI_LINE8);
    LL_EXTI_EnableIT_0_31(LL_EXTI_LINE_8);
    LL_EXTI_EnableRisingTrig_0_31(LL_EXTI_LINE_8);
    LL_EXTI_EnableFallingTrig_0_31(LL_EXTI_LINE_8);

    NVIC_SetPriority(EXTI9_5_IRQn, 2);
    NVIC_EnableIRQ(EXTI9_5_IRQn);
}

static int16_t input_rot_read(void)
{
    return (int16_t)LL_TIM_GetCounter(TIM2);
}

void input_init(void)
{
    rotary_init();
    buttons_init();
    me.rot_prev = input_rot_read();
}

static void scroll_event(uint32_t type, void *arg)
{
    int16_t rot = input_rot_read();
    if (me.rot_prev != rot)
    {
        me.button_during_rotation_mask |= me.button_mask;
        event_add(&me.ev_scroll, EVENT_UI_SCRL, (void *)(int)(me.rot_prev - rot));
    }
    me.rot_prev = rot;
}

bool input_is_button_pressed(input_button_t button)
{
    return (me.button_mask & (1 << button)) != 0;
}

static void button_event(uint32_t type, void *arg)
{
    switch (type)
    {
    case EVENT_BUTTON_PRESS:
    {
        input_button_t but = (input_button_t)arg;
        if (but >= _INPUT_BUTTON_COUNT)
            break;
        me.button_mask |= 1 << but;
        break;
    }

    case EVENT_BUTTON_RELEASE:
    {
        input_button_t but = (input_button_t)arg;
        if (but >= _INPUT_BUTTON_COUNT)
            break;

        uint32_t butmask = 1 << but;
        tick_t now = timer_now();

        if (((me.button_mask & butmask) != 0) &&
            ((me.button_during_rotation_mask & butmask) == 0))
        {
            if (now - me.button_release_tick[but] > CLICK_COOLDOWN_TICKS)
            {
                event_add(&me.ev_button, EVENT_UI_CLICK, (void *)but);
            }
        }

        me.button_release_tick[but] = now;
        me.button_mask &= ~butmask;
        me.button_during_rotation_mask &= ~butmask;
        break;
    }
    default:
        break;
    }
}

static void event_handler(uint32_t type, void *arg)
{

    switch (type)
    {
    case EVENT_SECOND_TICK:
    {
        if (me.button_mask == 0)
            break;
        uint32_t now_s = (uint32_t)arg;
        // TODO PETER longpress

        break;
    }
    default:
        break;
    }
}
EVENT_HANDLER(event_handler);

void TIM2_IRQHandler(void);
void TIM2_IRQHandler(void)
{
    // Rotary interrupt
    if (LL_TIM_IsActiveFlag_CC1(TIM2))
    {
        LL_TIM_ClearFlag_CC1(TIM2);
    }

    if (LL_TIM_IsActiveFlag_CC2(TIM2))
    {
        LL_TIM_ClearFlag_CC2(TIM2);
    }
}

void EXTI0_IRQHandler(void);
void EXTI0_IRQHandler(void)
{
    if (LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_0))
    {
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_0);
        event_add_specific(&me.ev_hw_scroll, EVENT_UI_SCRL, NULL, scroll_event);
    }
}
void EXTI1_IRQHandler(void);
void EXTI1_IRQHandler(void)
{
    if (LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_1))
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_1);
}

void EXTI2_TSC_IRQHandler(void);
void EXTI2_TSC_IRQHandler(void)
{
    if (LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_2))
    {
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_2);

        if (LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_2))
        {
            event_add_specific(&me.ev_hw_button, EVENT_BUTTON_RELEASE, (void *)INPUT_BUTTON_ROTARY, button_event);
        }
        else
        {
            event_add_specific(&me.ev_hw_button, EVENT_BUTTON_PRESS, (void *)INPUT_BUTTON_ROTARY, button_event);
        }
    }
}

void EXTI9_5_IRQHandler(void);
void EXTI9_5_IRQHandler(void)
{
    if (LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_8))
    {
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_8);

        if (LL_GPIO_IsInputPinSet(GPIOC, LL_GPIO_PIN_8))
        {
            event_add_specific(&me.ev_hw_button, EVENT_BUTTON_RELEASE, (void *)INPUT_BUTTON_ROTARY, button_event);
        }
        else
        {
            event_add_specific(&me.ev_hw_button, EVENT_BUTTON_PRESS, (void *)INPUT_BUTTON_ROTARY, button_event);
        }
    }
}
