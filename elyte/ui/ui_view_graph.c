#include <stdbool.h>
#include <stddef.h>
#include "cli.h"
#include "input.h"
#include "minio.h"
#include "timer.h"
#include "ui.h"
#include "ui_views.h"

// #define TEST_SAMPLES

#define SETTINGS_TEMP_GRAPH_LEN_MAX 10
#define SETTING_TEMP_GRAPH_LENGTH 10

static struct
{
    int data[SETTINGS_TEMP_GRAPH_LEN_MAX];
    int min_data;
    int max_data;
    int last_data;
    uint32_t len;
    uint32_t wix;
    uint32_t max_len;
    int spl_sum;
    uint32_t spl_count;
    tick_t last_update_ts;
    uint32_t zoom_len;
    uint32_t now_s;
#ifdef TEST_SAMPLES
    timer_t test_timer;
#endif

} me;

static void graph_add(int spl);

static void reset(void)
{

    me.max_len = (uint32_t)SETTING_TEMP_GRAPH_LENGTH;
    me.min_data = me.max_data = me.len = 0;
}

static void test_sample(void)
{
    static int seed = 123;
    static int _spl = 0;
    static int target_spl = 100;
    int ds = (target_spl - _spl);
    if (ds / 5 == 0)
    {
        _spl += ds < 0 ? -1 : 1;
    }
    else
    {
        _spl = _spl + ds / 5;
    }
    seed = seed * 4294967291;
    if (abs_i32(ds) < 3 || abs_i32(seed % 100) < 5)
    {
        target_spl = (seed % 1000);
        if (target_spl < 0)
            target_spl = -target_spl;
    }
    graph_add(_spl);
}
#ifdef TEST_SAMPLES
static void test_timer_cb(timer_t *t)
{
    test_sample();
}
#endif
static void init(const ui_view_t *this)
{
    memset(&me, 0, sizeof(me));
    reset();
#ifdef TEST_SAMPLES
    timer_start(&me.test_timer, test_timer_cb, TIMER_MS_TO_TICKS(200), TIMER_REPETITIVE);
#endif
}

static void enter(const ui_view_t *this)
{
}

static void exit(const ui_view_t *this)
{
    me.zoom_len = 0;
}

static void graph_add(int spl)
{
    if (spl < -400 || spl > 10000)
        return;
    me.last_data = spl;
    if (me.wix >= me.max_len)
    {
        me.wix = 0;
    }
    me.len++;
    if (me.len >= me.max_len)
    {
        me.len = me.max_len;
    }
    me.data[me.wix] = spl;
    me.min_data = me.max_data = 0;
    for (uint32_t i = 0; i < me.len; i++)
    {
        uint32_t ix = i > me.wix ? (me.max_len - (i - me.wix)) : (me.wix - i);
        int v = me.data[ix];
        me.min_data = min_i32(me.min_data, v);
        me.max_data = max_i32(me.max_data, v);
    }
    me.wix++;
    if (ui_get_current_view() == &view_graph)
    {
        ui_trigger_update();
    }
}

void ui_view_graph_thermo(int temp)
{
    const tick_t now = timer_now();
    const tick_t update_period = TIMER_MS_TO_TICKS(1000 * 1);
    me.spl_sum += temp;
    me.spl_count++;
    const tick_t dt = now - me.last_update_ts;
    if (dt >= update_period)
    {
#ifndef TEST_SAMPLES
        graph_add(me.spl_sum / me.spl_count);
#endif
        me.spl_count = me.spl_sum = 0;
        if (me.last_update_ts + update_period < now)
        {
            // too few samples in period, do our best
            me.last_update_ts = now;
        }
        else
        {
            me.last_update_ts += update_period;
        }
    }
}

