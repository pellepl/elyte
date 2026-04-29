#include <stddef.h>
#include "board.h"
#include "cli.h"
#include "dac.h"
#include "gpio_driver.h"
#include "minio.h"
#include "stm32f3xx_ll_bus.h"
#include "stm32f3xx_ll_dac.h"

extern void gpio_hal_stm32f3_af(uint16_t pin, uint8_t af);

static struct {
    uint32_t dac_val;
} me;

void dac_init(void)
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_DAC1);

    LL_DAC_Disable(DAC1, LL_DAC_CHANNEL_1);
    LL_DAC_DisableDMAReq(DAC1, LL_DAC_CHANNEL_1);
    LL_DAC_DisableTrigger(DAC1, LL_DAC_CHANNEL_1);
    LL_DAC_SetWaveAutoGeneration(DAC1, LL_DAC_CHANNEL_1, LL_DAC_WAVE_AUTO_GENERATION_NONE);
    LL_DAC_SetOutputBuffer(DAC1, LL_DAC_CHANNEL_1, LL_DAC_OUTPUT_BUFFER_ENABLE);

    me.dac_val = 0;
    LL_DAC_ConvertData12RightAligned(DAC1, LL_DAC_CHANNEL_1, me.dac_val);
    LL_DAC_Enable(DAC1, LL_DAC_CHANNEL_1);

    gpio_config(PIN_DAC, GPIO_DIRECTION_ANALOG, GPIO_PULL_NONE);
}

void dac_set(uint32_t dac)
{
    me.dac_val = dac > 0x0fff ? 0x0fff : dac;
    LL_DAC_ConvertData12RightAligned(DAC1, LL_DAC_CHANNEL_1, me.dac_val);
}

uint32_t dac_get(void) {
    return me.dac_val;
}

static int cli_dac_set(int argc, const char **argv)
{
    if (argc > 0) {
        me.dac_val = strtol(argv[0], NULL, 0);
        dac_set(me.dac_val);
    }
    printf("%d\n", me.dac_val);
    return 0;
}
CLI_FUNCTION(cli_dac_set, "dac_set", "<val>: set dac");
