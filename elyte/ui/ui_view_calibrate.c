#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include "cli.h"
#include "controller.h"
#include "input.h"
#include "minio.h"
#include "ui.h"
#include "ui_scrolltext.h"
#include "ui_views.h"

#define TITLE "CALIBRATE"
#define MSG_Y (DISP_H / 4 + 6)
#define DESCRIPTION "Connect a known resistor and press button. Longpress to abort."

#define MAX_MEASURES 7

typedef struct
{
    float i_local_ma;
    float v_local_mv;
    float r_real_mohm;
    float v_real_mv;
} measure_t;

static struct
{
    int32_t old_mv;
    int32_t old_ma;
    int title_w;
    ui_scrolltext_t desc_scrl;
    enum
    {
        STATE_CONNECT_RESISTOR,
        STATE_SET_AMP,
        STATE_REQUEST_VOLTAGE,
        STATE_DONE
    } state;
    float max_current_ma;
    struct
    {
        float target_current_ma;
        uint32_t count;
        float sliding_current_ma;
    } active_measure;
    float resistance_mohm;
    measure_t measures[MAX_MEASURES];
    uint8_t cur_meas_ix;
    status_info_t info;

} me;

bool linear_fit(
    const float *x,
    const float *y,
    size_t num,
    float *k,
    float *m)
{
    if (!x || !y || !k || !m || num < 2)
        return false;

    float mean_x = 0.0f;
    float mean_y = 0.0f;

    for (size_t i = 0; i < num; ++i)
    {
        if (!isfinite(x[i]) || !isfinite(y[i]))
            return false;

        mean_x += x[i];
        mean_y += y[i];
    }

    mean_x /= (float)num;
    mean_y /= (float)num;

    float sxx = 0.0f;
    float sxy = 0.0f;

    for (size_t i = 0; i < num; ++i)
    {
        const float dx = x[i] - mean_x;
        const float dy = y[i] - mean_y;

        sxx += dx * dx;
        sxy += dx * dy;
    }

    if (sxx == 0.0f)
        return false;

    *k = sxy / sxx;
    *m = mean_y - (*k * mean_x);

    return isfinite(*k) && isfinite(*m);
}

static void init(const ui_view_t *this)
{
    me.title_w = gfx_string_width(UI_FONT_NORMAL, TITLE);
    ui_scrolltext_init(&me.desc_scrl, DESCRIPTION, UI_FONT_SMALL, 0, MSG_Y, DISP_W);
    me.state = STATE_CONNECT_RESISTOR;
}

static void kill_power(void)
{
    ctrl_set_current_ma(0);
    ctrl_set_voltage_mv(0);
    ctrl_set_dac(0);
}

static void calib_enter(void)
{
    me.old_ma = ctrl_get_current_ma();
    me.old_mv = ctrl_get_voltage_mv();
    kill_power();
    me.cur_meas_ix = 0;
}

static void calib_exit(void)
{
    ctrl_set_current_ma(me.old_ma);
    ctrl_set_voltage_mv(me.old_mv);
    ctrl_set_dac(0);
    me.state = STATE_CONNECT_RESISTOR;
    ui_goto_view(&view_main, false);
}

static void enter(const ui_view_t *this)
{
    ui_scrolltext_reset(&me.desc_scrl);
    if (me.state == STATE_CONNECT_RESISTOR)
        calib_enter();
}

static void exit(const ui_view_t *this)
{
}

static void calib_next_amp(void)
{
    me.active_measure.count = 0;
    me.active_measure.sliding_current_ma = 0.f;
    me.active_measure.target_current_ma = round_nearest_f((float)(me.cur_meas_ix + 1) * me.max_current_ma / (float)MAX_MEASURES);
    ctrl_set_current_ma((int32_t)me.active_measure.target_current_ma);
    me.state = STATE_SET_AMP;
    ui_trigger_update();
}

static void calib_begin(void)
{
    float r_ohm = setting_get_val(SETTING_PRIVATE_PROFILE_RESISTANCE);
    float max_current_under_voltage_ma = ((float)MAX_VOLTAGE_MV / r_ohm);
    float max_current_under_power_ma = sqrtf((float)MAX_CALIBRATION_RESISTOR_POWER_MW / 1000.f / r_ohm) * 1000.f;
    // cap to a quarter of max to stay within heating limits and safe area
    me.max_current_ma = 0.25f * min_f(max_current_under_voltage_ma, max_current_under_power_ma);
    ctrl_set_voltage_mv(MAX_VOLTAGE_MV);
    calib_next_amp();
}

