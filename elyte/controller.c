#include <float.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "adc.h"
#include "assert.h"
#include "board.h"
#include "cli.h"
#include "controller.h"
#include "dac.h"
#include "events.h"
#include "gpio_driver.h"
#include "gpio_driver.h"
#include "irq.h"
#include "minio.h"
#include "settings.h"
#include "timer.h"
#include "utils.h"

#define HOLDOFF_DISCONNECT_S 1
#define HOLDOFF_SHORT_S 3

#define MIN_DAC_VAL 640
#define MAX_DAC_VAL 0xfff

#define SAMPLE_DELTA_MS 10 // this will be divided by 2 as we're sampling V & C alternating
#define AVG_BUF 8

#define VDEC_OP_COUNT 25 // number of consecutive voltage-decrease DAC ops to consider voltage as the main issue when deciding how to increase DAC

// Max time to detect max current
// SAMPLE_DELTA_MS * 2 * GAIN_MAX = 80ms

typedef struct
{
    float buf[AVG_BUF];
    float sum;
    uint8_t len;
    uint8_t ix;
} avg_buffer_t;

typedef struct
{
    float max;
    float min;
    float sum;
    int samples;
} monitored_value_t;

typedef union
{
    struct
    {
        bool electrode_disconnect : 1;
        bool electrode_short : 1;
        bool no_dac_period : 1;
    };
    uint32_t flags_all;
} flags_t;

static struct
{
    volatile bool panic;
    volatile bool enabled;
    event_t ev_panic;
    event_t ev_status;
    status_info_t info;
    avg_buffer_t current;
    avg_buffer_t voltage;
    current_gain_t adc_current_gain;
    int32_t i_raw;
    int32_t v_raw;
    uint16_t dac;
    volatile uint32_t holdoff;
    volatile uint64_t uptime_s;
    volatile flags_t flags;
    uint64_t start_s;
    uint64_t disconnect_holdoff_ts;
    uint64_t short_holdoff_ts;
    bool calibration;
    struct
    {
        bool enabled;
        float curr;
        float volt;
    } set;
    dac_op_t dac_last_op;
    uint32_t dac_op_count;
    uint32_t vdec_op_count;
    float short_mV_at_10_mA;
    volatile bool log_enabled;
    struct
    {
        uint32_t ix;
        monitored_value_t mv;
        monitored_value_t ma;
        flags_t flags;
    } second_report;

    struct
    {
        uint8_t alert;
    } debug;
} me;

static void monitored_value_reset(monitored_value_t *v)
{
    v->max = -FLT_MAX;
    v->min = FLT_MAX;
    v->sum = 0.f;
    v->samples = 0;
}

static void monitored_value_register(monitored_value_t *v, float val)
{
    if (v->max < val)
        v->max = val;
    if (v->min > val)
        v->min = val;
    v->sum += val;
    v->samples++;
}

static float monitored_value_avg(monitored_value_t *v)
{
    if (v->samples == 0)
        return NAN;
    return v->sum / (float)v->samples;
}

static void avg_buffer_add(avg_buffer_t *b, float v)
{
    if (b->ix >= AVG_BUF)
        b->ix = 0;
    if (b->len >= AVG_BUF)
        b->sum -= b->buf[b->ix];
    else
        b->len++;
    b->buf[b->ix] = v;
    b->sum += v;
    b->ix++;
}

static float avg_buffer_get_avg(avg_buffer_t *b)
{
    if (b->len == 0)
        return 0;
    return b->sum / (float)b->len;
}

static void adc_current_gain_increase(void)
{
    if (me.adc_current_gain >= GAIN_MAX)
        return;
    me.adc_current_gain++;
    adc_adjust_gain_continuous(me.adc_current_gain);
}

static void adc_current_gain_decrease(void)
{
    if (me.adc_current_gain <= GAIN_MIN)
        return;
    me.adc_current_gain--;
    adc_adjust_gain_continuous(me.adc_current_gain);
}

static void signal_short(uint32_t holdoff_s)
{
    me.second_report.flags.electrode_short = true;
    me.flags.electrode_short = true;
    me.short_holdoff_ts = me.uptime_s + holdoff_s;
    me.holdoff = holdoff_s;
    me.info.holdoff = me.holdoff;
}

static void signal_disconnect(uint32_t holdoff_s)
{
    me.second_report.flags.electrode_disconnect = true;
    me.flags.electrode_disconnect = true;
    me.disconnect_holdoff_ts = me.uptime_s + holdoff_s;
    me.holdoff = holdoff_s;
    me.info.holdoff = me.holdoff;
}

static void signal_period(void)
{
    me.second_report.flags.no_dac_period = true;
    me.flags.no_dac_period = true;
}

