#pragma once
#include <stdint.h>
#include "ui.h"

typedef struct
{
    const char *string;
} ui_listitem_t;

struct ui_list;

typedef ui_tick_t (*ui_list_custom_item_paint_t)(struct ui_list *list, const gfx_ctx_t *ctx, uint8_t item_ix, bool selected, int x, int y, int w, int h);

typedef struct ui_list
{
    const ui_listitem_t *items;
    uint8_t item_count;
    int x, y, w, h;
    int selected_index;
    int item_h;
    int y_current;
    int y_target;
    int anim_select;
    int anim_select_max;
    ui_list_custom_item_paint_t item_paint;
    union
    {
        void *user;
        const void *const_user;
    };
} ui_list_t;

void ui_list_init(ui_list_t *list, const ui_listitem_t *items, uint8_t item_count, uint8_t selected_index, int x, int y, int w, int h);
ui_tick_t ui_list_paint(ui_list_t *list, const gfx_ctx_t *ctx);
void ui_list_move_index(ui_list_t *list, int delta);
void ui_list_set_index(ui_list_t *list, uint8_t index);
uint8_t ui_list_get_selected_index(ui_list_t *list);
void ui_list_arm(ui_list_t *list);
void ui_list_select(ui_list_t *list);
void ui_list_unarm(ui_list_t *list);
void ui_list_reset(ui_list_t *list);
void ui_list_set_custom_item_painter(ui_list_t *list, ui_list_custom_item_paint_t item_paint, uint32_t item_height);
