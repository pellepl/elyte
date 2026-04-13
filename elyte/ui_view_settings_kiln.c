// view bugger is used for both settings and kiln

#include <stdbool.h>
#include <stddef.h>
#include "assert.h"
#include "cpu.h"
#include "minio.h"
#include "input.h"
#include "ui.h"
#include "ui_views.h"
#include "ui_scrolltext.h"
#include "ui_list.h"
#include "settings.h"

#define ITEM_X_OFFS 8
#define ITEM_SELECT_W 4
#define ITEM_HEIGHT 64

#define ANIM_FOCUS_LEN 20
#define ANIM_STORE_LEN 30

typedef struct
{
    ui_scrolltext_t scrl;
    ui_list_t list;
    bool list_focus;
    void *val_original;
    int anim_focus;
    int anim_store;
    uint8_t anim_store_ix;
    setting_t setting_indices[32];
    uint8_t setting_indices_count;
} view_data_t;

static view_data_t settings;
static view_data_t kiln;

static view_data_t *get_this(const ui_view_t *view)
{
    return (view_data_t *)view->user;
}

static uint8_t list_index(const ui_view_t *view)
{
    view_data_t *this = get_this(view);
    return ui_list_get_selected_index(&this->list);
}

static setting_t list_setting(const ui_view_t *view)
{
    view_data_t *this = get_this(view);
    uint8_t lix = list_index(view);
    ASSERT(lix < this->setting_indices_count);
    return this->setting_indices[lix];
}

static void exit(const ui_view_t *view)
{
    view_data_t *this = get_this(view);
    if (this->list_focus)
    {
        settings_set_val(list_setting(view), this->val_original);
        this->list_focus = false;
        this->anim_focus = ANIM_FOCUS_LEN;
    }
}

static void enter(const ui_view_t *view)
{
    view_data_t *this = get_this(view);
    this->list_focus = false;
    this->anim_focus = 0;
    this->anim_store = 0;
    ui_list_reset(&this->list);
    ui_scrolltext_reset(&this->scrl);
}

static const char *get_reset_msg(setting_t id)
{
    switch (id)
    {
    case SETTING_RESET:
        return "This will reset the CPU.";
    case SETTING_FACTORY:
        return "All your settings will be restored to default state.";
    default:
        return "Really sure about this?";
    }
}

static void update_scrl_text(const ui_view_t *view)
{
    view_data_t *this = get_this(view);
    const char *str_name = settings_get(list_setting(view))->ui_name;
    ui_scrolltext_init(&this->scrl, str_name, UI_FONT_NORMAL, ITEM_X_OFFS, 0, DISP_W - ITEM_X_OFFS);
}

static void reset_confirm_cb(bool answer, const void *user)
{
    const ui_view_t *view = (const ui_view_t *)user;
    view_data_t *this = (view_data_t *)get_this(view);
    if (answer)
    {
        const setting_def_t *s = settings_get(list_setting(view));
        printf("resetting %s\n", s->ui_name);
        if (s->id == SETTING_RESET)
        {
            cpu_reset();
        }
        else
        {
            settings_reset(s->id);
        }
    }
    ui_goto_view(&view_settings, true);
    this->anim_store = 1;
}

static void handle_button(const ui_view_t *view, input_button_t button)
{
    view_data_t *this = get_this(view);
    setting_t cur_setting = list_setting(view);

    switch (button)
    {
    case INPUT_BUTTON_ROTARY:
        if (!this->list_focus)
        {
            const setting_def_t *s = settings_get(cur_setting);
            if (s->unit == UNIT_RESETTABLE)
            {
                ui_confirm("CONFIRM", get_reset_msg(s->id), reset_confirm_cb, view);
            }
            else
            {
                this->val_original = settings_get_val(cur_setting);
                this->list_focus = true;
            }
        }
        else
        {
            void *val = settings_get_val(cur_setting);
            settings_store_val(cur_setting, val);
            this->anim_focus = 0;
            this->anim_store = 1;
            this->anim_store_ix = list_index(view);
            this->list_focus = false;
        }
        break;

    case INPUT_BUTTON_BACK:
        if (this->list_focus)
        {
            settings_set_val(cur_setting, this->val_original);
            this->list_focus = false;
            this->anim_focus = ANIM_FOCUS_LEN;
        }
        else
        {
            ui_goto_view(&view_menu, true);
        }
        break;

    default:
        break;
    }
}

static void handle_scroll(const ui_view_t *view, int delta)
{
    view_data_t *this = get_this(view);
    if (!this->list_focus)
    {
        ui_list_move_index(&this->list, delta);
        this->anim_focus = 0; // kill off any focus animations if scrolling
        update_scrl_text(view);
    }
    else
    {
        setting_t cur_setting = list_setting(view);
        const setting_def_t *u = settings_get(cur_setting);
        const int range = (int)u->max_value - (int)u->min_value;
        int v = (int)settings_get_val(cur_setting);
        delta = ui_scroll_accelerator(delta, range);
        settings_set_val(cur_setting, (void *)(v - delta));
    }
}

static void handle_event(const ui_view_t *view, uint32_t type, void *arg)
{
    switch (type)
    {
    case EVENT_UI_CLICK:
        handle_button(view, (input_button_t)arg);
        ui_trigger_update();
        break;
    case EVENT_UI_SCRL:
        handle_scroll(view, (int)arg);
        ui_trigger_update();
        break;
    default:
        break;
    }
}

