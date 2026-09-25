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
    if (selected)
    {
        ui_scrolltext_set_xy(&me.scrl, x + 8, y);
        t = min_u32(t, ui_scrolltext_paint(&me.scrl, ctx));
    }
    else
    {
        setting_id_t id = (setting_id_t)(item_ix - 1);
        const char *str;
        if (item_ix > 0)
        {
            setting_get(id, &s);
            str = s.def->name;
        }
        else
        {
            str = "Back";
        }
        gfx_string(ctx, UI_FONT_ITEM_SMALL, str, x + 8, y, GFX_COL_SET);
    }
    if (selected)
        t = min_u32(t, ui_list_paint_selector(list, ctx, y));
    return t;
}

static void update_selection(uint8_t ix)
{
    ui_scrolltext_reset(&me.scrl);
    setting_id_t id = (setting_id_t)(ix - 1);
    if (ix > 0)
    {
        setting_t s;
        setting_get(id, &s);
        ui_scrolltext_set_text(&me.scrl, s.def->name);
    }
    else
    {
        ui_scrolltext_set_text(&me.scrl, "Back");
    }
}

static void init(const ui_view_t *this)
{
    ui_scrolltext_init(&me.scrl, "", UI_FONT_ITEM, 8, 0, DISP_W - 8);
    ui_list_init(&me.list, NULL, SETTING_COUNT + 1, 1, 0, 0, DISP_W, DISP_H);
    ui_list_set_custom_item_painter(&me.list, item_painter, UI_FONT_ITEM->max_height);
}

static void enter(const ui_view_t *this)
{
    ui_list_reset(&me.list);
    ui_scrolltext_reset(&me.scrl);
    update_selection(ui_list_get_selected_index(&me.list));
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
        uint8_t sel_ix = ui_list_get_selected_index(&me.list);
        if (sel_ix == 0)
        {
            ui_goto_view(&view_menu, true);
        }
        else
        {
            setting_t s;
            setting_get((setting_id_t)(sel_ix - 1), &s);
            ui_setting_change(s.def->id, setting_confirm_change_cb, this);
        }
        ui_trigger_update();
    }
    break;
    case EVENT_UI_SCRL:
        ui_list_move_index(&me.list, (int)arg < 0 ? -1 : 1);
        update_selection(ui_list_get_selected_index(&me.list));
        ui_trigger_update();
        break;
    case EVENT_UI_PRESSHOLD:
        ui_goto_view(&view_main, true);
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
