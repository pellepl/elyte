#include <stddef.h>
#include <math.h>
#include "controller.h"
#include "cpu.h"
#include "cli.h"
#include "minio.h"
#include "timer.h"
#include "ui.h"
#include "ui_views.h"

static struct
{
    volatile bool inactivate;
    volatile bool activate;
    volatile bool disp_enabled;
    volatile bool ongoing_disp_command;
    volatile bool pending_disp_command;
    const ui_view_t *view_active;
    const ui_view_t *view_inactive;
    int anim_view_dx; // if <0 move to -DISP_W, if >0 move to +DISP_W
    ui_tick_t next_paint_update;
    event_t ev_scrl;
    event_t ev_press;
    event_t ev_back;
    event_t ev_repaint;
    timer_t timer_repaint;
    gfx_ctx_t ctx;
    uint32_t last_active_s;
    tick_t last_scroll_t;
    uint32_t abs_scroll_int;
    int32_t kiln_temp;
    uint32_t kiln_pow;
    uint32_t attention;
    bool keep_awake;
} me;

uint32_t ui_attention_mask(void)
{
    return me.attention;
}

static void repaint_timer_cb(timer_t *timer)
{
    event_add(&me.ev_repaint, EVENT_UI_DISP_UPDATE, NULL);
}

static void disp_command_done_cb(void)
{
    me.ongoing_disp_command = false;
    if (me.pending_disp_command)
    {
        me.pending_disp_command = false;
        event_add(&me.ev_repaint, EVENT_UI_DISP_UPDATE, NULL);
    }
}

static void disp_enable_done_cb(int err)
{
    disp_command_done_cb();
}

static void disp_update_done_cb(int err)
{
    disp_command_done_cb();
    if (me.next_paint_update == UI_TICK_NEVER)
    {
        return;
    }
    if (me.next_paint_update == 0)
    {
        event_add(&me.ev_repaint, EVENT_UI_DISP_UPDATE, NULL);
        return;
    }
    timer_start(&me.timer_repaint, repaint_timer_cb, me.next_paint_update * DISP_TRANSFER_TICKS, TIMER_ONESHOT);
}

static void ui_disp_update(void)
{
    me.ctx.tick = timer_now() / DISP_TRANSFER_TICKS;
    if (me.ongoing_disp_command)
    {
        me.pending_disp_command = true;
        return;
    }
    timer_stop(&me.timer_repaint);

    if (me.activate)
    {
        if (!me.disp_enabled)
        {
            printf("activate disp\n");
            me.ongoing_disp_command = true;
            me.pending_disp_command = true; // force another disp update after enabling it, in order to call current view update func
            disp_set_enabled(true, disp_enable_done_cb);
            me.disp_enabled = true;
            me.activate = false;
            if (me.view_active && me.view_active->enter)
            {
                me.view_active->enter(me.view_active);
            }
        }
        return;
    }
    else if (me.inactivate)
    {
        if (me.disp_enabled)
        {
            printf("inactivate disp\n");
            me.ongoing_disp_command = true;
            disp_set_enabled(false, disp_enable_done_cb);
            me.disp_enabled = false;
            me.inactivate = false;
            if (me.view_active && me.view_active->exit)
            {
                me.view_active->exit(me.view_active);
            }
        }
        return;
    }
    if (!me.disp_enabled)
        return;

    gfx_ctx_init(&me.ctx);
    ui_tick_t paint_update = UI_TICK_NEVER;
    if (me.view_inactive)
    {
        gfx_ctx_t lctx = me.ctx;
        gfx_ctx_move(&lctx, me.anim_view_dx, 0);
        ui_tick_t pu = me.view_inactive->paint(me.view_inactive, &lctx);
        if (pu < paint_update)
            paint_update = pu;
    }
    if (me.view_active)
    {
        gfx_ctx_t lctx = me.ctx;
        if (me.anim_view_dx != 0)
            gfx_ctx_move(&lctx, me.anim_view_dx < 0 ? (me.anim_view_dx + DISP_W) : (me.anim_view_dx - DISP_W), 0);
        ui_tick_t pu = me.view_active->paint(me.view_active, &lctx);
        if (pu < paint_update)
            paint_update = pu;
    }
    if (me.anim_view_dx != 0)
    {
        paint_update = 0;
        if (me.anim_view_dx < 0)
        {
            me.anim_view_dx = ui_move_towards(me.anim_view_dx, -DISP_W);
        }
        if (me.anim_view_dx > 0)
        {
            me.anim_view_dx = ui_move_towards(me.anim_view_dx, DISP_W);
        }
        if (me.anim_view_dx == DISP_W || me.anim_view_dx == -DISP_W)
        {
            me.anim_view_dx = 0;
            if (me.view_inactive && me.view_inactive->exit)
            {
                me.view_inactive->exit(me.view_inactive);
            }
            me.view_inactive = NULL;
        }
    }
    me.next_paint_update = paint_update;
    me.ongoing_disp_command = true;
    disp_screen_update(disp_update_done_cb);
}

