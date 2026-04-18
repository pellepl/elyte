#include <stdbool.h>
#include <stddef.h>
#include "adc.h"
#include "board.h"
#include "cli.h"
#include "gpio_driver.h"
#include "minio.h"
#include "stm32f3xx_ll_adc.h"
#include "stm32f3xx_ll_bus.h"
#include "stm32f3xx_ll_opamp.h"
#include "utils.h"

#define DEFAULT_VDDA 3.3f
#define CURR_SENS_R 3.333f
#define VOLT_SENS_DIV 2.f

static struct
{
    volatile bool busy;
    adc_t active;
    adc_cb_t cb;
    current_gain_t opamp_gain;
    float gain_factor;
    float vdda;

    struct
    {
        volatile bool active;
        volatile bool stop;
        timer_t timer;
        volatile current_gain_t gain_set;
        adc_t adc;
        adc_cb_t cb;
    } cont;
} me;

static void adc_delay_cycles(uint32_t cycles)
{
    while (cycles-- > 0)
    {
        __asm__ volatile("nop");
    }
}

static void adc_delay_us(uint32_t us)
{
    const uint32_t cycles_per_us = SystemCoreClock / 1000000;
    adc_delay_cycles((cycles_per_us * us) / 4 + 1);
}

static void adc_disable_wait(void)
{
    if (LL_ADC_IsEnabled(ADC2))
    {
        LL_ADC_Disable(ADC2);
        while (LL_ADC_IsEnabled(ADC2) || LL_ADC_IsDisableOngoing(ADC2))
            ;
    }
}

static void adc_enable_wait(void)
{
    LL_ADC_ClearFlag_ADRDY(ADC2);
    LL_ADC_Enable(ADC2);
    while (!LL_ADC_IsActiveFlag_ADRDY(ADC2))
        ;
}

static void adc_prepare_single_conversion(uint32_t channel, uint32_t single_diff, uint32_t sample_time)
{
    adc_disable_wait();
    LL_ADC_SetChannelSingleDiff(ADC2, channel, single_diff);
    LL_ADC_SetChannelSamplingTime(ADC2, channel, sample_time);
    LL_ADC_REG_SetSequencerLength(ADC2, LL_ADC_REG_SEQ_SCAN_DISABLE);
    LL_ADC_REG_SetSequencerRanks(ADC2, LL_ADC_REG_RANK_1, channel);
    adc_enable_wait();
}

_Static_assert(PIN_CURR_H == PORTB(0), "OPAMP2 non-inv input misconfigured (LL_OPAMP_INPUT_NONINVERT_IO2)");

static void opamp2_configure(current_gain_t gain)
{
    me.opamp_gain = gain;
    LL_OPAMP_Disable(OPAMP2);
    LL_OPAMP_SetTrimmingMode(OPAMP2, LL_OPAMP_TRIMMING_FACTORY);
    LL_OPAMP_SetInputsMuxMode(OPAMP2, LL_OPAMP_INPUT_MUX_DISABLE);
    LL_OPAMP_SetInputNonInverting(OPAMP2, LL_OPAMP_INPUT_NONINVERT_IO2);

    if (gain == GAIN_X1)
    {
        me.gain_factor = 1.0f;
        LL_OPAMP_SetFunctionalMode(OPAMP2, LL_OPAMP_MODE_FOLLOWER);
    }
    else
    {
        uint32_t pga_gain;
        switch (gain)
        {
        case GAIN_X2:
            pga_gain = LL_OPAMP_PGA_GAIN_2;
            me.gain_factor = 2.f;
            break;
        case GAIN_X4:
            pga_gain = LL_OPAMP_PGA_GAIN_4;
            me.gain_factor = 4.f;
            break;
        case GAIN_X8:
            pga_gain = LL_OPAMP_PGA_GAIN_8;
            me.gain_factor = 8.f;
            break;
        case GAIN_X16:
        default:
            pga_gain = LL_OPAMP_PGA_GAIN_16;
            me.gain_factor = 16.f;
            break;
        }
        LL_OPAMP_SetFunctionalMode(OPAMP2, LL_OPAMP_MODE_PGA);
        LL_OPAMP_SetInputInverting(OPAMP2, LL_OPAMP_INPUT_INVERT_CONNECT_NO);
        LL_OPAMP_SetPGAGain(OPAMP2, pga_gain);
    }

    LL_OPAMP_Enable(OPAMP2);
    adc_delay_us(LL_OPAMP_DELAY_STARTUP_US * 3); // play it safe
}

