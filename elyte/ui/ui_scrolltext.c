#include "ui_scrolltext.h"
#include "utils.h"
#include "minio.h"

#define SCROLLTEXT_SPACE 32
#define SCROLLTEXT_TICKS_TO_SCROLL 260
#define SCROLLTEXT_DX_Q4_PER_TICK 8

static void set_text(ui_scrolltext_t *st, const char *string)
{
    st->tick = 0;
    st->string = string;
    st->str_width = gfx_string_width(st->font, string);
}

void ui_scrolltext_init(ui_scrolltext_t *st, const char *string, const gfx_font_t *font, int x, int y, int width)
{
    st->font = font;
    st->x = x;
    st->y = y;
    st->width = width;
    set_text(st, string);
}

ui_tick_t ui_scrolltext_paint(ui_scrolltext_t *st, const gfx_ctx_t *ctx)
{
    gfx_ctx_t lctx = *ctx;
    lctx.clip.x0 = max_i32(st->x, ctx->clip.x0);
    lctx.clip.x1 = min_i32(st->x + st->width, ctx->clip.x1);
    if (st->str_width <= st->width)
    {
        gfx_string(&lctx, st->font, st->string, st->x, st->y, GFX_COL_SET);
        return UI_TICK_NEVER;
    }
    if (st->tick == 0)
        st->tick = ctx->tick;
    ui_tick_t dt = ctx->tick - st->tick;
    if (dt < SCROLLTEXT_TICKS_TO_SCROLL)
    {
        gfx_string(&lctx, st->font, st->string, st->x, st->y, GFX_COL_SET);
        return SCROLLTEXT_TICKS_TO_SCROLL - dt;
    }
    int dx = ((dt - SCROLLTEXT_TICKS_TO_SCROLL) * SCROLLTEXT_DX_Q4_PER_TICK) >> 4;
    gfx_string(&lctx, st->font, st->string, st->x - dx, st->y, GFX_COL_SET);
    gfx_string(&lctx, st->font, st->string, st->x - dx + st->str_width + SCROLLTEXT_SPACE, st->y, GFX_COL_SET);
    if (dt > (ui_tick_t)(SCROLLTEXT_TICKS_TO_SCROLL + ((st->str_width + SCROLLTEXT_SPACE) << 4) / SCROLLTEXT_DX_Q4_PER_TICK))
    {
        st->tick = ctx->tick;
    }
    return 0;
}

void ui_scrolltext_reset(ui_scrolltext_t *st)
{
    st->tick = 0;
}

void ui_scrolltext_set_text(ui_scrolltext_t *st, const char *string)
{
    set_text(st, string);
}

void ui_scrolltext_set_xy(ui_scrolltext_t *st, int x, int y)
{
    st->x = x;
    st->y = y;
}
