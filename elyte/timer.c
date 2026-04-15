#include "irq.h"
#include "timer.h"
#include "tick_timer_hal.h"

static tick_timer_t ttim;
static timer_t *timer_q;

static void insert_timer(timer_t *timer)
{
    timer->_next = 0;
    timer_t *prev_t = 0;
    timer_t *cur_t = timer_q;
    while (cur_t && cur_t->t_trigger <= timer->t_trigger)
    {
        prev_t = cur_t;
        cur_t = cur_t->_next;
    }
    if (prev_t == 0)
    {
        // first in list
        timer_q = timer;
        timer->_next = cur_t;
        tick_timer_set_alarm(&ttim, timer->t_trigger);
    }
    else
    {
        prev_t->_next = timer;
        timer->_next = cur_t;
    }
}

tick_t timer_now(void)
{
    return tick_timer_get_current(&ttim);
}

void timer_halt_ms(uint32_t ms)
{
    const uint64_t ticks_per_second = tick_timer_hal_get_frequency(&ttim);
    tick_t now = timer_now();
    tick_t then = now + ms * ticks_per_second / 1000ULL;
    while (timer_now() < then)
        ;
}

// override weak impl
void cpu_halt(uint32_t ms)
{
    timer_halt_ms(ms);
}

// override weak impl
void cpu_halt_us(uint32_t us)
{
    const uint64_t ticks_per_second = tick_timer_hal_get_frequency(&ttim);
    tick_t now = timer_now();
    tick_t then = now + us * ticks_per_second / 1000000ULL;
    while (timer_now() < then)
        ;
}

uint64_t timer_uptime_ms(void)
{
    const uint64_t ticks_per_second = tick_timer_hal_get_frequency(&ttim);
    return (timer_now() * 1000ULL) / ticks_per_second;
}

void timer_start(timer_t *timer, timer_callback_fn_t fn, tick_t delta_tick, timer_type_t type)
{
    timer->cb = fn;
    timer->t_delta = delta_tick;
    timer->type = type;
    timer->started = 1;
    tick_t now = tick_timer_get_current(&ttim);
    uint32_t primask = cpu_primask_save_and_disable();
    timer->t_trigger = now + delta_tick;
    insert_timer(timer);
    cpu_primask_restore(primask);
}

void timer_stop(timer_t *timer)
{
    uint32_t primask = cpu_primask_save_and_disable();
    timer->started = 0;
    timer_t *prev_t = 0;
    timer_t *cur_t = timer_q;
    while (cur_t && cur_t != timer)
    {
        prev_t = cur_t;
        cur_t = cur_t->_next;
    }
    if (cur_t == timer)
    {
        timer->started = 0;
        if (prev_t == 0)
        {
            timer_q = cur_t->_next;
            tick_timer_abort_alarm(&ttim);
            if (timer_q)
            {
                tick_timer_set_alarm(&ttim, timer_q->t_trigger);
            }
        }
        else
        {
            prev_t->_next = timer->_next;
        }
    }
    cpu_primask_restore(primask);
}

void timer_init(void)
{
    timer_q = 0;
    tick_timer_init(&ttim);
}

void tick_timer_on_alarm(tick_timer_t *tim)
{
    timer_t *t = timer_q;
    while (t && t->t_trigger <= tick_timer_get_current(&ttim))
    {
        timer_t *next = t->_next;
        int started = t->started;
        timer_q = next;
        if (t->type == TIMER_ONESHOT)
            t->started = 0;
        if (started && t->cb)
            t->cb(t);
        if (t->type == TIMER_REPETITIVE && started)
        {
            t->t_trigger += t->t_delta;
            insert_timer(t);
        }
        t = next;
    }
    if (timer_q)
        tick_timer_set_alarm(&ttim, timer_q->t_trigger);
}