static void handle_event(const ui_view_t *this, uint32_t type, void *arg)
{
    switch (type)
    {
    case EVENT_UI_CLICK:
        switch ((input_button_t)arg)
        {
        case INPUT_BUTTON_BACK:
            ui_goto_view(&view_menu, true);
            break;
        case INPUT_BUTTON_ROTARY:
            if (me.zoom_len)
            {
                me.zoom_len = 0;
            }
            else
            {
                ui_goto_view(&view_menu, true);
            }
        default:
            break;
        }
        ui_trigger_update();
        break;
    case EVENT_UI_SCRL:
        if ((int)arg < 0)
        {
            if (me.zoom_len == 0)
                me.zoom_len = 19 * me.len / 20;
            else
                me.zoom_len = clamp_i32(100, me.zoom_len - me.len / 20, me.len);
        }
        else
        {
            if (me.zoom_len > 0)
                me.zoom_len = clamp_i32(100, me.zoom_len + me.len / 20, me.len);
            if (me.zoom_len > me.len)
                me.zoom_len = 0;
        }
        me.zoom_len += (int)arg;
        ui_trigger_update();
        break;
    case EVENT_THERMO:
        // not handled here as events are disabled for inactive views
        break;
    case EVENT_SECOND_TICK:
        me.now_s = (uint32_t)arg;
        break;
    default:
        break;
    }
}

static void string_outline(const gfx_ctx_t *ctx, const char *str, const gfx_font_t *font, int x, int y)
{
    gfx_string(ctx, font, str, x - 1, y, GFX_COL_CLR);
    gfx_string(ctx, font, str, x + 1, y, GFX_COL_CLR);
    gfx_string(ctx, font, str, x, y - 1, GFX_COL_CLR);
    gfx_string(ctx, font, str, x, y + 1, GFX_COL_CLR);
    gfx_string(ctx, font, str, x, y, GFX_COL_SET);
}