static ui_tick_t item_paint(ui_list_t *list, const gfx_ctx_t *ctx, uint8_t item_ix, bool selected, int x, int y, int w, int h)
{
    view_data_t *this = get_this((const ui_view_t *)list->user);
    ui_tick_t t = UI_TICK_NEVER;
    if (y + h < ctx->clip.y0 || y >= ctx->clip.y1)
        return t;
    char str_val[20];
    setting_t setting = this->setting_indices[item_ix];
    const setting_def_t *s = settings_get(setting);
    const char *str_name = s->ui_name;
    const char *str_unit = settings_unit_str(setting);
    settings_get_val_as_string(setting, str_val, sizeof(str_val), false);
    if (selected)
    {
        ui_scrolltext_set_xy(&this->scrl, ITEM_X_OFFS, y);
        t = ui_scrolltext_paint(&this->scrl, ctx);
    }
    else
    {
        gfx_string(ctx, UI_FONT_ITEM, str_name, ITEM_X_OFFS, y, GFX_COL_SET);
    }
    if (setting == SETTING_UI_KILN_POWER)
    {
        uint32_t set_q8 = (((uint32_t)settings_get_val(setting)) << 8) / ((uint32_t)s->max_value);
        const int pad_y = h / 12;
        gfx_area_t a = {
            .x0 = ITEM_X_OFFS, .x1 = DISP_W - ITEM_X_OFFS, .y0 = y + ITEM_HEIGHT / 2 + pad_y, .y1 = y + h - pad_y};
        gfx_rect(ctx, &a, GFX_COL_SET);
        a.x0 += 2;
        a.x1 -= 2;
        a.y0 += 2;
        a.y1 -= 2;
        a.x1 = a.x0 + (((a.x1 - a.x0) * set_q8) >> 8);
        if (set_q8 > 0)
        {
            gfx_fill(ctx, &a, GFX_COL_SET);
        }
    }
    else
    {
        gfx_string(ctx, UI_FONT_BIG, str_val, ITEM_X_OFFS, y + ITEM_HEIGHT / 2 - UI_FONT_BIG->max_height / 3, GFX_COL_SET);
        int unit_w = gfx_string_width(UI_FONT_MINI, str_unit);
        gfx_string(ctx, UI_FONT_MINI, str_unit, w - unit_w, y + ITEM_HEIGHT / 2 + 2 * UI_FONT_BIG->max_height / 3, GFX_COL_SET);
    }

    if (selected)
    {
        // focus rectangle animation
        int x1 = lerp(ITEM_SELECT_W, w, this->anim_focus, ANIM_FOCUS_LEN);
        if (this->list_focus)
        {
            if (this->anim_focus < ANIM_FOCUS_LEN)
            {
                this->anim_focus++;
                t = 0;
            }
        }
        else
        {
            if (this->anim_focus > 0)
            {
                this->anim_focus--;
                t = 0;
            }
        }
        gfx_area_t a = {
            .x0 = 0, .x1 = x1, .y0 = y, .y1 = y + h};
        gfx_fill(ctx, &a, GFX_COL_XOR);
    }

    // store rectangle animation
    if (this->anim_store > 0 && item_ix == this->anim_store_ix)
    {
        int dw = lerp(0, (w - ITEM_SELECT_W) / 2, this->anim_store, ANIM_STORE_LEN);
        int dh = lerp(0, h / 2, this->anim_store, ANIM_STORE_LEN);
        gfx_area_t a = (gfx_area_t){
            .x0 = ITEM_SELECT_W + dw, .x1 = w - dw, .y0 = y + dh, .y1 = y + h - dh};
        gfx_fill(ctx, &a, GFX_COL_XOR);
        this->anim_store++;
        if (this->anim_store > ANIM_STORE_LEN)
        {
            this->anim_store = 0;
        }
        t = 0;
    }
    return t;
}

static ui_tick_t paint(const ui_view_t *view, const gfx_ctx_t *ctx)
{
    view_data_t *this = get_this(view);
    ui_tick_t t = UI_TICK_NEVER;
    t = min_frame(ui_list_paint(&this->list, ctx), t);
    return t;
}

static void init_common(const ui_view_t *view)
{
    view_data_t *this = get_this(view);
    ui_list_init(&this->list, NULL, this->setting_indices_count, 0, 0, 0, DISP_W, DISP_H);
    ui_list_set_custom_item_painter(&this->list, item_paint, ITEM_HEIGHT);
    this->list.const_user = view;
    ui_scrolltext_init(&this->scrl, "", UI_FONT_NORMAL, 0, 0, DISP_W);
    update_scrl_text(view);
}

static void init_settings(const ui_view_t *view)
{
    view_data_t *this = get_this(view);
    uint8_t ix = 0;
    for (setting_t s = _SETTING_LIST_FIRST; s <= _SETTING_LIST_LAST; s++)
    {
        this->setting_indices[ix] = s;
        ix++;
    }
    this->setting_indices_count = ix;
    init_common(view);
}

static void init_kiln(const ui_view_t *view)
{
    view_data_t *this = get_this(view);
    this->setting_indices_count = 2;
    this->setting_indices[0] = SETTING_UI_KILN_TEMP;
    this->setting_indices[1] = SETTING_UI_KILN_POWER;
    init_common(view);
}

UI_DECLARE_VIEW view_settings = {
    .init = init_settings,
    .enter = enter,
    .exit = exit,
    .handle_event = handle_event,
    .paint = paint,
    .user = &settings};

UI_DECLARE_VIEW view_kiln = {
    .init = init_kiln,
    .enter = enter,
    .exit = exit,
    .handle_event = handle_event,
    .paint = paint,
    .user = &kiln};
