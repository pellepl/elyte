#include <stdbool.h>
#include <stddef.h>
#include "cli.h"
#include "input.h"
#include "minio.h"
#include "timer.h"
#include "ui.h"
#include "ui_views.h"

static void init(const ui_view_t *this)
{
}

static void enter(const ui_view_t *this)
{
}

static void exit(const ui_view_t *this)
{
}

static void handle_event(const ui_view_t *this, uint32_t type, void *arg)
{
    switch (type)
    {
    case EVENT_UI_CLICK:
        break;
    case EVENT_UI_SCRL:
        break;
    default:
        break;
    }
}

static ui_tick_t paint(const ui_view_t *this, const gfx_ctx_t *ctx)
{
    return UI_TICK_NEVER;
}

UI_DECLARE_VIEW view_graph = {
    .init = init,
    .enter = enter,
    .exit = exit,
    .handle_event = handle_event,
    .paint = paint,
    .name = "GRA",
};
