#include "minio.h"
#include "controller.h"
#include "ui.h"
#include "ui_views.h"

#define MAX_LINES 25

static int32_t y_offs = 0;
static status_info_t info;

static void exit(const ui_view_t *this)
{
    y_offs = 0;
}

static void handle_event(const ui_view_t *this, uint32_t type, void *arg)
{
    switch (type)
    {
    case EVENT_UI_CLICK:
        ui_goto_view(&view_menu, true);
        ui_trigger_update();
        break;
    case EVENT_UI_SCRL:
        if ((int)arg < 0)
        {
            y_offs -= UI_FONT_MINI->max_height / 2;
        }
        else
        {
            y_offs += UI_FONT_MINI->max_height / 2;
        }
        ui_trigger_update();
        break;
    case EVENT_STATUS:
        info = *ctrl_request_status();
        ui_trigger_update();
        break;
    default:
        break;
    }
}

#define NL                             \
    {                                  \
        y += UI_FONT_MINI->max_height; \
        x = 0;                         \
    }

static ui_tick_t paint(const ui_view_t *this, const gfx_ctx_t *ctx)
{
    y_offs = clamp_i32(-UI_FONT_MINI->max_height * MAX_LINES, y_offs, 0);
    int y = y_offs;
    int x = 0;
    char str[32];
    sprintf(str, "Build %s", stringify(BUILD_INFO_GIT_COMMIT));
    gfx_string(ctx, UI_FONT_MINI, str, x, y, GFX_COL_SET);
    NL;
    sprintf(str, "DAC   %d", info.dac);
    gfx_string(ctx, UI_FONT_MINI, str, x, y, GFX_COL_SET);
    NL;
    sprintf(str, "Holdoff %d", info.holdoff);
    gfx_string(ctx, UI_FONT_MINI, str, x, y, GFX_COL_SET);
    NL;
    sprintf(str, "CURR %s mA", ftostr1(info.current_avg * 1000.f));
    gfx_string(ctx, UI_FONT_MINI, str, x, y, GFX_COL_SET);
    NL;
    sprintf(str, "VOLT %s mV", ftostr1(info.voltage_avg * 1000.f));
    gfx_string(ctx, UI_FONT_MINI, str, x, y, GFX_COL_SET);
    NL;

    return UI_TICK_NEVER;
}

UI_DECLARE_VIEW view_info = {
    .handle_event = handle_event,
    .paint = paint,
    .exit = exit,
    .name = "INF",
};
