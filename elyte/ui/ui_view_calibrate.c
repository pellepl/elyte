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
        STATE_CALIB_START,
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
    event_t ev_calibration;
    bool calibration_success;
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

static void fit_and_store(void)
{
    float k_i = 0.f, m_i = 0.f;
    float k_v = 0.f, m_v = 0.f;

    float i_local_ma[MAX_MEASURES];
    float v_local_mv[MAX_MEASURES];
    float i_real_ma[MAX_MEASURES];
    float v_real_mv[MAX_MEASURES];

    for (size_t i = 0; i < MAX_MEASURES; ++i)
    {
        i_local_ma[i] = me.measures[i].i_local_ma;
        v_local_mv[i] = me.measures[i].v_local_mv;
        i_real_ma[i] = me.measures[i].v_real_mv * 1000.f / me.resistance_mohm;
        v_real_mv[i] = me.measures[i].v_real_mv;
    }

    if (!linear_fit(v_local_mv, v_real_mv, MAX_MEASURES, &k_v, &m_v))
    {
        printf("ERROR: linear fit failed for voltage\n");
        return;
    }

    if (!linear_fit(i_local_ma, i_real_ma, MAX_MEASURES, &k_i, &m_i))
    {
        printf("ERROR: linear fit failed for current\n");
        return;
    }

    printf("CALIB: I_K=%s I_M=%s V_K=%s V_M=%s\n", ftostr1(k_i), ftostr1(m_i), ftostr1(k_v), ftostr1(m_v));
    const setting_id_t ids[] = {
        SETTING_PRIVATE_CALIB_I_K,
        SETTING_PRIVATE_CALIB_I_M,
        SETTING_PRIVATE_CALIB_V_K,
        SETTING_PRIVATE_CALIB_V_M,
    };
    // Gains and current offset use thousandths; voltage offset is stored
    // in millivolts (setting_get_val converts it to volts).
    const float values[] = {k_i * 1000.f, m_i * 1000.f, k_v * 1000.f, m_v};
    int32_t stored_values[ARRAY_LEN(ids)];

    // Validate the entire fit before writing: setting_set silently clamps.
    for (size_t i = 0; i < ARRAY_LEN(ids); ++i)
    {
        setting_t setting;
        if (!setting_get(ids[i], &setting) || !isfinite(values[i]) ||
            values[i] < setting.def->min || values[i] > setting.def->max)
        {
            printf("ERROR: calibration setting %d out of range\n", (int)ids[i]);
            return;
        }
        stored_values[i] = (int32_t)roundf(values[i]);
    }

    for (size_t i = 0; i < ARRAY_LEN(ids); ++i)
    {
        int res = setting_set(ids[i], stored_values[i]);
        if (res != 0)
        {
            printf("ERROR: calibration setting %d write failed %d\n", (int)ids[i], res);
            return;
        }
    }
    me.calibration_success = true;
}

static void init(const ui_view_t *this)
{
    me.title_w = gfx_string_width(UI_FONT_NORMAL, TITLE);
    ui_scrolltext_init(&me.desc_scrl, DESCRIPTION, UI_FONT_SMALL, 0, MSG_Y, DISP_W);
    me.state = STATE_CALIB_START;
    me.calibration_success = false;
}

static void kill_power(void)
{
    ctrl_set_current_ma(0);
    ctrl_set_voltage_mv(0);
    ctrl_set_dac(0);
}

static void calib_enter(void)
{
    printf("CALIB ENTER\n");
    me.state = STATE_CONNECT_RESISTOR;
    me.calibration_success = false;
    me.old_ma = ctrl_get_current_ma();
    me.old_mv = ctrl_get_voltage_mv();
    kill_power();
    me.cur_meas_ix = 0;
    event_add(&me.ev_calibration, EVENT_CALIBRATION, (void *)true);
}