static void adjust_dac(void)
{
    me.info.dac_off = true;
    if (!me.enabled)
        return;
    bool i_maxed_reading = me.i_raw > (int)(0.99f * ADC_RAW_MAX_VAL);
    if (me.adc_current_gain == GAIN_MIN && i_maxed_reading)
    {
        // current maxed => shorted, hold off instantly
        ctrl_set_dac(0);
        signal_short(HOLDOFF_SHORT_S);
        return;
    }

    if (me.set.curr == 0 || me.set.volt == 0)
    {
        ctrl_set_dac(0);
        return;
    }

    uint32_t now_s = (uint32_t)me.uptime_s;
    dac_op_t last_op = me.dac_last_op;

    int32_t dac = (int32_t)me.dac;
    float v_avg = me.info.voltage_avg;
    float i_avg = me.info.current_avg;
    float v_cur = me.info.voltage_cur;
    float i_cur = me.info.current_cur;
    float dv_avg = me.set.volt - v_avg;
    float dv_cur = me.set.volt - v_cur;
    float di_avg = me.set.curr - i_avg;
    float di_cur = me.set.curr - i_cur;
    const float EPS = 0.0005f;
    if (abs_f(dv_cur) < EPS)
        dv_cur = 0.f;
    if (abs_f(dv_avg) < EPS)
        dv_avg = 0.f;
    if (abs_f(di_cur) < EPS)
        di_cur = 0.f;
    if (abs_f(di_avg) < EPS)
        di_avg = 0.f;

    // check short
    if (v_avg <= me.short_mV_at_10_mA && i_avg >= 0.01f && dac > MIN_DAC_VAL)
    {
        // zero voltage, but current => shorted, hold off instantly
        ctrl_set_dac(0);
        signal_short(HOLDOFF_SHORT_S);
        return;
    }

    if (me.short_holdoff_ts < now_s)
        me.flags.electrode_short = false;

    // check disconnect
    if (v_cur > 2.5f && dv_cur < -0.350f && dac == MIN_DAC_VAL)
    {
        ctrl_set_dac(0);
        signal_disconnect(HOLDOFF_DISCONNECT_S);
        return;
    }

    if (me.disconnect_holdoff_ts < now_s)
        me.flags.electrode_disconnect = false;

    bool dac_disable = false;

    // check periodic
    const uint32_t cycle_s = (uint32_t)setting_get_val(SETTING_CURR_CYCLE_PERIOD_S);
    if (cycle_s > 0 && !me.flags.electrode_disconnect && !me.flags.electrode_short && !me.calibration)
    {
        const uint32_t duty_s = (uint32_t)setting_get_val(SETTING_CURR_CYCLE_DUTY_S);
        float mv_limit = setting_get_val(SETTING_CURR_CYCLE_LIMIT_MV);
        if ((now_s % cycle_s) != 0)
        {
            if ((v_avg * 1000.f >= mv_limit && (now_s % cycle_s) >= duty_s) || me.flags.no_dac_period)
            {
                dac_disable = true;
                signal_period();
            }
        }
    }

    dac_disable |= me.flags.electrode_disconnect;
    dac_disable |= me.flags.electrode_short;
    dac_disable |= me.flags.no_dac_period;
    dac_disable |= me.holdoff > 0;

    if (!dac_disable)
    {
        bool log = me.log_enabled;
        if (dv_cur < -0.350f)
        {
            // instant voltage reading >= 0.35V too high, react instantly
            if (log)
                printf("DAC %d/2: V_CUR >= 0.35V TOO HIGH\n", dac);
            dac /= 2;
            me.dac_last_op = V_DEC;
        }
        else if (dv_avg < -0.100f)
        {
            // instant voltage reading >= 0.1V too high, react instantly
            if (log)
                printf("DAC %d-20: V_AVG >= 0.1V TOO HIGH\n", dac);
            dac -= 20;
            me.dac_last_op = V_DEC;
        }
        else if (di_cur < -0.100f)
        {
            // instant current reading >= 0.1A too high, react instantly
            if (log)
                printf("DAC %d*3/4: I_CUR >= 0.1A TOO HIGH\n", dac);
            dac = dac * 3 / 4;
            me.dac_last_op = I_DEC;
        }
        else if (dv_avg < 0)
        {
            // average voltage too high, lower DAC slowly
            if (log)
                printf("DAC %d-1: V_AVG > 0 TOO HIGH\n", dac);
            dac--;
            me.dac_last_op = V_DEC;
        }
        else if (di_avg < -0.05f)
        {
            // average current too high, lower DAC slowly
            if (log)
                printf("DAC %d-10: I_AVG >= 50mA TOO HIGH\n", dac);
            dac -= 10;
            me.dac_last_op = I_DEC;
        }
        else if (di_avg < 0)
        {
            // average current too high, lower DAC slowly
            if (log)
                printf("DAC %d-1: I_AVG > 0 TOO HIGH\n", dac);
            dac--;
            me.dac_last_op = I_DEC;
        }
        else if (di_avg > 0.05f)
        {
            // average current much too low, raise DAC quickly (unless we just capped voltage)
            if (log)
                printf("DAC %d+1/25: I_AVG >= 50mA TOO LOW\n", dac);
            dac += last_op == V_DEC ? 1 : 25;
            me.dac_last_op = last_op == V_DEC && me.vdec_op_count < VDEC_OP_COUNT ? V_DEC : I_INC;
        }
        else if (di_avg > 0.005f)
        {
            // average current pretty low, raise DAC quicklyish
            if (log)
                printf("DAC %d+1/10: I_AVG >= 5mA TOO LOW\n", dac);
            dac += last_op == V_DEC ? 1 : 10;
            me.dac_last_op = last_op == V_DEC && me.vdec_op_count < VDEC_OP_COUNT ? V_DEC : I_INC;
        }
        else if (di_avg > 0)
        {
            // average current too low, raise DAC slowly
            if (log)
                printf("DAC %d+1: I_AVG TOO LOW\n", dac);
            dac++;
            me.dac_last_op = I_INC;
        }

        if (me.dac_last_op == V_DEC)
        {
            if (me.vdec_op_count < VDEC_OP_COUNT)
                me.vdec_op_count++;
        }
        else
        {
            me.vdec_op_count = 0;
        }
        me.info.dac_op = me.dac_last_op;
        me.info.dac_op_count = me.dac_op_count;
        if (me.dac_last_op == I_DEC || me.dac_last_op == V_DEC)
        {
            me.info.dac_op_dec = me.dac_last_op;
            me.info.dac_op_dec_count = me.dac_op_count;
        }

        dac = clamp_i32(MIN_DAC_VAL, dac, MAX_DAC_VAL);
        me.dac_op_count++;
    }
    else
    {
        dac = 0;
    }

    me.info.dac_off = false;
    ctrl_set_dac((uint16_t)dac);
}

