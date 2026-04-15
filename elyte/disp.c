#include <stddef.h>
#include <string.h>

#include "cpu.h"
#include "cli.h"
#include "disp.h"
#include "minio.h"
#include "timer.h"

#include "stm32f3xx_ll_bus.h"
#include "stm32f3xx_ll_gpio.h"
#include "stm32f3xx_ll_i2c.h"
#include "stm32f3xx_ll_dma.h"

#define SSD1306_I2C_ADDR 0x3c

static struct
{
    volatile bool dma_transfer_complete;
    volatile bool dma_transfer_error;
    volatile bool screen_update;
    volatile bool page_cmd;
    volatile uint8_t page_nbr;
    uint8_t gfx_buffer[DISP_W * DISP_H / 8];
    disp_cb_t done_cb;
} me;

static void i2c_dma_init(void);

static void i2c_write_bytes_dma(uint8_t addr, const uint8_t *data, uint16_t size)
{
    me.dma_transfer_complete = false;
    me.dma_transfer_error = false;

    LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_6);

    LL_DMA_SetMemoryAddress(DMA1, LL_DMA_CHANNEL_6, (uint32_t)data);
    LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_6, size);

    LL_DMA_ClearFlag_GI6(DMA1);
    LL_DMA_ClearFlag_TC6(DMA1);
    LL_DMA_ClearFlag_HT6(DMA1);
    LL_DMA_ClearFlag_TE6(DMA1);

    if (LL_I2C_IsActiveFlag_STOP(I2C1))
        LL_I2C_ClearFlag_STOP(I2C1);
    if (LL_I2C_IsActiveFlag_NACK(I2C1))
        LL_I2C_ClearFlag_NACK(I2C1);

    LL_I2C_HandleTransfer(I2C1,
                          (uint32_t)(addr << 1),
                          LL_I2C_ADDRSLAVE_7BIT,
                          size,
                          LL_I2C_MODE_AUTOEND,
                          LL_I2C_GENERATE_START_WRITE);

    LL_I2C_EnableDMAReq_TX(I2C1);
    LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_6);
}

static void tx_next_page(void)
{
    static uint8_t data[5 + 128];

    if (me.page_cmd)
    {
        if (me.page_nbr >= 8)
        {
            me.screen_update = false;
            if (me.done_cb)
                me.done_cb(0);
            return;
        }

        data[0] = 0x00;               // command mode indicator
        data[1] = 0xb0 + me.page_nbr; // set page address
        data[2] = 0x00;               // lower column address
        data[3] = 0x10;               // higher column address

        i2c_write_bytes_dma(SSD1306_I2C_ADDR, data, 4);
        me.page_cmd = false;
    }
    else
    {
        data[4] = 0x40;                                           // data mode indicator
        memcpy(&data[5], &me.gfx_buffer[me.page_nbr * 128], 128); // copy one page
        i2c_write_bytes_dma(SSD1306_I2C_ADDR, &data[4], 129);
        me.page_nbr++;
        me.page_cmd = true;
    }

    if (me.done_cb && me.dma_transfer_error)
        me.done_cb(-1);
}

void disp_init(void)
{
    i2c_dma_init();
}

void disp_set_enabled(bool enable, disp_cb_t done_cb)
{
    me.done_cb = done_cb;

    if (enable)
    {
        static const uint8_t init_cmds[] = {
            0x00,             // command mode indicator
            0xAE,             // Display OFF
            0xD5, 0x80,       // Set display clock divide ratio/oscillator frequency
            0xA8, DISP_H - 1, // Set multiplex ratio
            0xD3, 0x00,       // Set display offset
            0x40,             // Set start line

            0x8D, 0x14, // Enable charge pump

            0x20, 0x00, // Page addressing mode
            0xA1,       // Segment remap
            0xC8,       // COM scan direction
            0xDA, 0x12, // COM pins hardware configuration
            0x81, 0xCF, // Contrast
            0xD9, 0xF1, // Pre-charge period
            0xDB, 0x40, // VCOMH deselect level
            0xA4,       // Resume to RAM content display
            0xA6,       // Normal display
            0xAF        // Display ON
        };

        i2c_write_bytes_dma(SSD1306_I2C_ADDR, init_cmds, sizeof(init_cmds));
    }
    else
    {
        static const uint8_t cmds[] = {
            0x00, // command mode indicator
            0x8D,
            0x10, // Disable charge pump
            0xAE, // Display OFF
        };

        i2c_write_bytes_dma(SSD1306_I2C_ADDR, cmds, sizeof(cmds));
    }

    timer_halt_ms(100);
}

bool disp_screen_update(disp_cb_t done_cb)
{
    if (me.screen_update || !me.dma_transfer_complete)
        return false;

    me.screen_update = true;
    me.page_cmd = true;
    me.page_nbr = 0;
    me.done_cb = done_cb;

    tx_next_page();
    return true;
}

uint8_t *disp_gbuf(void)
{
    return me.gfx_buffer;
}

