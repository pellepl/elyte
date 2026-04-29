#include <stdbool.h>
#include <stddef.h>
#include "adc.h"
#include "board.h"
#include "cli.h"
#include "controller.h"
#include "dac.h"
#include "disp.h"
#include "events.h"
#include "flash_driver.h"
#include "gpio_driver.h"
#include "input.h"
#include "minio.h"
#include "ringbuffer.h"
#include "second.h"
#include "timer.h"
#include "uart_driver.h"
#include "ui.h"
#include "utils.h"

#define NOINIT_MAGIC 0xfeedc0ca

static struct
{
    uint8_t rx_buf[32];
    ringbuffer_t cli_rx_rb;
} me;

static void cli_cb(const char *func_name, int res);

// Overrides weak empty implementation, IRQ context
void uart_irq_rxchar_stm32f3(int handle, char c);
void uart_irq_rxchar_stm32f3(int handle, char c)
{
    (void)handle;
    if (me.cli_rx_rb.buffer == NULL || me.cli_rx_rb.max_len == 0)
        return;
    ringbuffer_putc(&me.cli_rx_rb, (uint8_t)c);
}

static void consume_uart_rx(void)
{
    while (ringbuffer_available(&me.cli_rx_rb) > 0)
    {
        uint8_t *buf_ptr;
        int avail = ringbuffer_available_linear(&me.cli_rx_rb, &buf_ptr);
        if (avail > 0)
        {
            cli_parse((const char *)buf_ptr, avail);
            ringbuffer_get(&me.cli_rx_rb, NULL, avail);
        }
    }
}

static void gpio_setup(void)
{
    gpio_set(PIN_LED_G, 0);
    gpio_set(PIN_LED_R, 0);
    gpio_config(PIN_LED_G, GPIO_DIRECTION_OUTPUT, GPIO_PULL_NONE);
    gpio_config(PIN_LED_R, GPIO_DIRECTION_OUTPUT, GPIO_PULL_NONE);
}

static void uart_setup(void)
{
    uart_config_t cfg = {
        .baudrate = UART_BAUDRATE_115200,
        .parity = UART_PARITY_NONE,
        .stopbits = UART_STOPBITS_1,
        .flowcontrol = UART_FLOWCONTROL_NONE};
    uart_deinit(UART_STD);
    NVIC_SetPriority(USART1_IRQn, 5);
    NVIC_SetPriority(USART2_IRQn, 5);
    NVIC_SetPriority(USART3_IRQn, 5);
    uart_init(UART_STD, &cfg);
}

static void event_handler(uint32_t event, void *arg)
{
    // printf("ev %d\t%08x\n", event, arg);
    const event_func_t *ev_fns = EVENT_HANDLERS_START;
    const event_func_t *ev_fns_end = EVENT_HANDLERS_END;
    while (ev_fns < ev_fns_end)
    {
        ((event_func_t)(*ev_fns))(event, arg);
        ev_fns++;
    }
}

static void noinit_init(void)
{
    volatile noinit_t *n = noinit();
    if (n->magic != NOINIT_MAGIC)
    {
        memset((volatile void *)n, 0, sizeof(noinit_t));
        n->magic = NOINIT_MAGIC;
        n->reset_count = 0;
    }
    n->reset_count++;
}

int main(void)
{
    cpu_init();
    noinit_init();
    board_init();
    NVIC_SetPriorityGrouping(/*NVIC_PRIORITYGROUP_4*/3);
    gpio_init();
    gpio_setup();
    dac_init();
    gpio_set(PIN_LED_R, 1);
    ringbuffer_init(&me.cli_rx_rb, me.rx_buf, sizeof(me.rx_buf));
    uart_setup();
    printf("\n" stringify(BUILD_INFO_TARGET_NAME) "\n");
    printf(stringify(BUILD_INFO_GIT_COMMIT) "@" stringify(BUILD_INFO_HOST_WHO) " " stringify(BUILD_INFO_HOST_WHEN_DATE) " " stringify(BUILD_INFO_HOST_WHEN_TIME) "\n");
    timer_init();
    cli_init(cli_cb, "\r\n;", " ,", "", "");

    adc_init();

    event_init(event_handler);
    disp_init();
    disp_set_enabled(false, NULL);
    gfx_init();
    ui_init();
    ui_trigger_update();
    input_init();
    second_init();
    disp_set_enabled(true, NULL);
    ctrl_init();
    gpio_set(PIN_LED_G, 1);

    while (1)
    {
        consume_uart_rx();
        while (event_execute_one())
            ;

        __WFI();
    } // main spinner
}

