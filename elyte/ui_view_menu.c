#include <stddef.h>
#include "input.h"
#include "minio.h"
#include "ui.h"
#include "ui_list.h"
#include "ui_views.h"

static struct
{
    ui_list_t list;
} me;

enum
{
    LIST_ITEM_SETTINGS,
    LIST_ITEM_KILN,
    LIST_ITEM_TEMPHIST,
    LIST_ITEM_INFO,
};

static const ui_listitem_t items[] = {
    [LIST_ITEM_SETTINGS] = (ui_listitem_t){.string = "Settings"},
    [LIST_ITEM_KILN] = (ui_listitem_t){.string = "Set kiln"},
    [LIST_ITEM_TEMPHIST] = (ui_listitem_t){.string = "Temp history"},
    [LIST_ITEM_INFO] = (ui_listitem_t){.string = "System info"},
};

static void init(const ui_view_t *this)
{
    ui_list_init(&me.list, items, ARRAY_LENGTH(items), LIST_ITEM_KILN, 0, 0, DISP_W, DISP_H);
}

static void enter(const ui_view_t *this)
{
    ui_list_reset(&me.list);
}

static void handle_button(input_button_t button)
{
    switch (button)
    {
    case INPUT_BUTTON_ROTARY:
    {
        switch (ui_list_get_selected_index(&me.list))
        {
        case LIST_ITEM_SETTINGS:
            ui_goto_view(&view_settings, false);
            break;
        case LIST_ITEM_KILN:
            ui_goto_view(&view_kiln, false);
            break;
        case LIST_ITEM_TEMPHIST:
            ui_goto_view(&view_graph, false);
            break;
        case LIST_ITEM_INFO:
            ui_goto_view(&view_info, false);
            break;
        }
    }
    break;
    default:
        break;
    }
}

static void handle_event(const ui_view_t *this, uint32_t type, void *arg)
{
    switch (type)
    {
    case EVENT_UI_CLICK:
        handle_button((input_button_t)arg);
        ui_trigger_update();
        break;
    case EVENT_UI_SCRL:
        ui_list_move_index(&me.list, (int)arg);
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

UI_DECLARE_VIEW view_menu = {
    .init = init,
    .enter = enter,
    .handle_event = handle_event,
    .paint = paint,
};