static void i2c_dma_init(void)
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_I2C1);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);

    // PB6 = I2C1_SCL, PB7 = I2C1_SDA, AF4, open-drain
    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_6, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_0_7(GPIOB, LL_GPIO_PIN_6, LL_GPIO_AF_4);
    LL_GPIO_SetPinOutputType(GPIOB, LL_GPIO_PIN_6, LL_GPIO_OUTPUT_OPENDRAIN);
    LL_GPIO_SetPinSpeed(GPIOB, LL_GPIO_PIN_6, LL_GPIO_SPEED_FREQ_HIGH);
    LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_6, LL_GPIO_PULL_UP);

    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_7, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_0_7(GPIOB, LL_GPIO_PIN_7, LL_GPIO_AF_4);
    LL_GPIO_SetPinOutputType(GPIOB, LL_GPIO_PIN_7, LL_GPIO_OUTPUT_OPENDRAIN);
    LL_GPIO_SetPinSpeed(GPIOB, LL_GPIO_PIN_7, LL_GPIO_SPEED_FREQ_HIGH);
    LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_7, LL_GPIO_PULL_UP);

    LL_I2C_Disable(I2C1);

    const uint32_t timing =
        (1 << 28) | // PRESC
        (0 << 20) | // SCLDEL
        (0 << 16) | // SDADEL
        (0 << 8) |  // SCLH
        (0 << 0);   // SCLL

    LL_I2C_SetTiming(I2C1, timing);

    // Optional cleanup
    LL_I2C_DisableOwnAddress1(I2C1);
    LL_I2C_SetOwnAddress1(I2C1, 0, LL_I2C_OWNADDRESS1_7BIT);
    LL_I2C_DisableGeneralCall(I2C1);
    LL_I2C_EnableClockStretching(I2C1);

    LL_I2C_EnableAutoEndMode(I2C1);
    LL_I2C_Enable(I2C1);

    // DMA: memory -> peripheral, normal mode, increment memory only
    LL_DMA_ConfigTransfer(DMA1, LL_DMA_CHANNEL_6,
                          LL_DMA_DIRECTION_MEMORY_TO_PERIPH |
                              LL_DMA_MODE_NORMAL |
                              LL_DMA_PERIPH_NOINCREMENT |
                              LL_DMA_MEMORY_INCREMENT |
                              LL_DMA_PDATAALIGN_BYTE |
                              LL_DMA_MDATAALIGN_BYTE |
                              LL_DMA_PRIORITY_HIGH);

    LL_DMA_SetPeriphAddress(DMA1, LL_DMA_CHANNEL_6, (uint32_t)&I2C1->TXDR);

    LL_DMA_EnableIT_TC(DMA1, LL_DMA_CHANNEL_6);
    LL_DMA_EnableIT_TE(DMA1, LL_DMA_CHANNEL_6);

    NVIC_SetPriority(DMA1_Channel6_IRQn, 1);
    NVIC_EnableIRQ(DMA1_Channel6_IRQn);

    me.dma_transfer_complete = true;
    me.dma_transfer_error = false;
    me.screen_update = false;
    me.page_cmd = false;
    me.page_nbr = 0;
    me.done_cb = NULL;
}

void DMA1_Channel6_IRQHandler(void);
void DMA1_Channel6_IRQHandler(void)
{
    if (LL_DMA_IsActiveFlag_TE6(DMA1))
    {
        LL_DMA_ClearFlag_TE6(DMA1);
        LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_6);

        me.dma_transfer_error = true;
        me.dma_transfer_complete = true;

        if (LL_I2C_IsActiveFlag_STOP(I2C1))
            LL_I2C_ClearFlag_STOP(I2C1);
        if (LL_I2C_IsActiveFlag_NACK(I2C1))
            LL_I2C_ClearFlag_NACK(I2C1);

        if (me.screen_update)
            me.screen_update = false;

        if (me.done_cb)
            me.done_cb(-1);

        return;
    }

    if (LL_DMA_IsActiveFlag_TC6(DMA1))
    {
        LL_DMA_ClearFlag_TC6(DMA1);
        LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_6);

        // Wait for the I2C transfer to actually finish on the wire.
        while (!LL_I2C_IsActiveFlag_STOP(I2C1))
        {
            if (LL_I2C_IsActiveFlag_NACK(I2C1))
            {
                LL_I2C_ClearFlag_NACK(I2C1);
                me.dma_transfer_error = true;
                break;
            }
        }

        if (LL_I2C_IsActiveFlag_STOP(I2C1))
            LL_I2C_ClearFlag_STOP(I2C1);

        me.dma_transfer_complete = true;

        if (me.screen_update && !me.dma_transfer_error)
        {
            tx_next_page();
        }
        else
        {
            if (me.screen_update && me.dma_transfer_error)
                me.screen_update = false;

            if (me.done_cb)
                me.done_cb(me.dma_transfer_error ? -1 : 0);
        }
    }
}

static void cli_disp_cb(int err)
{
    printf("\nDISP cb ret %d\n", err);
}

static int cli_disp_init(int argc, const char **argv)
{
    (void)argc;
    (void)argv;
    disp_init();
    return 0;
}
CLI_FUNCTION(cli_disp_init, "disp_init", ":");

static int cli_disp_ena(int argc, const char **argv)
{
    disp_set_enabled(argc == 0 || strtol(argv[0], NULL, 0), cli_disp_cb);
    return 0;
}
CLI_FUNCTION(cli_disp_ena, "disp_ena", "<x>:");

static int cli_disp_upd(int argc, const char **argv)
{
    memset(me.gfx_buffer, argc == 0 ? 0x00 : strtol(argv[0], NULL, 0), sizeof(me.gfx_buffer));
    return disp_screen_update(cli_disp_cb) ? 0 : -1;
}
CLI_FUNCTION(cli_disp_upd, "disp_upd", "<x>:");