static void calib_exit(void)
{
    ctrl_set_current_ma(me.old_ma);
    ctrl_set_voltage_mv(me.old_mv);
    ctrl_set_dac(0);
    me.state = STATE_CALIB_START;
    event_add(&me.ev_calibration, EVENT_CALIBRATION, (void *)false);
    ui_goto_view(&view_main, true);
    printf("CALIB EXIT\n");
}

static void enter(const ui_view_t *this)
{
    ui_scrolltext_reset(&me.desc_scrl);
    if (me.state == STATE_CALIB_START)
    {
        calib_enter();
    }
}

static void exit(const ui_view_t *this)
{
}

static void calib_next_amp(void)
{
    me.active_measure.count = 0;
    me.active_measure.sliding_current_ma = 0.f;
    me.active_measure.target_current_ma = round_nearest_f((float)(me.cur_meas_ix + 1) * me.max_current_ma / (float)MAX_MEASURES);
    printf("CALIB target I=%d ma\n", (int32_t)me.active_measure.target_current_ma);
    ctrl_set_current_ma((int32_t)me.active_measure.target_current_ma);
    me.state = STATE_SET_AMP;
    ui_trigger_update();
}

static void calib_begin(void)
{
    float r_ohm = setting_get_val(SETTING_PRIVATE_PROFILE_RESISTANCE);
    printf("CALIB R=%s ohm\n", ftostr1(r_ohm));
    float max_current_under_voltage_ma = ((float)MAX_VOLTAGE_MV / r_ohm);
    float max_current_under_power_ma = sqrtf((float)MAX_CALIBRATION_RESISTOR_POWER_MW / 1000.f / r_ohm) * 1000.f;
    // cap to a quarter of max to stay within heating limits and safe area
    me.max_current_ma = 0.25f * min_f(max_current_under_voltage_ma, max_current_under_power_ma);
    printf("CALIB max I=%s ma\n", ftostr1(me.max_current_ma));
    ctrl_set_voltage_mv(MAX_VOLTAGE_MV);
    calib_next_amp();
}

static void setting_cb(setting_id_t id, bool conf, int value)
{
    if (!conf)
    {
        if (id == SETTING_PRIVATE_PROFILE_VOLTAGE) {
            kill_power();
            me.state = STATE_CONNECT_RESISTOR;
        }
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
        m->v_real_mv = setting_get_val(SETTING_PRIVATE_PROFILE_VOLTAGE) * 1000.f;
        me.cur_meas_ix++;
        if (me.cur_meas_ix >= MAX_MEASURES)
        {

            kill_power();
            fit_and_store();
            me.state = STATE_DONE;
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
        me.state = STATE_CALIB_START;
        calib_exit();
        break;
    default:
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
            setting_set(SETTING_PRIVATE_PROFILE_VOLTAGE, (int32_t)round_nearest_f(m->v_local_mv));
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
    int y = MSG_Y;
    switch (me.state)
    {
    case STATE_CONNECT_RESISTOR:
        t = ui_scrolltext_paint(&me.desc_scrl, ctx);
        break;
    case STATE_SET_AMP:
        sprintf(str, "%d/%d", me.cur_meas_ix + 1, MAX_MEASURES);
        int cw = gfx_string_width(UI_FONT_SMALL, str);
        gfx_string(ctx, UI_FONT_SMALL, str, (DISP_W - cw) / 2, y, GFX_COL_SET);

        y += UI_FONT_SMALL->max_height + 4;
        sprintf(str, "%s", ftostr1(me.info.current_avg * 1000.f));
        gfx_string(ctx, UI_FONT_NORMAL, str, 4, y, GFX_COL_SET);
        sprintf(str, "%s", ftostr1(me.active_measure.target_current_ma));
        gfx_string(ctx, UI_FONT_NORMAL, str, DISP_W / 2, y, GFX_COL_SET);
        break;
    case STATE_REQUEST_VOLTAGE:
        break;
    case STATE_CALIB_START:
    case STATE_DONE:
        gfx_string(ctx, UI_FONT_BIG, me.calibration_success ? "DONE" : "FAIL", 4, y, GFX_COL_SET); // TODO PETER
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
