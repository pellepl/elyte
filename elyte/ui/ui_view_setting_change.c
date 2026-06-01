#include <stdbool.h>
#include "minio.h"
#include "settings.h"
#include "timer.h"
#include "ui.h"
#include "ui_views.h"
#include "ui_scrolltext.h"


#define MSG_Y ((DISP_H / 4) + 1)

static struct
{
    const char *title;
    int title_w;
    int descr_w;
    int unit_w;
    setting_t setting;
    ui_scrolltext_t title_scrl;
    ui_scrolltext_t descr_scrl;
    ui_setting_confirm_cb_t cb;
    uint32_t last_mod_s;
} me;

static void init(const ui_view_t *this)
{
}

static void enter(const ui_view_t *this)
{
    ui_scrolltext_reset(&me.title_scrl);
    ui_scrolltext_reset(&me.descr_scrl);
}

static void exit(const ui_view_t *this)
{
}

static void handle_event(const ui_view_t *this, uint32_t type, void *arg)
{
    uint32_t now_s = timer_uptime_ms() / 1000;
    switch (type)
    {
    case EVENT_UI_PRESSHOLD:
        if (me.cb)
            me.cb(me.setting.def->id, false, me.setting.value);
        ui_goto_view(&view_settings, true);
        ui_trigger_update();
        break;
    case EVENT_UI_CLICK:
        if (me.cb)
            me.cb(me.setting.def->id, true, me.setting.value);
        ui_goto_view(&view_settings, true);
        ui_trigger_update();
        break;
    case EVENT_UI_SCRL:
        me.last_mod_s = now_s;
        me.setting.value = ui_scroll_time_accelerator(-(int)arg, me.setting.value);
        me.setting.value = clamp_i32(me.setting.def->min, me.setting.value, me.setting.def->max);
        ui_trigger_update();
        break;
    case EVENT_SECOND_TICK:
        if (now_s - me.last_mod_s > UI_MODIFICATION_TMO_S)
        {
            if (me.cb)
                me.cb(me.setting.def->id, false, me.setting.value);
            ui_goto_view(&view_settings, true);
            ui_trigger_update();
        }
    default:
        break;
    }
}

static ui_tick_t paint(const ui_view_t *this, const gfx_ctx_t *ctx)
{
    ui_tick_t t = UI_TICK_NEVER;
    int y = 0;
    if (me.title_w > DISP_W)
        t = min_u32(t, ui_scrolltext_paint(&me.descr_scrl, ctx));
    else
        gfx_string(ctx, UI_FONT_NORMAL, me.setting.def->name, DISP_W / 2 - me.title_w / 2, y, GFX_COL_SET);
    y = MSG_Y;
    if (me.descr_w > DISP_W)
        t = min_u32(t, ui_scrolltext_paint(&me.descr_scrl, ctx));
    else
        gfx_string(ctx, UI_FONT_SMALL, me.setting.def->descr, DISP_W / 2 - me.descr_w / 2, y, GFX_COL_SET);
    y += DISP_H / 4 + 1;

    char str[32];
    snprintf(str, sizeof(str) - 1, "%d", me.setting.value);
    int w = gfx_string_width(UI_FONT_BIG, str);
    gfx_string(ctx, UI_FONT_BIG, str, DISP_W / 2 - w / 2, y, GFX_COL_SET);
    gfx_string(ctx, UI_FONT_MINI, me.setting.def->unit, DISP_W - 4 - me.unit_w, DISP_H - UI_FONT_MINI->max_height, GFX_COL_SET);
    return t;
}

void ui_setting_change(setting_id_t id, ui_setting_confirm_cb_t cb)
{
    if (setting_get(id, &me.setting) == NULL)
        return;
    me.cb = cb;

    me.title_w = gfx_string_width(UI_FONT_NORMAL, me.setting.def->name);
    me.descr_w = gfx_string_width(UI_FONT_SMALL, me.setting.def->descr);
    me.unit_w = gfx_string_width(UI_FONT_MINI, me.setting.def->unit);
    ui_scrolltext_init(&me.title_scrl, me.setting.def->name, UI_FONT_BIG, 0, MSG_Y, DISP_W);
    ui_scrolltext_init(&me.descr_scrl, me.setting.def->descr, UI_FONT_SMALL, 0, MSG_Y, DISP_W);
    ui_goto_view(&view_setting_change, false);
}

UI_DECLARE_VIEW view_setting_change = {
    .init = init,
    .enter = enter,
    .exit = exit,
    .handle_event = handle_event,
    .paint = paint,
    .name = "CHG",
};
