#include <stdbool.h>
#include "input.h"
#include "controller.h"
#include "minio.h"
#include "ui.h"
#include "ui_views.h"

#define SIZE_FULL 16
#define Y_VOLTAGE 0
#define Y_CURRENT (DISP_H / 2)

static int dac = 0;

static struct
{
    enum
    {
        NONE,
        SELECT_VOLTAGE,
        SELECT_CURRENT,
        ADJUST_VOLTAGE,
        ADJUST_CURRENT,
    } adjust;
    struct
    {
        int select_y;
        int select_y_target;
        int select_size;
        int32_t i_ma;
        int32_t v_mv;
    } ui;

    status_info_t info;
} me;

static void init(const ui_view_t *this)
{
    me.ui.v_mv = 0;
    me.ui.i_ma = 0;
}

static void enter(const ui_view_t *this)
{
    me.info = *ctrl_request_status();
    me.adjust = NONE;
}

static void exit(const ui_view_t *this)
{
    me.adjust = NONE;
}

static void handle_event(const ui_view_t *this, uint32_t type, void *arg)
{
    switch (type)
    {
    case EVENT_UI_PRESSHOLD:
        if (me.adjust != NONE)
        {
            me.adjust = NONE;
        }
        else
        {
            ui_goto_view(&view_menu, false);
        }
        ui_trigger_update();
        break;
    case EVENT_UI_CLICK:
        if (me.adjust == NONE)
        {
            me.adjust = SELECT_CURRENT;
            me.ui.select_size = 0;
            me.ui.select_y = me.ui.select_y_target = Y_CURRENT;
        }
        else if (me.adjust == SELECT_CURRENT || me.adjust == SELECT_VOLTAGE)
        {
            me.adjust = me.adjust == SELECT_VOLTAGE ? ADJUST_VOLTAGE : ADJUST_CURRENT;
            me.ui.select_size = 0;
        }
        else
        {
            // TODO PETER update control with respective ui setting
            me.adjust = NONE;
        }
        ui_trigger_update();
        break;
    case EVENT_UI_SCRL:
        if (input_is_button_pressed(INPUT_BUTTON_ROTARY))
        {
            dac += 4 * ((int)arg);
            if (dac < 0)
                dac = 0;
            if (dac > 0xfff)
                dac = 0xfff;
            ctrl_set_dac(dac);
            return;
        }
        if (me.adjust == NONE || me.adjust == SELECT_CURRENT || me.adjust == SELECT_VOLTAGE)
        {
            me.adjust = (int)arg < 0 ? SELECT_VOLTAGE : SELECT_CURRENT;
            me.ui.select_y_target = (int)arg < 0 ? Y_VOLTAGE : Y_CURRENT;
            if (me.adjust == NONE)
                me.ui.select_size = 0;
        }
        else if (me.adjust == ADJUST_CURRENT)
        {
            
            me.ui.i_ma += ui_scroll_time_accelerator((int)arg);
            me.ui.i_ma = clamp_i32(0, me.ui.i_ma, 1000);

        }
        else if (me.adjust == ADJUST_VOLTAGE)
        {
            me.ui.v_mv += ui_scroll_time_accelerator((int)arg);
            me.ui.v_mv = clamp_i32(0, me.ui.v_mv, 2500);
        }
        ui_trigger_update();
        break;
    case EVENT_STATUS:
        me.info = *((status_info_t *)arg);
        ui_trigger_update();
        break;
    default:
        break;
    }
}

static ui_tick_t paint(const ui_view_t *this, const gfx_ctx_t *ctx)
{
    ui_tick_t t = UI_TICK_NEVER;
    int str_w;
    char str[32];

    const int unit_w = 18;

    for (int i = 0; i < 2; i++)
    {
        int16_t y = i ? Y_CURRENT : Y_VOLTAGE;
        const char *unit_str = i ? "mA" : "mV";
        int32_t value = (int32_t)((i ? me.info.current_avg : me.info.voltage_avg) * 1000);
        uint32_t setting = i ? me.ui.i_ma : me.ui.v_mv;
        bool select = i ? me.adjust == SELECT_CURRENT : me.adjust == SELECT_VOLTAGE;
        bool adjust = i ? me.adjust == ADJUST_CURRENT : me.adjust == ADJUST_VOLTAGE;
        if (adjust)
        {
            sprintf(str, "%d", value);
            gfx_string(ctx, UI_FONT_MINI, str, 8, y, GFX_COL_SET);
            sprintf(str, "%d", setting);
            str_w = gfx_string_width(UI_FONT_HUGE, str);
            gfx_string(ctx, UI_FONT_HUGE, str, DISP_W - unit_w - str_w, y, GFX_COL_SET);
        }
        else if (select)
        {
            sprintf(str, "%d", setting);
            gfx_string(ctx, UI_FONT_MINI, str, 8, y, GFX_COL_SET);
            sprintf(str, "%d", value);
            str_w = gfx_string_width(UI_FONT_HUGE, str);
            gfx_string(ctx, UI_FONT_HUGE, str, DISP_W - unit_w - str_w, y, GFX_COL_SET);
        }
        else
        {
            sprintf(str, "%d", value);
            str_w = gfx_string_width(UI_FONT_HUGE, str);
            gfx_string(ctx, UI_FONT_HUGE, str, DISP_W - unit_w - str_w, y, GFX_COL_SET);
        }

        gfx_string(ctx, UI_FONT_MINI, unit_str, DISP_W - unit_w,
                   y + UI_FONT_HUGE->max_height - UI_FONT_MINI->max_height - 4, GFX_COL_SET);
    }

    int select_size_target = 0;
    if (me.adjust != NONE)
    {
        select_size_target = SIZE_FULL;
    }
    if (me.ui.select_size != select_size_target)
    {
        me.ui.select_size = ui_move_towards(me.ui.select_size, select_size_target);
        t = 0;
    }
    if (me.ui.select_y != me.ui.select_y_target)
    {
        me.ui.select_y = ui_move_towards(me.ui.select_y, me.ui.select_y_target);
        t = 0;
    }
    if (me.adjust == NONE && me.ui.select_size > 0)
    {
        int w = (DISP_W / 2 * me.ui.select_size) / SIZE_FULL;
        int h = (DISP_H / 4 * me.ui.select_size) / SIZE_FULL;
        int x = DISP_W / 2;
        int y = me.ui.select_y + DISP_H / 4;
        gfx_area_t a = {.x0 = x - w, .x1 = x + w, .y0 = y - h, .y1 = y + h};
        gfx_fill(ctx, &a, GFX_COL_XOR);
    }
    else if (me.adjust != NONE)
    {
        int w = ((DISP_W - 4) * me.ui.select_size) / SIZE_FULL;
        int h = (DISP_H / 4 * me.ui.select_size) / SIZE_FULL;
        // int x = DISP_W / 2;
        int y = me.ui.select_y + DISP_H / 4;
        if (me.adjust == SELECT_CURRENT || me.adjust == SELECT_VOLTAGE)
        {
            gfx_area_t a = {.x0 = 0, .x1 = 4, .y0 = y - h, .y1 = y + h};
            gfx_fill(ctx, &a, GFX_COL_XOR);
        }
        else
        {
            gfx_area_t a = {.x0 = 0, .x1 = w, .y0 = me.ui.select_y, .y1 = me.ui.select_y + DISP_H / 2};
            gfx_fill(ctx, &a, GFX_COL_XOR);
        }
    }

    return t;
}

UI_DECLARE_VIEW view_main = {
    .init = init,
    .enter = enter,
    .exit = exit,
    .handle_event = handle_event,
    .paint = paint,
    .name = "MAI",
};