void ui_trigger_update(void)
{
    event_add(&me.ev_repaint, EVENT_UI_DISP_UPDATE, NULL);
}

void ui_set_view(const ui_view_t *v)
{
    if (v == me.view_active)
        return;
    if (me.view_active && me.view_active->exit)
    {
        me.view_active->exit(me.view_active);
    }
    if (v->enter)
    {
        v->enter(v);
    }
    me.view_active = v;
    ui_trigger_update();
}

void ui_goto_view(const ui_view_t *v, bool back)
{
    if (v == me.view_active)
        return;
    if (v->enter)
    {
        v->enter(v);
    }
    me.view_inactive = me.view_active;
    me.view_active = v;
    me.anim_view_dx = ui_move_towards(0, back ? DISP_W : -DISP_W);
}

const ui_view_t *ui_get_current_view(void)
{
    return me.view_active;
}

int ui_move_towards(int current, int target)
{
#define DIV 8
    if (current == target)
        return target;
    int d = target - current;
    if (d < 0 && d > -DIV)
        return current - 1;
    if (d > 0 && d < DIV)
        return current + 1;
    return current + d / DIV;
}

static void ui_setting_change_cb(setting_t id, void *val, bool store)
{
    switch (id)
    {
    case SETTING_TEMP_GRAPH_UPDATE:
    case SETTING_TEMP_GRAPH_LENGTH:
        ui_view_graph_setting_change_cb(id, val, store);
        break;
    case SETTING_UI_KILN_TEMP:
    {
        me.kiln_temp = (int32_t)val;
        if (store)
        {
            ctrl_manual_set_temp(me.kiln_temp);
            ctrl_manual_set_power(me.kiln_pow * 10);
            int err = nvmtnv_write(id, (uint8_t *)&val, sizeof(me.kiln_temp));
            if (err < 0)
                printf("err storing last ui value %d:%d, %d\n", id, (int)val, err);
        }
    }
    break;
    case SETTING_UI_KILN_POWER:
    {
        me.kiln_pow = (int32_t)val;
        if (store)
        {
            ctrl_manual_set_temp(me.kiln_temp);
            ctrl_manual_set_power(me.kiln_pow * 10);
            int err = nvmtnv_write(id, (uint8_t *)&val, sizeof(me.kiln_pow));
            if (err < 0)
                printf("err storing last ui value %d:%d, %d\n", id, (int)val, err);
        }
    }
    break;
    case SETTING_KILN_MAX_TEMP:
        if (store && me.kiln_temp > (int32_t)val)
        {
            ui_setting_change_cb(SETTING_UI_KILN_TEMP, val, true);
        }
        break;
    case SETTING_PID_K_P:
        ctrl_pid_set_k_p((uint32_t)val);
        break;
    case SETTING_PID_K_I:
        ctrl_pid_set_k_i((uint32_t)val);
        break;
    case SETTING_PID_K_D:
        ctrl_pid_set_k_d((uint32_t)val);
        break;
    case SETTING_PID_EPSILON:
        ctrl_pid_set_epsilon((uint32_t)val);
        break;

    default:
        break;
    }
}
static const setting_def_t *ui_setting_get_cb(setting_t id)
{
    static setting_def_t ui_setting;
    switch (id)
    {
    case SETTING_UI_KILN_TEMP:
        ui_setting.id = SETTING_UI_KILN_TEMP;
        ui_setting.max_value = settings_get_val(SETTING_KILN_MAX_TEMP);
        ui_setting.min_value = 0;
        ui_setting.ui_name = "Target temp";
        ui_setting.unit = UNIT_DEGREE_C;
        break;
    case SETTING_UI_KILN_POWER:
        ui_setting.id = SETTING_UI_KILN_POWER;
        ui_setting.max_value = (void *)10;
        ui_setting.min_value = (void *)1;
        ui_setting.ui_name = "Kiln power";
        ui_setting.unit = UNIT_INTEGER;
        break;
    default:
        return NULL;
    }
    return &ui_setting;
}

static void *ui_setting_val_cb(setting_t id)
{
    switch (id)
    {
    case SETTING_UI_KILN_TEMP:
        return (void *)me.kiln_temp;
    case SETTING_UI_KILN_POWER:
        return (void *)me.kiln_pow;
    default:
        return 0;
    }
}

