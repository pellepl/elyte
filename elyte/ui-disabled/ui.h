#pragma once

#include <stdint.h>
#include "events.h"
#include "gfx.h"
#include "font_pixeloidsans_9.h"
#include "font_roboto_semibold_13.h"
#include "font_roboto_semibold_16.h"
#include "font_roboto_extrabold_20.h"
#include "font_roboto_extrabold_30.h"

#define _str(x) __str(x)
#define __str(x) #x

#define ARRAY_LENGTH(x) (sizeof(x) / (sizeof((x)[0])))

// #define UI_FONT_MINI (&font_pixeloidsans_9)
// #define UI_FONT_SMALL (&font_roboto_semibold_13)
// #define UI_FONT_NORMAL (&font_roboto_semibold_16)
// #define UI_FONT_BIG (&font_roboto_extrabold_20)
// #define UI_FONT_HUGE (&font_roboto_extrabold_30)
#define UI_FONT_MINI (&font_roboto_semibold_13)
#define UI_FONT_SMALL (&font_roboto_semibold_13)
#define UI_FONT_NORMAL (&font_roboto_semibold_13)
#define UI_FONT_BIG (&font_roboto_semibold_13)
#define UI_FONT_HUGE (&font_roboto_semibold_13)
#define UI_FONT_ITEM_SMALL UI_FONT_NORMAL
#define UI_FONT_ITEM UI_FONT_BIG

#define UI_TICK_NEVER (ui_tick_t)0xffffffff

#define UI_DECLARE_VIEW \
    const __attribute__((used, section(".ui_view"))) ui_view_t

extern const intptr_t __ui_views_start;
extern const intptr_t __ui_views_end;
#define UI_VIEWS_START (const ui_view_t *)&__ui_views_start
#define UI_VIEWS_END (const ui_view_t *)&__ui_views_end

struct ui_view;
typedef uint32_t ui_tick_t;
typedef void (*ui_init_func_t)(const struct ui_view *view);
typedef void (*ui_enter_func_t)(const struct ui_view *view);
typedef ui_tick_t (*ui_paint_func_t)(const struct ui_view *view, const gfx_ctx_t *ctx);
typedef void (*ui_event_func_t)(const struct ui_view *view, uint32_t event, void *arg);
typedef void (*ui_exit_func_t)(const struct ui_view *view);

typedef struct ui_view
{
    ui_init_func_t init;
    ui_enter_func_t enter;
    ui_exit_func_t exit;
    ui_event_func_t handle_event;
    ui_paint_func_t paint;
    void *user;
} ui_view_t;

uint32_t ui_attention_mask(void);
void ui_attention_clear(void);
void ui_set_view(const ui_view_t *view);
const ui_view_t *ui_get_current_view(void);
void ui_trigger_update(void);
void ui_init(void);
void ui_goto_view(const ui_view_t *v, bool back);
int ui_move_towards(int current, int target);
void ui_active(void);
int ui_scroll_accelerator(int dscroll, int range);

typedef void (*ui_confirm_cb_t)(bool conf, const void *user);
void ui_confirm(const char *title, const char *msg, ui_confirm_cb_t cb, const void *user);

void ui_popup(const char *title, const char *msg, const char *button);
