#include <stddef.h>
#include "ui_list.h"
#include "utils.h"

#define ANIM_SELECT_TOTAL (20)
#define ANIM_SELECT_ENTER (7)

static void update(ui_list_t *list)
{
    if (list->selected_index < 0)
        list->selected_index = 0;
    if (list->selected_index >= list->item_count)
        list->selected_index = list->item_count - 1;
    list->y_target = (list->h / 2 - list->item_h / 2 - list->selected_index * list->item_h + list->y) << 3;
}

void ui_list_init(ui_list_t *list, const ui_listitem_t *items, uint8_t item_count, uint8_t selected_index, int x, int y, int w, int h)
{
    list->item_count = item_count;
    list->items = items;
    list->selected_index = selected_index;
    list->x = x;
    list->y = y;
    list->w = w;
    list->h = h;
    update(list);
    list->y_current = list->y_target;
    list->item_h = UI_FONT_ITEM->max_height;
}

void ui_list_move_index(ui_list_t *list, int delta)
{
    list->selected_index += delta;
    update(list);
}

void ui_list_set_index(ui_list_t *list, uint8_t index)
{
    list->selected_index = index;
    update(list);
    list->y_current = list->y_target;
}

void ui_list_reset(ui_list_t *list)
{
    update(list);
    list->y_current = list->y_target;
    ui_list_unarm(list);
}

uint8_t ui_list_get_selected_index(ui_list_t *list)
{
    return list->selected_index;
}

void ui_list_select(ui_list_t *list)
{
    if (list->anim_select == 0)
        list->anim_select = 1;
    list->anim_select_max = ANIM_SELECT_TOTAL;
}

void ui_list_arm(ui_list_t *list)
{
    if (list->anim_select == 0)
        list->anim_select = 1;
    if (list->anim_select_max < ANIM_SELECT_ENTER)
        list->anim_select_max = ANIM_SELECT_ENTER;
}

void ui_list_unarm(ui_list_t *list)
{
    list->anim_select = 0;
    list->anim_select_max = 0;
}

void ui_list_set_custom_item_painter(ui_list_t *list, ui_list_custom_item_paint_t item_paint, uint32_t item_height)
{
    list->item_paint = item_paint;
    list->item_h = list->item_paint == NULL ? UI_FONT_ITEM->max_height : item_height;
}

static void paint_selector(ui_list_t *list, const gfx_ctx_t *ctx, int y)
{
    int anim = list->anim_select;
    if (anim >= ANIM_SELECT_TOTAL)
    {
        list->anim_select = ANIM_SELECT_TOTAL + 1;
        return;
    }
    gfx_area_t a = {
        .x0 = list->x,
        .y0 = y + 3,
        .x1 = list->x + 4,
        .y1 = y + list->item_h - 1,
    };
    if (anim > 0)
    {
        a.x0 = lerp_i32(list->x, list->x + list->w, anim - ANIM_SELECT_ENTER, ANIM_SELECT_TOTAL - ANIM_SELECT_ENTER);
        a.x1 = lerp_i32(list->x + 4, list->x + list->w, anim, ANIM_SELECT_ENTER);
        if (list->anim_select < list->anim_select_max)
            list->anim_select += 1;
    }
    gfx_fill(ctx, &a, GFX_COL_XOR);
}

ui_tick_t ui_list_paint(ui_list_t *list, const gfx_ctx_t *ctx)
{
    list->y_current = ui_move_towards(list->y_current, list->y_target);
    int y = (list->y_current + 3) >> 3;
    ui_tick_t t = UI_TICK_NEVER;
    for (uint8_t i = 0; i < list->item_count; i++)
    {
        bool selected = i == list->selected_index;
        if (list->item_paint)
        {
            t = min_u32(t, list->item_paint(list, ctx, i, selected, list->x, y, list->w, list->item_h));
        }
        else
        {
            const gfx_font_t *font = selected ? UI_FONT_ITEM : UI_FONT_ITEM_SMALL;
            int dy = list->item_h / 2 - font->max_height / 2;
            gfx_string(ctx, font, list->items[i].string, list->x + 8, y + dy, GFX_COL_SET);
            if (selected)
            {
                paint_selector(list, ctx, y);
            }
        }
        y += list->item_h;
    }
    if (list->y_current != list->y_target || (list->anim_select > 0 && list->anim_select <= ANIM_SELECT_TOTAL))
    {
        t = 0;
    }
    return t;
}
