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

static struct
{
    volatile bool busy;
    adc_t active;
    adc_cb_t cb;
    float vdda;
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
    LL_ADC_SetChannelSingleDiff(ADC2, LL_ADC_CHANNEL_14, LL_ADC_SINGLE_ENDED);
    LL_ADC_SetChannelSingleDiff(ADC2, channel, single_diff);
    LL_ADC_SetChannelSamplingTime(ADC2, channel, sample_time);
    LL_ADC_REG_SetSequencerLength(ADC2, LL_ADC_REG_SEQ_SCAN_DISABLE);
    LL_ADC_REG_SetSequencerRanks(ADC2, LL_ADC_REG_RANK_1, channel);
    adc_enable_wait();
}

static uint32_t opamp_pga_gain_from_current_gain(current_gain_t gain)
{
    switch (gain)
    {
    case GAIN_X2:
        return LL_OPAMP_PGA_GAIN_2;
    case GAIN_X4:
        return LL_OPAMP_PGA_GAIN_4;
    case GAIN_X8:
        return LL_OPAMP_PGA_GAIN_8;
    case GAIN_X16:
    default:
        return LL_OPAMP_PGA_GAIN_16;
    }
}

_Static_assert(PIN_CURR_H == PORTB(0), "OPAMP2 non-inv input misconfigured (LL_OPAMP_INPUT_NONINVERT_IO2)");

static void opamp2_configure(current_gain_t gain)
{
    LL_OPAMP_Disable(OPAMP2);
    LL_OPAMP_SetTrimmingMode(OPAMP2, LL_OPAMP_TRIMMING_FACTORY);
    LL_OPAMP_SetInputsMuxMode(OPAMP2, LL_OPAMP_INPUT_MUX_DISABLE);
    LL_OPAMP_SetInputNonInverting(OPAMP2, LL_OPAMP_INPUT_NONINVERT_IO2);

    if (gain == GAIN_X1)
    {
        LL_OPAMP_SetFunctionalMode(OPAMP2, LL_OPAMP_MODE_FOLLOWER);
    }
    else
    {
        LL_OPAMP_SetFunctionalMode(OPAMP2, LL_OPAMP_MODE_PGA);
        LL_OPAMP_SetInputInverting(OPAMP2, LL_OPAMP_INPUT_INVERT_CONNECT_NO);
        LL_OPAMP_SetPGAGain(OPAMP2, opamp_pga_gain_from_current_gain(gain));
    }

    LL_OPAMP_Enable(OPAMP2);
    adc_delay_us(LL_OPAMP_DELAY_STARTUP_US);
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
    LL_ADC_SetCommonPathInternalCh(__LL_ADC_COMMON_INSTANCE(ADC2), LL_ADC_PATH_INTERNAL_VREFINT);

    adc_disable_wait();

    LL_ADC_StartCalibration(ADC2, LL_ADC_SINGLE_ENDED);
    while (LL_ADC_IsCalibrationOnGoing(ADC2))
    {
    }

    LL_ADC_StartCalibration(ADC2, LL_ADC_DIFFERENTIAL_ENDED);
    while (LL_ADC_IsCalibrationOnGoing(ADC2))
    {
    }

    adc_delay_cycles(LL_ADC_DELAY_CALIB_ENABLE_ADC_CYCLES * 4U);

    LL_ADC_EnableInternalRegulator(ADC2);
    adc_delay_us(LL_ADC_DELAY_INTERNAL_REGUL_STAB_US);
    adc_delay_us(LL_ADC_DELAY_VREFINT_STAB_US);

    LL_ADC_SetResolution(ADC2, LL_ADC_RESOLUTION_12B);
    LL_ADC_SetDataAlignment(ADC2, LL_ADC_DATA_ALIGN_RIGHT);
    LL_ADC_REG_SetTriggerSource(ADC2, LL_ADC_REG_TRIG_SOFTWARE);
    LL_ADC_REG_SetContinuousMode(ADC2, LL_ADC_REG_CONV_SINGLE);
    LL_ADC_REG_SetDMATransfer(ADC2, LL_ADC_REG_DMA_TRANSFER_NONE);
    LL_ADC_EnableIT_EOC(ADC2);
    NVIC_EnableIRQ(ADC1_2_IRQn);

    opamp2_configure(GAIN_X1);
    adc_prepare_single_conversion(LL_ADC_CHANNEL_VOPAMP2, LL_ADC_SINGLE_ENDED, LL_ADC_SAMPLINGTIME_181CYCLES_5);

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
    adc_prepare_single_conversion(LL_ADC_CHANNEL_VOPAMP2, LL_ADC_SINGLE_ENDED, LL_ADC_SAMPLINGTIME_181CYCLES_5);
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

float adc_convert_to_volt(adc_t adc, int32_t value)
{
    switch (adc)
    {
    case ADC_VOLTAGE:
        return ((float)value * me.vdda) / (float)(1 << 11);
    case ADC_CURRENT:
        return ((float)value * me.vdda) / 4095.0f;
    case ADC_VDDA:
        return (float)value / 1000.0f;
    default:
        return 0.0f;
    }
}

static int32_t adc_value_from_result(uint16_t raw)
{
    if (me.active == ADC_VOLTAGE)
    {
        return (int32_t)raw - (1 << 11);
    }
    if (me.active == ADC_VDDA)
    {
        return (int32_t)__LL_ADC_CALC_VREFANALOG_VOLTAGE(raw, LL_ADC_RESOLUTION_12B);
    }
    return (int32_t)raw;
}

void ADC1_2_IRQHandler(void);
void ADC1_2_IRQHandler(void)
{
    if (LL_ADC_IsActiveFlag_EOC(ADC2))
    {
        const int32_t value = adc_value_from_result(LL_ADC_REG_ReadConversionData12(ADC2));
        LL_ADC_ClearFlag_EOC(ADC2);
        if (me.active == ADC_VDDA)
            me.vdda = (float)value / 1000.f;
        adc_cb_t cb = me.cb;
        me.busy = false;
        if (cb)
            cb(me.active, value);
    }
}

static void cli_adc_cb(adc_t adc, int32_t value)
{
    switch (adc)
    {
    case ADC_VOLTAGE:
        printf("VOLT ");
        break;
    case ADC_CURRENT:
        printf("CURR ");
        break;
    case ADC_VDDA:
        printf("VDDA ");
        break;
    default:
        printf("???? ");
        break;
    }
    printf("%sV (%d)\n", ftostr(adc_convert_to_volt(adc, value)), value);
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