void ui_init(void)
{
    me.disp_enabled = true;

    const ui_view_t *view = UI_VIEWS_START;
    const ui_view_t *view_end = UI_VIEWS_END;

    if (nvmtnv_read(SETTING_UI_KILN_TEMP, (uint8_t *)&me.kiln_temp, sizeof(me.kiln_temp)) < 0)
    {
        me.kiln_temp = (uint32_t)settings_get_val(SETTING_KILN_MAX_TEMP) / 2;
    }
    if (nvmtnv_read(SETTING_UI_KILN_POWER, (uint8_t *)&me.kiln_pow, sizeof(me.kiln_pow)) < 0)
    {
        me.kiln_pow = 10;
    }
    me.attention = 0;
    settings_set_cb_fns(ui_setting_change_cb, ui_setting_get_cb, ui_setting_val_cb);

    while (view < view_end)
    {
        if (view->init)
            view->init(view);
        view++;
    }

    ui_set_view(&view_menu);
}

void ui_active(void)
{
    me.last_active_s = sys_get_up_second();
    if (!me.disp_enabled)
    {
        me.inactivate = false;
        me.activate = true;
        if (me.view_active && me.view_active->enter)
        {
            me.view_active->enter(me.view_active);
        }
        ui_trigger_update();
    }
}

void ui_attention_clear(void)
{
    me.attention &= ~ATT_PANIC;
}

int ui_scroll_accelerator(int dscroll, int range)
{
    int mul = 1;
    if (range == 0 || range > 1000)
        range = 1000;
    if (me.abs_scroll_int > 100)
    {
        mul = range / 10;
    }
    else if (me.abs_scroll_int > 10)
    {
        mul = range / 20;
    }
    else if (me.abs_scroll_int > 3)
    {
        mul = range / 100;
    }
    else if (me.abs_scroll_int > 1)
    {
        mul = range / 200;
    }
    return dscroll * mul;
}

static void ui_event(uint32_t type, void *arg)
{
    bool enable = false;
    bool was_disabled = !me.disp_enabled;
    enable = (type == EVENT_UI_CLICK || type == EVENT_UI_PRESSHOLD || type == EVENT_UI_SCRL);

    if (type == EVENT_ATTENTION)
    {
        uint32_t att_bit = (1 << ((uint32_t)arg));
        if ((me.attention & att_bit) == 0)
        {
            printf("ATTENTION: %d\n", (int)arg);
            me.attention |= att_bit;
            enable = true;
        }
    }
    if (enable)
    {
        ui_active();
    }
    if (type == EVENT_THERMO) {
        int t = (int)arg;
        ui_view_graph_thermo(t);
    }

    if (was_disabled)
        return;

    switch (type)
    {
    case EVENT_UI_CLICK:
        break;
    case EVENT_UI_PRESSHOLD:
        break;
    case EVENT_UI_SCRL:
    {
#define FACTOR 26
        tick_t now = timer_now();
        tick_t dt = (tick_t)sqrtf(now - me.last_scroll_t);
        me.last_scroll_t = now;
        me.abs_scroll_int = me.abs_scroll_int * FACTOR / dt;
        me.abs_scroll_int += abs((int)arg);
    }
    break;

    case EVENT_SECOND_TICK:
        if (me.disp_enabled)
        {
            uint32_t now_s = (uint32_t)arg;
            if (now_s - me.last_active_s > (uint32_t)settings_get_val(SETTING_SCREEN_TIMEOUT) && !me.keep_awake)
            {
                if (ctrl_is_enabled() || me.attention != 0)
                {
                    if (me.view_active != &view_sleep)
                    {
                        ui_set_view(&view_sleep);
                    }
                }
                else
                {
                    me.activate = false;
                    me.inactivate = true;
                }
                ui_trigger_update();
            }
        }
        break;
    case EVENT_UI_DISP_UPDATE:
        ui_disp_update();
        return;
    default:
        break;
    }
    if (me.view_active != NULL)
    {
        me.view_active->handle_event(me.view_active, type, arg);
    }
}
EVENT_HANDLER(ui_event);

static int cli_ui_ev_scrl(int argc, const char **argv)
{
    event_add(&me.ev_scrl, EVENT_UI_SCRL, (void *)(argc == 0 ? 1 : strtol(argv[0], NULL, 0)));
    return 0;
}
CLI_FUNCTION(cli_ui_ev_scrl, "ev_scrl", "");
static int cli_ui_ev_click(int argc, const char **argv)
{
    event_add(&me.ev_press, EVENT_UI_CLICK, (void *)(argc == 0 ? 0 : strtol(argv[0], NULL, 0)));
    return 0;
}
CLI_FUNCTION(cli_ui_ev_click, "ev_click", "");

static int cli_ui_keep_awake(int argc, const char **argv)
{
    me.keep_awake = strtol(argv[0], NULL, 0) != 0;
    return 0;
}
CLI_FUNCTION(cli_ui_keep_awake, "keep_awake", "");
