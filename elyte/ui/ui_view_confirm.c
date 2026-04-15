#include <stdbool.h>
#include "minio.h"
#include "ui.h"
#include "ui_views.h"
#include "ui_scrolltext.h"

#define MSG_Y ((DISP_H / 4) + 1)

static struct
{
    const char *title;
    int title_w;
    const char *msg;
    int msg_w;
    ui_scrolltext_t msg_scrl;
    int yes_w;
    int no_w;
    bool selection;
    ui_confirm_cb_t cb;
    int select_x_current;
    int select_x_target;
    const void *user;
} me;

static void init(const ui_view_t *this)
{
    me.yes_w = gfx_string_width(UI_FONT_HUGE, "YES");
    me.no_w = gfx_string_width(UI_FONT_HUGE, "NO");
}

static void enter(const ui_view_t *this)
{
    ui_scrolltext_reset(&me.msg_scrl);
    me.selection = 0;
    me.select_x_target = me.selection ? 0 : DISP_W / 2;
    me.select_x_current = me.select_x_target;
}

static void exit(const ui_view_t *this)
{
    me.selection = false;
}

static void handle_event(const ui_view_t *this, uint32_t type, void *arg)
{
    switch (type)
    {
    case EVENT_UI_CLICK:
        if (me.cb)
        {
            me.cb(me.selection, me.user);
        }
        ui_trigger_update();
        break;
    case EVENT_UI_SCRL:
        me.selection = ((int)arg) > 0;
        me.select_x_target = me.selection ? 0 : DISP_W / 2;
        ui_trigger_update();
        break;
    default:
        break;
    }
}

static ui_tick_t paint(const ui_view_t *this, const gfx_ctx_t *ctx)
{
    ui_tick_t t = UI_TICK_NEVER;
    int y = 0;
    gfx_string(ctx, UI_FONT_NORMAL, me.title, DISP_W / 2 - me.title_w / 2, y, GFX_COL_SET);
    y = MSG_Y;
    if (me.msg_w > DISP_W)
    {
        t = ui_scrolltext_paint(&me.msg_scrl, ctx);
    }
    else
    {
        gfx_string(ctx, UI_FONT_SMALL, me.msg, DISP_W / 2 - me.msg_w / 2, y, GFX_COL_SET);
    }
    y += DISP_H / 4 + 1;
    gfx_string(ctx, UI_FONT_HUGE, "YES", DISP_W / 4 - me.yes_w / 2, y, GFX_COL_SET);
    gfx_string(ctx, UI_FONT_HUGE, "NO", 3 * DISP_W / 4 - me.no_w / 2, y, GFX_COL_SET);
    gfx_area_t a = {.x0 = me.select_x_current + 2, .x1 = me.select_x_current + DISP_W / 2 - 2, .y0 = y + 4, .y1 = y + DISP_H / 2 - 1};
    if (me.select_x_current != me.select_x_target)
    {
        me.select_x_current = ui_move_towards(me.select_x_current, me.select_x_target);
        t = 0;
    }
    gfx_fill(ctx, &a, GFX_COL_XOR);
    return t;
}

void ui_confirm(const char *title, const char *msg, ui_confirm_cb_t cb, const void *user)
{
    me.cb = cb;
    me.user = user;
    me.title = title;
    me.title_w = gfx_string_width(UI_FONT_NORMAL, title);
    me.msg = msg;
    me.msg_w = gfx_string_width(UI_FONT_SMALL, msg);
    ui_scrolltext_init(&me.msg_scrl, msg, UI_FONT_SMALL, 0, MSG_Y, DISP_W);
    ui_goto_view(&view_confirm, false);
}

UI_DECLARE_VIEW view_confirm = {
    .init = init,
    .enter = enter,
    .exit = exit,
    .handle_event = handle_event,
    .paint = paint,
    .name = "OK?",
};
