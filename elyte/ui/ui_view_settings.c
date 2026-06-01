#include <stddef.h>
#include "input.h"
#include "minio.h"
#include "settings.h"
#include "ui.h"
#include "ui_list.h"
#include "ui_scrolltext.h"
#include "ui_views.h"

static struct
{
    ui_list_t list;
    ui_scrolltext_t scrl;
} me;

static ui_tick_t item_painter(ui_list_t *list, const gfx_ctx_t *ctx, uint8_t item_ix, bool selected,
                              int x, int y, int w, int h)
{
    setting_t s;
    ui_tick_t t = UI_TICK_NEVER;
    if (setting_get(item_ix, &s) == NULL)
        return UI_TICK_NEVER;
    if (selected)
    {
        ui_scrolltext_set_xy(&me.scrl, me.scrl.x, y);
        t = min_u32(t, ui_scrolltext_paint(&me.scrl, ctx));
    }
    else
    {
        gfx_string(ctx, UI_FONT_ITEM_SMALL, s.def->name, x, y, GFX_COL_SET);
    }
    t = min_u32(t, ui_list_paint_selector(list, ctx, y));
    return t;
}

static void update_selection(uint8_t ix)
{
    setting_t s;
    setting_get(ix, &s);
    ui_scrolltext_reset(&me.scrl);
    ui_scrolltext_set_text(&me.scrl, s.def->name);
}

static void init(const ui_view_t *this)
{
    ui_scrolltext_init(&me.scrl, "", UI_FONT_ITEM, 0, 0, DISP_W);
    ui_list_init(&me.list, NULL, SETTING_COUNT, 0, 0, 0, DISP_W, DISP_H);
    ui_list_set_custom_item_painter(&me.list, item_painter, UI_FONT_ITEM->max_height);
}

static void enter(const ui_view_t *this)
{
    ui_list_reset(&me.list);
    ui_scrolltext_reset(&me.scrl);
}

static void setting_confirm_change_cb(setting_id_t id, bool conf, int value)
{
    if (!conf)
        return;
    int err = setting_set(id, value);
    if (err)
        printf("ERROR: set %d = %d\n", id, value);
}

static void handle_event(const ui_view_t *this, uint32_t type, void *arg)
{
    switch (type)
    {
    case EVENT_UI_CLICK:
    {
        setting_t s;
        if (setting_get(ui_list_get_selected_index(&me.list), &s) == NULL)
            break;
        ui_setting_change(s.def->id, setting_confirm_change_cb);
        ui_trigger_update();
    }
    break;
    case EVENT_UI_SCRL:
        ui_list_move_index(&me.list, (int)arg);
        update_selection(ui_list_get_selected_index(&me.list));
        ui_trigger_update();
        break;
    default:
        break;
    }
}

static ui_tick_t paint(const ui_view_t *this, const gfx_ctx_t *ctx)
{
    return ui_list_paint(&me.list, ctx);
}

UI_DECLARE_VIEW view_settings = {
    .init = init,
    .enter = enter,
    .handle_event = handle_event,
    .paint = paint,
    .name = "SET",
};
