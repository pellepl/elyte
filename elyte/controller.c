#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "events.h"
#include "controller.h"
#include "timer.h"
#include "minio.h"
#include "board.h"
#include "gpio_driver.h"
#include "gpio_driver.h"
#include "assert.h"

static struct
{
    volatile bool panic;
    status_event_info_t info;
} me;

static event_t ev_panic;
static event_t ev_status;

void ctrl_start(void)
{
}

void ctrl_stop(void)
{
}
void ctrl_init(void)
{
}

void ctrl_panic(void)
{
    if (!me.panic)
    {
        me.panic = true;
        event_add(&ev_panic, EVENT_ATTENTION, NULL);
    }
}

bool ctrl_is_panicking(void)
{
    return me.panic;
}

void ctrl_set_dac(uint16_t dac) {
    me.info.dac = dac;
    event_add(&ev_status, EVENT_STATUS, &me.info);

}

static void ctrl_event_handler(uint32_t type, void *arg)
{
    switch (type)
    {
    case EVENT_SECOND_TICK:
        break;
    default:
        break;
    }
}
EVENT_HANDLER(ctrl_event_handler);