static void ctrl_adc_cb(int res, adc_t adc, int32_t raw, float val)
{
    if (res)
        return;
    switch (adc)
    {
    case ADC_VOLTAGE:
        me.v_raw = raw;
        monitored_value_register(&me.second_report.mv, val * 1000.f);
        avg_buffer_add(&me.voltage, val);
        me.info.voltage_cur = val;
        me.info.voltage_avg = avg_buffer_get_avg(&me.voltage);
        break;

    case ADC_CURRENT:
    {
        me.i_raw = raw;
        monitored_value_register(&me.second_report.ma, val * 1000.f);
        avg_buffer_add(&me.current, val);
        me.info.current_cur = val;
        me.info.current_avg = avg_buffer_get_avg(&me.current);
        if (raw < ADC_RAW_MAX_VAL / 3)
            adc_current_gain_increase();
        else if (raw > 3 * ADC_RAW_MAX_VAL / 4)
            adc_current_gain_decrease();
        adjust_dac();
    }
    break;

    case ADC_VDDA:
        printf("VDDA %s %d\n", ftostr(val), raw);
        break;
    }
}

void ctrl_start(void)
{
    me.short_mV_at_10_mA = setting_get_val(SETTING_SHORT_MV_AT_10_MA);
    if (!me.enabled)
    {
        me.start_s = timer_uptime_ms() / 1000;
        me.uptime_s = 0;
        monitored_value_reset(&me.second_report.ma);
        monitored_value_reset(&me.second_report.mv);
        me.second_report.ix = 0;
        me.second_report.flags.flags_all = 0;
    }
    me.enabled = true;
}

void ctrl_stop(void)
{
    me.enabled = false;
}

void ctrl_init(void)
{
    int res;
    me.short_mV_at_10_mA = setting_get_val(SETTING_SHORT_MV_AT_10_MA);
    me.adc_current_gain = GAIN_MIN;
    me.enabled = true;
    adc_adjust_gain_continuous(me.adc_current_gain);
    res = adc_read_vdda(ctrl_adc_cb);
    if (res)
        printf("ERR CTRL read vdda %d\n", res);
    res = adc_read_continuous(ctrl_adc_cb, TIMER_MS_TO_TICKS(SAMPLE_DELTA_MS));
    if (res)
        printf("ERR CTRL read I & V %d\n", res);
}

void ctrl_panic(void)
{
    if (!me.panic)
    {
        me.panic = true;
        event_add(&me.ev_panic, EVENT_ATTENTION, NULL);
    }
}

bool ctrl_is_panicking(void)
{
    return me.panic;
}

void ctrl_set_dac(uint16_t dac)
{
    dac_set(dac);
    me.dac = dac;
    me.info.dac = dac;
    event_add(&me.ev_status, EVENT_STATUS, &me.info);
}

void ctrl_request_status(status_info_t *dst)
{
    *dst = me.info;
}