static void setting_cb(setting_id_t id, bool conf, int value)
{
    if (!conf)
    {
        if (id == SETTING_PRIVATE_PROFILE_VOLTAGE)
            me.state = STATE_CONNECT_RESISTOR;
        return;
    }
    setting_set(id, value);
    if (id == SETTING_PRIVATE_PROFILE_RESISTANCE)
    {
        me.resistance_mohm = 1000.f * setting_get_val(SETTING_PRIVATE_PROFILE_RESISTANCE);
        calib_begin();
    }
    else if (id == SETTING_PRIVATE_PROFILE_VOLTAGE)
    {
        measure_t *m = &me.measures[me.cur_meas_ix];
        m->v_real_mv = setting_get_val(SETTING_PRIVATE_PROFILE_VOLTAGE);
        me.cur_meas_ix++;
        if (me.cur_meas_ix >= MAX_MEASURES)
        {

            kill_power();
            me.state = STATE_DONE;
            // TODO PETER: some linear fitting and storing to persistence here
        }
        else
        {
            calib_next_amp();
        }
    }
}

static void on_click(void)
{
    switch (me.state)
    {
    case STATE_CONNECT_RESISTOR:
        ui_setting_change(SETTING_PRIVATE_PROFILE_RESISTANCE, setting_cb, &view_calibrate);
        break;
    case STATE_SET_AMP:
        kill_power();
        me.state = STATE_CONNECT_RESISTOR;
        break;
    case STATE_REQUEST_VOLTAGE:
        break;
    case STATE_DONE:
        calib_exit();
        break;
    }
}

static void on_info(void)
{
    if (me.state != STATE_SET_AMP)
        return;

    float i_now_ma = me.info.current_avg * 1000.f;
    float di_ma = abs_f(me.active_measure.target_current_ma - i_now_ma);
    // allow from 0.2ma to 2ma or 10% of target current difference, whatever is smallest
    float max_di_ma = min_f(2.f, me.active_measure.target_current_ma * 0.1f);
    max_di_ma = max_f(max_di_ma, 0.2f);
    if (di_ma <= max_di_ma)
    {
        if (me.active_measure.count == 0)
            me.active_measure.sliding_current_ma = i_now_ma;
        else
            me.active_measure.sliding_current_ma = 0.9f * me.active_measure.sliding_current_ma + 0.1f * i_now_ma;
        me.active_measure.count++;
        if (me.active_measure.count >= 500)
        {
            // assume stable enuf
            measure_t *m = &me.measures[me.cur_meas_ix];
            m->i_local_ma = i_now_ma;
            m->r_real_mohm = me.resistance_mohm;
            m->v_local_mv = me.info.voltage_avg * 1000.f;
            me.state = STATE_REQUEST_VOLTAGE;
            ui_setting_change(SETTING_PRIVATE_PROFILE_VOLTAGE, setting_cb, &view_calibrate);
        }
    }
    else
    {
        me.active_measure.count = 0;
    }
    ui_trigger_update();
}

static void handle_event(const ui_view_t *this, uint32_t type, void *arg)
{
    switch (type)
    {
    case EVENT_UI_PRESSHOLD:
        calib_exit();
        ui_trigger_update();
        break;
    case EVENT_STATUS:
        me.info = *((status_info_t *)arg);
        on_info();
        break;
    case EVENT_UI_CLICK:
        on_click();
        ui_trigger_update();
        break;
    case EVENT_UI_SCRL:
        break;
    default:
        break;
    }
}

static ui_tick_t paint(const ui_view_t *this, const gfx_ctx_t *ctx)
{
    char str[32];
    ui_tick_t t = UI_TICK_NEVER;
    gfx_string(ctx, UI_FONT_NORMAL, TITLE, DISP_W / 2 - me.title_w / 2, 0, GFX_COL_SET);
    switch (me.state)
    {
    case STATE_CONNECT_RESISTOR:
        t = ui_scrolltext_paint(&me.desc_scrl, ctx);
        break;
    case STATE_SET_AMP:
        sprintf(str, "%s", ftostr1(me.info.current_avg * 1000.f));
        gfx_string(ctx, UI_FONT_BIG, str, 4, MSG_Y, GFX_COL_SET);
        sprintf(str, "%s", ftostr1(me.active_measure.target_current_ma));
        gfx_string(ctx, UI_FONT_BIG, str, 4, MSG_Y + 4 + UI_FONT_BIG->max_height, GFX_COL_SET);
        break;
    case STATE_REQUEST_VOLTAGE:
        break;
    case STATE_DONE:
        gfx_string(ctx, UI_FONT_BIG, "DONE", 4, MSG_Y, GFX_COL_SET); // TODO PETER
        break;
    }
    return t;
}

UI_DECLARE_VIEW view_calibrate = {
    .init = init,
    .enter = enter,
    .exit = exit,
    .handle_event = handle_event,
    .paint = paint,
    .name = "CAL",
};