void adc_init(void)
{
    me.busy = false;
    me.active = ADC_VOLTAGE;
    me.cb = NULL;

    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_ADC12);
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);

    gpio_config(PIN_VSENSE_P, GPIO_DIRECTION_ANALOG, GPIO_PULL_NONE);
    gpio_config(PIN_VSENSE_N, GPIO_DIRECTION_ANALOG, GPIO_PULL_NONE);
    gpio_config(PIN_CURR_H, GPIO_DIRECTION_ANALOG, GPIO_PULL_NONE);
    gpio_config(PIN_CURR_L, GPIO_DIRECTION_ANALOG, GPIO_PULL_NONE);

    LL_ADC_SetCommonClock(__LL_ADC_COMMON_INSTANCE(ADC2), LL_ADC_CLOCK_SYNC_PCLK_DIV4);
    LL_ADC_SetMultimode(__LL_ADC_COMMON_INSTANCE(ADC2), LL_ADC_MULTI_INDEPENDENT);

    adc_disable_wait();

    LL_ADC_EnableInternalRegulator(ADC2);
    adc_delay_us(LL_ADC_DELAY_INTERNAL_REGUL_STAB_US);

    LL_ADC_SetCommonPathInternalCh(__LL_ADC_COMMON_INSTANCE(ADC2), LL_ADC_PATH_INTERNAL_VREFINT);
    adc_delay_us(LL_ADC_DELAY_VREFINT_STAB_US);

    LL_ADC_StartCalibration(ADC2, LL_ADC_SINGLE_ENDED);
    cpu_halt(10);
    while (LL_ADC_IsCalibrationOnGoing(ADC2))
        ;
    cpu_halt(10);

    LL_ADC_StartCalibration(ADC2, LL_ADC_DIFFERENTIAL_ENDED);
    while (LL_ADC_IsCalibrationOnGoing(ADC2))
        ;
    cpu_halt(10);

    LL_ADC_SetResolution(ADC2, LL_ADC_RESOLUTION_12B);
    LL_ADC_SetDataAlignment(ADC2, LL_ADC_DATA_ALIGN_RIGHT);
    LL_ADC_REG_SetTriggerSource(ADC2, LL_ADC_REG_TRIG_SOFTWARE);
    LL_ADC_REG_SetContinuousMode(ADC2, LL_ADC_REG_CONV_SINGLE);
    LL_ADC_REG_SetDMATransfer(ADC2, LL_ADC_REG_DMA_TRANSFER_NONE);
    LL_ADC_EnableIT_EOC(ADC2);
    NVIC_EnableIRQ(ADC1_2_IRQn);

    opamp2_configure(GAIN_X1);
    adc_prepare_single_conversion(LL_ADC_CHANNEL_VREFINT, LL_ADC_SINGLE_ENDED, LL_ADC_SAMPLINGTIME_601CYCLES_5);

    me.vdda = DEFAULT_VDDA;
}

int32_t adc_read_voltage(adc_cb_t cb)
{
    if (me.busy)
        return -1;

    me.busy = true;
    me.cb = cb;
    me.active = ADC_VOLTAGE;

    adc_prepare_single_conversion(LL_ADC_CHANNEL_14, LL_ADC_DIFFERENTIAL_ENDED, LL_ADC_SAMPLINGTIME_181CYCLES_5);
    LL_ADC_REG_StartConversion(ADC2);
    return 0;
}

int32_t adc_read_current(adc_cb_t cb, current_gain_t gain)
{
    if (me.busy)
        return -1;

    me.busy = true;
    me.cb = cb;
    me.active = ADC_CURRENT;

    opamp2_configure(gain);
    adc_prepare_single_conversion(LL_ADC_CHANNEL_3, LL_ADC_SINGLE_ENDED, LL_ADC_SAMPLINGTIME_181CYCLES_5);
    LL_ADC_REG_StartConversion(ADC2);
    return 0;
}

int32_t adc_read_vdda(adc_cb_t cb)
{
    if (me.busy)
        return -1;

    me.busy = true;
    me.cb = cb;
    me.active = ADC_VDDA;

    adc_prepare_single_conversion(LL_ADC_CHANNEL_VREFINT, LL_ADC_SINGLE_ENDED, LL_ADC_SAMPLINGTIME_601CYCLES_5);
    LL_ADC_REG_StartConversion(ADC2);
    return 0;
}

static void adc_cb_from_timer(int res, adc_t adc, int32_t raw, float value)
{
    if (me.cont.cb)
    {
        me.cont.cb(res, adc, raw, value);
    }
}

