#include "minio.h"
#include "controller.h"
#include "rtc.h"
#include "ui.h"
#include "ui_views.h"

#define MAX_LINES 25

static int32_t y_offs = 0;
static status_event_info_t ctrl;

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
    case EVENT_CONTROL_STATUS:
        ctrl = *((status_event_info_t *)arg);
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
    y_offs = clamp(y_offs, -UI_FONT_MINI->max_height * MAX_LINES, 0);
    int y = y_offs;
    int x = 0;
    uint32_t att = ui_attention_mask();
    char str[32];
    if (att & (1 << ATT_PANIC))
    {
        gfx_string(ctx, UI_FONT_MINI, "ATT: PANIC", x, y, GFX_COL_SET);
        NL;
    }
    if (att & (1 << ATT_THERMO_ERR))
    {
        gfx_string(ctx, UI_FONT_MINI, "ATT: THERMO ERROR", x, y, GFX_COL_SET);
        NL;
    }
    if (att & (1 << ATT_MAX_TEMP))
    {
        gfx_string(ctx, UI_FONT_MINI, "ATT: MAX TEMP", x, y, GFX_COL_SET);
        NL;
    }
    if (att & (1 << ATT_MAX_POW_TIME))
    {
        gfx_string(ctx, UI_FONT_MINI, "ATT: MAX TIME POWER", x, y, GFX_COL_SET);
        NL;
    }
    if (att & (1 << ATT_MAX_ON_TIME))
    {
        gfx_string(ctx, UI_FONT_MINI, "ATT: MAX TIME ON", x, y, GFX_COL_SET);
        NL;
    }
    if (att) NL;
    sprintf(str, "Kiln %s", ctrl_is_enabled() ? "ON" : "OFF");
    gfx_string(ctx, UI_FONT_MINI, str, x, y, GFX_COL_SET);
    NL;
    sprintf(str, "Temp %dC", ctrl.current_temp);
    gfx_string(ctx, UI_FONT_MINI, str, x, y, GFX_COL_SET);
    NL;
    sprintf(str, "Target temp %dC", ctrl.target_temp);
    gfx_string(ctx, UI_FONT_MINI, str, x, y, GFX_COL_SET);
    NL;
    sprintf(str, "Power %d%%", ctrl.power * 10);
    gfx_string(ctx, UI_FONT_MINI, str, x, y, GFX_COL_SET);
    NL;
    rtc_datetime_t dt;
    rtc_get_date_time(&dt);
    sprintf(str, "Date %d-%02d-%02d", dt.date.year, dt.date.month + 1, dt.date.month_day + 1);
    gfx_string(ctx, UI_FONT_MINI, str, x, y, GFX_COL_SET);
    NL;
    sprintf(str, "Time %02d:%02d:%02d", dt.time.hour, dt.time.minute, dt.time.second);
    gfx_string(ctx, UI_FONT_MINI, str, x, y, GFX_COL_SET);
    NL;
    NL;
    gfx_string(ctx, UI_FONT_MINI, "commit " _str(BUILD_INFO_GIT_COMMIT), x, y, GFX_COL_SET);
    NL;
    gfx_string(ctx, UI_FONT_MINI, "branch " _str(BUILD_INFO_GIT_BRANCH), x, y, GFX_COL_SET);
    NL;
    gfx_string(ctx, UI_FONT_MINI, "build date " _str(BUILD_INFO_HOST_WHEN_DATE), x, y, GFX_COL_SET);
    NL;
    gfx_string(ctx, UI_FONT_MINI, "build time " _str(BUILD_INFO_HOST_WHEN_TIME), x, y, GFX_COL_SET);
    NL;
    NL;
    gfx_string(ctx, UI_FONT_MINI, "GPIO", x, y, GFX_COL_SET);
    NL;
    #define pri_port(x) ((x)>>4)+'A',(x)&0xf
    gfx_string(ctx, UI_FONT_MINI, "ELEMENT", 9, y, GFX_COL_SET);
    sprintf(str, "%c%02d", pri_port(PIN_ELEMENT));
    gfx_string(ctx, UI_FONT_MINI, str, 5*DISP_W/6, y, GFX_COL_SET);
    NL;
    gfx_string(ctx, UI_FONT_MINI, "THERMO_CLK", 9, y, GFX_COL_SET);
    sprintf(str, "%c%02d", pri_port(PIN_THERMOC_CLK));
    gfx_string(ctx, UI_FONT_MINI, str, 5*DISP_W/6, y, GFX_COL_SET);
    NL;
    gfx_string(ctx, UI_FONT_MINI, "THERMO_CS", 9, y, GFX_COL_SET);
    sprintf(str, "%c%02d", pri_port(PIN_THERMOC_CS));
    gfx_string(ctx, UI_FONT_MINI, str, 5*DISP_W/6, y, GFX_COL_SET);
    NL;
    gfx_string(ctx, UI_FONT_MINI, "THERMO_MISO", 9, y, GFX_COL_SET);
    sprintf(str, "%c%02d", pri_port(PIN_THERMOC_MISO));
    gfx_string(ctx, UI_FONT_MINI, str, 5*DISP_W/6, y, GFX_COL_SET);
    NL;
    gfx_string(ctx, UI_FONT_MINI, "DISP_SCL", 9, y, GFX_COL_SET);
    sprintf(str, "%c%02d", pri_port(PIN_DISP_SCL));
    gfx_string(ctx, UI_FONT_MINI, str, 5*DISP_W/6, y, GFX_COL_SET);
    NL;
    gfx_string(ctx, UI_FONT_MINI, "DISP_SDA", 9, y, GFX_COL_SET);
    sprintf(str, "%c%02d", pri_port(PIN_DISP_SDA));
    gfx_string(ctx, UI_FONT_MINI, str, 5*DISP_W/6, y, GFX_COL_SET);
    NL;
    gfx_string(ctx, UI_FONT_MINI, "UART_TX", 9, y, GFX_COL_SET);
    sprintf(str, "%c%02d", pri_port(PIN_UART_TX));
    gfx_string(ctx, UI_FONT_MINI, str, 5*DISP_W/6, y, GFX_COL_SET);
    NL;
    gfx_string(ctx, UI_FONT_MINI, "UART_RX", 9, y, GFX_COL_SET);
    sprintf(str, "%c%02d", pri_port(PIN_UART_RX));
    gfx_string(ctx, UI_FONT_MINI, str, 5*DISP_W/6, y, GFX_COL_SET);
    NL;

    return UI_TICK_NEVER;
}

UI_DECLARE_VIEW view_info = {
    .handle_event = handle_event,
    .paint = paint,
    .exit = exit,
};
