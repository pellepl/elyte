#include "minio.h"
#include "ui.h"
#include "ui_views.h"
#include "font_roboto_extrabold_30.h"

static void handle_event(const ui_view_t *this, uint32_t type, void *arg)
{
    switch (type)
    {
    case EVENT_UI_CLICK:
        ui_goto_view(&view_menu, true);
        ui_trigger_update();
        break;
    default:
        break;
    }
}

static ui_tick_t paint(const ui_view_t *this, const gfx_ctx_t *ctx)
{
    return UI_TICK_NEVER;
}

UI_DECLARE_VIEW view_sleep = {
    .handle_event = handle_event,
    .paint = paint,
};
