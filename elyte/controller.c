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
    volatile float last;
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
} me;

static void avg_buffer_add(avg_buffer_t *b, float v)
{
    b->last = v;
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

static float avg_buffer_get_last(avg_buffer_t *b)
{
    return b->len == 0 ? 0 : b->last;
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

static void ctrl_adc_cb(int res, adc_t adc, int32_t raw, float val)
{
    if (res)
        return;
    switch (adc)
    {
    case ADC_VOLTAGE:
        me.v_raw = raw;
        avg_buffer_add(&me.voltage, val);
        break;
    case ADC_CURRENT:
    {
        me.i_raw = raw;
        bool maxed_reading = raw > (int)(0.99f * ADC_RAW_MAX_VAL);
        if (me.adc_current_gain == GAIN_MIN && maxed_reading)
        {
            // current maxed, hold off instantly
            ctrl_set_dac(0);
        }
        if (!maxed_reading)
            avg_buffer_add(&me.current, val);
        if (raw < ADC_RAW_MAX_VAL / 3)
            adc_current_gain_increase();
        else if (raw > 3 * ADC_RAW_MAX_VAL / 4)
            adc_current_gain_decrease();
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
    me.info.dac = dac;
    event_add(&me.ev_status, EVENT_STATUS, &me.info);
}

const status_info_t *ctrl_request_status(void)
{
    me.info.voltage_avg = avg_buffer_get_avg(&me.voltage);
    me.info.current_avg = avg_buffer_get_avg(&me.current);
    me.info.voltage_cur = avg_buffer_get_last(&me.voltage);
    me.info.current_cur = avg_buffer_get_last(&me.current);
    return &me.info;
}

static void ctrl_event_handler(uint32_t type, void *arg)
{
    switch (type)
    {
    case EVENT_SECOND_TICK:
    {
        float v_avg = avg_buffer_get_avg(&me.voltage);
        float i_avg = avg_buffer_get_avg(&me.current);
        printf("V:%s %d I:%s %d x%d\n", ftostr(v_avg), me.v_raw, ftostr(i_avg), me.i_raw, (1 << me.adc_current_gain));
        if (v_avg != me.info.voltage_avg || i_avg != me.info.current_avg)
        {
            me.info.voltage_avg = v_avg;
            me.info.current_avg = i_avg;
            me.info.voltage_cur = avg_buffer_get_last(&me.voltage);
            me.info.current_cur = avg_buffer_get_last(&me.current);

            event_add(&me.ev_status, EVENT_STATUS, &me.info);
        }
    }
    break;
    default:
        break;
    }
}
EVENT_HANDLER(ctrl_event_handler);