void ctrl_set_current_ma(int32_t curr)
{
    me.set.curr = (float)curr / 1000.f;
}

int32_t ctrl_get_current_ma(void)
{
    return (int32_t)(me.set.curr * 1000.f);
}

void ctrl_set_voltage_mv(int32_t volt)
{
    me.set.volt = (float)volt / 1000.f;
}

int32_t ctrl_get_voltage_mv(void)
{
    return (int32_t)(me.set.volt * 1000.f);
}

bool ctrl_is_alert(void)
{
    return me.flags.electrode_disconnect || me.flags.electrode_short || me.flags.no_dac_period || me.debug.alert > 0;
}

bool ctrl_is_alert_serious(void)
{
    return me.flags.electrode_disconnect || me.flags.electrode_short || me.debug.alert > 1;
}

static void output_second_report(uint32_t holdoff_s)
{

    uint32_t primask = cpu_primask_save_and_disable();
    bool dac_off = (me.info.dac == 0);
    flags_t flags = me.second_report.flags;
    monitored_value_t m_ma = me.second_report.ma;
    monitored_value_t m_mv = me.second_report.mv;
    me.second_report.flags.flags_all = 0;
    monitored_value_reset(&me.second_report.ma);
    monitored_value_reset(&me.second_report.mv);
    cpu_primask_restore(primask);
    float ma_avg = monitored_value_avg(&m_ma);
    float mv_avg = monitored_value_avg(&m_mv);
    printf("%08d ", me.second_report.ix);
    if (dac_off)
        printf("V:0.0>0.0>0.0 [%s] ", ftostr1(me.set.volt * 1000.f));
    else
        printf("V:%s>%s>%s [%s] ", ftostr1(m_mv.min), ftostr1(mv_avg), ftostr1(m_mv.max), ftostr1(me.set.volt * 1000.f));
    printf("I:%s>%s>%s [%s] ", ftostr1(m_ma.min), ftostr1(ma_avg), ftostr1(m_ma.max), ftostr1(me.set.curr * 1000.f));
    printf("DAC:%4d ", me.dac);
    if (holdoff_s)
        printf("OFF:%ds ", holdoff_s);
    if (flags.electrode_disconnect)
        printf("DIS ");
    if (flags.electrode_short)
        printf("SHO ");
    if (flags.no_dac_period)
        printf("PER[%s] ", ftostr1(setting_get_val(SETTING_CURR_CYCLE_LIMIT_MV)));
    printf("\n");
    me.second_report.ix++;
}

static void ctrl_event_handler(uint32_t type, void *arg)
{
    switch (type)
    {
    case EVENT_SECOND_TICK:
    {
        me.uptime_s++;
        const uint32_t now_s = (uint32_t)me.uptime_s;
        uint32_t old_holdoff = me.holdoff;
        if (me.holdoff)
            me.holdoff--;
        if (me.disconnect_holdoff_ts > now_s)
            me.holdoff = max_u32(me.holdoff, (uint32_t)(me.disconnect_holdoff_ts - now_s));
        if (me.short_holdoff_ts > now_s)
            me.holdoff = max_u32(me.holdoff, (uint32_t)(me.short_holdoff_ts - now_s));
        me.info.holdoff = me.holdoff;

        const uint32_t cycle_s = (uint32_t)setting_get_val(SETTING_CURR_CYCLE_PERIOD_S);
        if (cycle_s > 0 && (now_s % cycle_s) == 0)
        {
            me.flags.no_dac_period = false;
        }
        me.second_report.flags.no_dac_period = me.flags.no_dac_period;
        output_second_report(old_holdoff);
        event_add(&me.ev_status, EVENT_STATUS, &me.info);
    }
    break;
    case EVENT_SETTING_CHANGE:
        me.short_mV_at_10_mA = setting_get_val(SETTING_SHORT_MV_AT_10_MA);
        break;
    case EVENT_CALIBRATION:
        me.calibration = (bool)(uintptr_t)arg;
    default:
        break;
    }
}
EVENT_HANDLER(ctrl_event_handler);

static int cli_ctrl_log(int argc, const char **argv)
{
    if (argc > 0)
    {
        me.log_enabled = argv[0][0] == '1';
    }
    printf("CTRL LOG: %s\n", me.log_enabled ? "ON" : "OFF");
    return 0;
}
CLI_FUNCTION(cli_ctrl_log, "ctrl_log", "(0|1): log each dac adjustment")

static int cli_ctrl_alert(int argc, const char **argv)
{
    if (argc > 0)
    {
        me.debug.alert = argv[0][0] - '0';
    }
    printf("CTRL ALERT: %d\n", me.debug.alert);
    return 0;
}
CLI_FUNCTION(cli_ctrl_alert, "ctrl_alert", "(0|1|2): set alert level")
