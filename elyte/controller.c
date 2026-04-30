#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "adc.h"
#include "assert.h"
#include "board.h"
#include "controller.h"
#include "dac.h"
#include "events.h"
#include "gpio_driver.h"
#include "gpio_driver.h"
#include "minio.h"
#include "timer.h"
#include "utils.h"

#define MIN_DAC_VAL 640
#define MAX_DAC_VAL 0xfff

#define SAMPLE_DELTA_MS 10 // this will be divided by 2 as we're sampling V & C alternating
#define AVG_BUF 8

// Max time to detect max current
// SAMPLE_DELTA_MS * 2 * GAIN_MAX = 80ms

typedef struct
{
    float buf[AVG_BUF];
    float sum;
    uint8_t len;
    uint8_t ix;
} avg_buffer_t;

static struct
{
    volatile bool panic;
    event_t ev_panic;
    event_t ev_status;
    status_info_t info;
    avg_buffer_t current;
    avg_buffer_t voltage;
    current_gain_t adc_current_gain;
    int32_t i_raw;
    int32_t v_raw;
    uint16_t dac;
    volatile uint16_t holdoff;
    struct
    {
        bool enabled;
        float curr;
        float volt;
    } set;
    dac_op_t dac_last_op;
    uint32_t dac_op_count;
} me;

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

static void adjust_dac(void)
{
    bool i_maxed_reading = me.i_raw > (int)(0.99f * ADC_RAW_MAX_VAL);
    if (me.adc_current_gain == GAIN_MIN && i_maxed_reading)
    {
        // current maxed => shorted, hold off instantly
        me.holdoff = 3;
        me.info.holdoff = me.holdoff;
        printf("SHORT\n");
        ctrl_set_dac(0);
        return;
    }

    if (me.holdoff > 0)
        return;

    if (me.set.curr == 0 || me.set.volt == 0)
        return;

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

    if (v_avg <= EPS && i_avg >= 0.003f && dac > MIN_DAC_VAL)
    {
        // zero voltage, but current => shorted, hold off instantly
        me.holdoff = 3;
        me.info.holdoff = me.holdoff;
        printf("SHORT\n");
        ctrl_set_dac(0);
        return;
    }

    if (dv_cur < -0.350f)
    {
        // instant voltage reading >= 0.35V too high, react instantly
        dac /= 2;
        me.dac_last_op = V_DEC;
    }
    else if (dv_avg < -0.100f)
    {
        // instant voltage reading >= 0.1V too high, react instantly
        dac -= 20;
        me.dac_last_op = V_DEC;
    }
    else if (di_cur < -0.100f)
    {
        // instant current reading >= 0.1A too high, react instantly
        dac = dac * 3 / 4;
        me.dac_last_op = I_DEC;
    }
    else if (dv_avg < 0)
    {
        // average voltage too high, lower DAC slowly
        dac--;
        me.dac_last_op = V_DEC;
    }
    else if (di_avg < -0.05f)
    {
        // average current too high, lower DAC slowly
        dac -= 10;
        me.dac_last_op = I_DEC;
    }
    else if (di_avg < 0)
    {
        // average current too high, lower DAC slowly
        dac--;
        me.dac_last_op = I_DEC;
    }
    else if (di_avg > 0.05f)
    {
        // average current much too low, raise DAC quickly (unless we just capped voltage)
        dac += last_op == V_DEC ? 1 : 25;
        me.dac_last_op = last_op == V_DEC ? V_DEC : I_INC;
    }
    else if (di_avg > 0.005f)
    {
        // average current pretty low, raise DAC quicklyish
        dac += last_op == V_DEC ? 1 : 10;
        me.dac_last_op = last_op == V_DEC ? V_DEC : I_INC;
    }
    else if (di_avg > 0)
    {
        // average current too low, raise DAC slowly
        dac++;
        me.dac_last_op = I_INC;
    }
    me.info.dac_op = me.dac_last_op;
    me.info.dac_op_count = me.dac_op_count;
    if (me.dac_last_op == I_DEC || me.dac_last_op == V_DEC)
    {
        me.info.dac_op_dec = me.dac_last_op;
        me.info.dac_op_dec_count = me.dac_op_count;
    }

    dac = clamp_i32(MIN_DAC_VAL, dac, MAX_DAC_VAL);

    ctrl_set_dac((uint16_t)dac);
    me.dac_op_count++;
}

static void ctrl_adc_cb(int res, adc_t adc, int32_t raw, float val)
{
    if (res)
        return;
    switch (adc)
    {
    case ADC_VOLTAGE:
        me.v_raw = raw;
        avg_buffer_add(&me.voltage, val);
        me.info.voltage_cur = val;
        me.info.voltage_avg = avg_buffer_get_avg(&me.voltage);
        break;

    case ADC_CURRENT:
    {
        me.i_raw = raw;
        avg_buffer_add(&me.current, val);
        me.info.current_cur = val;
        me.info.current_avg = avg_buffer_get_avg(&me.current);
        if (raw < ADC_RAW_MAX_VAL / 3)
            adc_current_gain_increase();
        else if (raw > 3 * ADC_RAW_MAX_VAL / 4)
            adc_current_gain_decrease();
        if (me.set.enabled)
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
}

void ctrl_stop(void)
{
}

void ctrl_init(void)
{
    int res;
    me.adc_current_gain = GAIN_MIN;
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

void ctrl_force_dac(uint16_t dac)
{
    me.set.enabled = false;
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
    me.set.enabled = true;
}

int32_t ctrl_get_current_ma(void)
{
    return (int32_t)(me.set.curr * 1000.f);
}

void ctrl_set_voltage_mv(int32_t volt)
{
    me.set.volt = (float)volt / 1000.f;
    me.set.enabled = true;
}
int32_t ctrl_get_voltage_mv(void)
{
    return (int32_t)(me.set.volt * 1000.f);
}

static void ctrl_event_handler(uint32_t type, void *arg)
{
    switch (type)
    {
    case EVENT_SECOND_TICK:
    {
        if (me.holdoff)
        {
            me.holdoff--;
            me.info.holdoff = me.holdoff;
            ctrl_set_dac(0);
        }
        printf("V:%s %d I:%s %d x%d\tDAC:%d\n", ftostr(me.info.voltage_avg), me.v_raw, ftostr(me.info.current_avg), me.i_raw, (1 << me.adc_current_gain), me.dac);
        event_add(&me.ev_status, EVENT_STATUS, &me.info);
    }
    break;
    default:
        break;
    }
}
EVENT_HANDLER(ctrl_event_handler);