static ui_tick_t paint(const ui_view_t *this, const gfx_ctx_t *ctx)
{
    if (me.len == 0)
        return UI_TICK_NEVER;

    const int x0 = ctx->clip.x0;
    const int y0 = ctx->clip.y0;
    const uint32_t w = ctx->clip.x1 - x0;
    const uint32_t h = ctx->clip.y1 - y0 - 1;
    const uint32_t len = me.zoom_len == 0 ? me.len : (uint32_t)clamp_i32(100, me.zoom_len, me.len);
    uint32_t start_ix = (len > me.wix) ? (me.wix + me.max_len - len) : (me.wix - len);
    uint32_t range = me.max_data - me.min_data;
    int y_mark;
#define FACT 1
    if (range >= FACT * 25000)
        y_mark = 0;
    else if (range >= FACT * 10000)
        y_mark = 5000;
    else if (range >= FACT * 5000)
        y_mark = 2500;
    else if (range >= FACT * 2500)
        y_mark = 1000;
    else if (range >= FACT * 1000)
        y_mark = 500;
    else if (range >= FACT * 500)
        y_mark = 250;
    else if (range >= FACT * 250)
        y_mark = 100;
    else if (range >= FACT * 100)
        y_mark = 50;
    else if (range >= FACT * 50)
        y_mark = 25;
    else if (range >= FACT * 25)
        y_mark = 10;
    else
        y_mark = 5;
    uint32_t ky_q8 = (h << 8) / range;

    // paint origo
    const int y_origo = h - (((0 - me.min_data) * ky_q8 + 128) >> 8) + y0;
    for (uint32_t ox = 0; ox < w; ox += 2)
    {
        gfx_plot(ctx, ox, y_origo, GFX_COL_SET);
    }

    // paint graph
    int gy = h - (((me.data[start_ix] - me.min_data) * ky_q8 + 128) >> 8) + y0;
    if (len < w)
    {
        // more pixels than samples
        uint32_t dx_q16 = (w << 16) / len;
        uint32_t ix = start_ix;
        uint32_t x_q16 = x0 << 16;
        uint32_t l = 0;
        while (l++ < len)
        {
            int next_gy = h - (((me.data[ix] - me.min_data) * ky_q8 + 128) >> 8) + y0;
            int next_x_q16 = x_q16 + dx_q16;
            gfx_line(ctx, x_q16 >> 16, gy, next_x_q16 >> 16, next_gy, GFX_COL_SET);
            x_q16 = next_x_q16;
            gy = next_gy;
            ix++;
            if (ix >= me.max_len)
                ix = 0;
        }
    }
    else if (len < 5 * w)
    {
        // 1-5 samples per pixel
        uint32_t dix_q8 = (len << 8) / w;
        uint32_t ix_q8 = start_ix << 8;
        uint32_t x = x0;
        while (x < w + x0)
        {
            uint32_t n_spl_q8 = dix_q8;
            uint32_t spl_ix = ix_q8 >> 8;
            int32_t sum_spl_q8 = 0;

            if ((ix_q8 & 0xff) > 0)
            {
                sum_spl_q8 += me.data[spl_ix] * (0x100 - (ix_q8 & 0xff));
                n_spl_q8 -= (0x100 - (ix_q8 & 0xff));
                spl_ix++;
                if (spl_ix >= me.max_len)
                    spl_ix = 0;
            }
            while (n_spl_q8 >= 0x100)
            {
                sum_spl_q8 += me.data[spl_ix] << 8;
                n_spl_q8 -= 0x100;
                spl_ix++;
                if (spl_ix >= me.max_len)
                    spl_ix = 0;
            }
            if (n_spl_q8 > 0)
            {
                sum_spl_q8 += me.data[spl_ix] * n_spl_q8;
            }

            int spl_avg = sum_spl_q8 / (int32_t)dix_q8;

            int next_gy = h - (((spl_avg - me.min_data) * ky_q8 + 128) >> 8) + y0;
            gfx_line(ctx, x, gy, x + 1, next_gy, GFX_COL_SET);
            ix_q8 += dix_q8;
            if (ix_q8 >> 8 >= me.max_len)
                ix_q8 -= (me.max_len << 8);
            gy = next_gy;
            x++;
        }
    }
    else
    {
        // bunch of samples per pixel
        uint32_t dix_q8 = (len << 8) / w;
        uint32_t ix_q8 = start_ix << 8;
        uint32_t x = x0;
        while (x < w + x0)
        {
            uint32_t spl_ix = ix_q8 >> 8;
            int spl_min = INT32_MAX;
            int spl_max = INT32_MIN;
            // prefer a bit of overshoot here to bind curve together
            for (uint32_t i = ix_q8; i <= (ix_q8 + dix_q8 + 0x100); i += 0x100)
            {
                spl_min = min_i32(me.data[spl_ix], spl_min);
                spl_max = max_i32(me.data[spl_ix], spl_max);
                spl_ix++;
                if (spl_ix >= me.max_len)
                    spl_ix = 0;
                if (spl_ix == me.wix)
                    break;
            }

            int gy_max = h - (((spl_max - me.min_data) * ky_q8 + 128) >> 8) + y0;
            int gy_min = h - (((spl_min - me.min_data) * ky_q8 + 128) >> 8) + y0;
            gy = gy_min;
            gfx_vline(ctx, x, gy_min, gy_max, GFX_COL_SET);
            ix_q8 += dix_q8;
            if (ix_q8 >> 8 >= me.max_len)
                ix_q8 -= (me.max_len << 8);
            x++;
        }
    }

    // paint y markers
    char num[16];
    if (y_mark > 0)
    {
        for (int ty = (me.min_data / y_mark) * y_mark; ty < me.max_data; ty += y_mark)
        {
            const int g_ty = h - (((ty - me.min_data) * ky_q8 + 128) >> 8) + y0;
            for (uint32_t x = 0; x < w; x += 4)
            {
                gfx_plot(ctx, x, g_ty, GFX_COL_SET);
            }
            sprintf(num, "%d", ty);
            string_outline(ctx, num, UI_FONT_MINI, 0, g_ty - UI_FONT_MINI->max_height);
        }
    }

    // paint current and max temp
    sprintf(num, "%d", me.max_data);
    string_outline(ctx, num, UI_FONT_MINI, (DISP_W - gfx_string_width(UI_FONT_MINI, num)) / 2,
                   DISP_H - UI_FONT_MINI->max_height);
    sprintf(num, "%d", me.last_data);
    int y_cur_temp_y = gy >= DISP_H - UI_FONT_MINI->max_height ? (DISP_H - UI_FONT_MINI->max_height) / 2 : (DISP_H - UI_FONT_MINI->max_height);
    string_outline(ctx, num, UI_FONT_MINI, DISP_W - gfx_string_width(UI_FONT_MINI, num),
                   y_cur_temp_y);

    return UI_TICK_NEVER;
}

UI_DECLARE_VIEW view_graph = {
    .init = init,
    .enter = enter,
    .exit = exit,
    .handle_event = handle_event,
    .paint = paint,
};

static int cli_test_samples(int argc, const char **argv)
{
    int samples = argc == 0 ? 1 : strtol(argv[0], NULL, 0);
    for (int i = 0; i < samples; i++)
        test_sample();
    return 0;
}
CLI_FUNCTION(cli_test_samples, "test_spl", "");