static int cli_help(int argc, const char **argv)
{
    int len = cli_entry_list_length();
    cli_entry_t **list = cli_entry_list_start();
    const int cmd_sep = 20;
    const int help_line_len = 100;
    for (int i = 0; i < len; i++)
    {
        if (argc > 0)
        {
            bool found = false;
            for (int j = 0; j < argc; j++)
            {
                if (strstr(list[i]->name, argv[j]))
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                continue;
        }
        const char *help_str = list[i]->help;
        size_t cmd_len = strlen(list[i]->name);
        int help_len = strlen(help_str);
        size_t spaces = cmd_len > cmd_sep ? 2 : cmd_sep - cmd_len;
        printf("%s", list[i]->name);
        if (help_len == 0)
            printf("\n");

        while (help_len > 0)
        {
            int this_line_len = help_line_len - cmd_len - spaces;
            for (size_t s = 0; s < spaces; s++)
                printf(" ");
            while (this_line_len > 0 && help_len > 0)
            {
                printf("%c", *help_str++);
                this_line_len--;
                help_len--;
            }
            cmd_len = 0;
            spaces = cmd_sep;
            printf("\n");
        }
    }
    return 0;
}
CLI_FUNCTION(cli_help, "help", ": display help");

static int cli_reset(int argc, const char **argv)
{
    cpu_reset();
    return 0;
}
CLI_FUNCTION(cli_reset, "reset", ": reset");

static int cli_dbg_reset(int argc, const char **argv)
{
    volatile noinit_t *n = noinit();
    n->magic = 0;
    return 0;
}
CLI_FUNCTION(cli_dbg_reset, "dbg_reset", ": reset dbg info");

static int cli_dbg_show(int argc, const char **argv)
{
    volatile noinit_t *n = noinit();
    if (n->magic != NOINIT_MAGIC)
        printf("BAD MAGIC\n");
    printf("reset count:    %d\n", n->reset_count);
    printf("hardfault count: %d (last %d)\n", n->hardfault_count, n->hardfault_count_last);
    for (size_t i = 0; i < sizeof(n->entries); i++)
    {
        if (n->entries[i] == 0 && n->exits[i] == 0)
            continue;
        printf("entry/exit %02d: %d/%d\n", i, n->entries[i], n->exits[i]);
    }
    if (n->irq_trap_count > 0)
    {
        printf("irq_trap: count=%d ipsr=%d\n", n->irq_trap_count, n->irq_trap_last_ipsr);
    }
    n->hardfault_count_last = n->hardfault_count;
    return 0;
}
CLI_FUNCTION(cli_dbg_show, "dbg_show", ": show dbg info");

static void cli_cb(const char *func_name, int res)
{
    switch (res)
    {
    case 0:
        printf("OK\n");
        break;
    case ERR_CLI_NO_ENTRY:
        cli_help(0, (void *)0);
        printf("ERROR \"%s\" not found\n", func_name);
        break;
    case ERR_CLI_INPUT_OVERFLOW:
        printf("ERROR too long input\n");
        break;
    case ERR_CLI_ARGUMENT_OVERFLOW:
        printf("ERROR too many arguments\n");
        break;
    case ERR_CLI_SILENT:
        break;
    default:
        printf("ERROR (%d)\n", res);
        break;
    }
}

void HardFault_Handler(void);
void HardFault_Handler(void)
{
    volatile noinit_t *n = noinit();
    n->hardfault_count++;
    cpu_interrupt_disable();
    printf("\nHARDFAULT\n");
    while (1)
        ;
}