static void cont_timer_cb(timer_t *t)
{
    if (!me.cont.active)
        return;

    if (me.cont.stop)
    {
        timer_stop(t);
        adc_cb_t cb = me.cont.cb;
        me.cont.active = false;
        if (cb)
        {
            cb(-1, me.cont.adc, 0, 0.f);
        }
        return;
    }

    if (me.busy)
    {
        return;
    }

    me.busy = true;
    me.cont.adc = (me.cont.adc == ADC_VOLTAGE) ? ADC_CURRENT : ADC_VOLTAGE;
    me.cb = adc_cb_from_timer;
    me.active = me.cont.adc;
    if (me.cont.adc == ADC_CURRENT && me.opamp_gain != me.cont.gain_set)
        opamp2_configure(me.cont.gain_set);
    if (me.cont.adc == ADC_VOLTAGE)
        adc_prepare_single_conversion(LL_ADC_CHANNEL_14, LL_ADC_DIFFERENTIAL_ENDED, LL_ADC_SAMPLINGTIME_181CYCLES_5);
    else
        adc_prepare_single_conversion(LL_ADC_CHANNEL_3, LL_ADC_SINGLE_ENDED, LL_ADC_SAMPLINGTIME_181CYCLES_5);
    LL_ADC_REG_StartConversion(ADC2);
}

int32_t adc_read_continuous(adc_cb_t cb, tick_t delta)
{
    if (me.cont.active)
        return -1;
    me.cont.active = true;
    me.cont.cb = cb;
    timer_start(&me.cont.timer, cont_timer_cb, delta, TIMER_REPETITIVE);
    return 0;
}

int32_t adc_stop_continuous(void)
{
    if (!me.cont.active)
        return -1;
    me.cont.stop = true;
    return 0;
}

void adc_adjust_gain_continuous(current_gain_t gain)
{
    me.cont.gain_set = gain;
}

static int32_t adc_raw_adjust(adc_t adc, uint16_t raw)
{
    if (adc == ADC_VOLTAGE)
        return (int32_t)raw - 2048;
    else if (adc == ADC_VDDA)
        return (int32_t)__LL_ADC_CALC_VREFANALOG_VOLTAGE(raw, LL_ADC_RESOLUTION_12B);
    return (int32_t)raw;
}

static float adc_convert_to_volt(adc_t adc, int32_t value)
{
    switch (adc)
    {
    case ADC_VOLTAGE:
        return (VOLT_SENS_DIV * (float)value * me.vdda) / (float)(1 << 11);
    case ADC_CURRENT:
        return ((float)value * me.vdda) / 4095.0f;
    case ADC_VDDA:
        return (float)value / 1000.0f;
    default:
        return 0.0f;
    }
}

void ADC1_2_IRQHandler(void);
void ADC1_2_IRQHandler(void)
{
    if (LL_ADC_IsActiveFlag_OVR(ADC2))
        LL_ADC_ClearFlag_OVR(ADC2);

    if (LL_ADC_IsActiveFlag_EOC(ADC2))
    {
        LL_ADC_ClearFlag_EOC(ADC2);
        const int32_t raw = adc_raw_adjust(me.active, LL_ADC_REG_ReadConversionData12(ADC2));
        float volt = adc_convert_to_volt(me.active, raw);
        float value = volt;
        if (me.active == ADC_CURRENT)
            value = volt / CURR_SENS_R / me.gain_factor;
        adc_cb_t cb = me.cb;
        me.busy = false;
        if (cb)
            cb(0, me.active, raw, value);
    }
}

static void cli_adc_cb(int res, adc_t adc, int32_t raw, float value)
{
    printf("\n");
    float volts = adc_convert_to_volt(adc, raw);
    switch (adc)
    {
    case ADC_VOLTAGE:
        printf("VOLT ");
        break;
    case ADC_CURRENT:
        printf("CURR %smA ", value * 1000.f);
        break;
    case ADC_VDDA:
        printf("VDDA ");
        break;
    default:
        printf("???? ");
        break;
    }
    printf("%sV %d\n", ftostr(volts), raw);
}

static int cli_adc_read(int argc, const char **argv)
{
    int res = 0;
    if (argc < 1)
        return ERR_CLI_EINVAL;
    int gain = argc > 1 ? strtol(argv[1], NULL, 0) : 1;
    if (gain != 1 && gain != 2 && gain != 4 && gain != 8 && gain != 16)
        return ERR_CLI_EINVAL;
    current_gain_t cgain;
    switch (gain)
    {
    case 1:
        cgain = GAIN_X1;
        break;
    case 2:
        cgain = GAIN_X2;
        break;
    case 4:
        cgain = GAIN_X4;
        break;
    case 8:
        cgain = GAIN_X8;
        break;
    case 16:
        cgain = GAIN_X16;
        break;
    }
    switch (argv[0][0])
    {
    case 'v':
        res = adc_read_voltage(cli_adc_cb);
        break;
    case 'c':
        res = adc_read_current(cli_adc_cb, cgain);
        break;
    case 'a':
        res = adc_read_vdda(cli_adc_cb);
        break;
    default:
        return ERR_CLI_EINVAL;
    }
    return res;
}
CLI_FUNCTION(cli_adc_read, "adc_read", "<v|c|a> (<gain>): read ADC, Voltage|Current|vddA");
