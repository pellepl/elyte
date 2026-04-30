#pragma once

#include <stdbool.h>
#include "bmtypes.h"

typedef enum
{
    IDLE,
    V_DEC,
    I_DEC,
    V_INC,
    I_INC,
} dac_op_t;

typedef struct {
    uint16_t dac;
    float current_avg;
    float voltage_avg;
    float current_cur;
    float voltage_cur;
    uint16_t holdoff;
    dac_op_t dac_op;
    dac_op_t dac_op_dec;
    uint32_t dac_op_count;
    uint32_t dac_op_dec_count;
} status_info_t;

void ctrl_init(void);
void ctrl_panic(void);
bool ctrl_is_panicking(void);
bool ctrl_is_enabled(void);
void ctrl_start(void);
void ctrl_stop(void);
void ctrl_set_current_ma(int32_t curr);
int32_t ctrl_get_current_ma(void);
void ctrl_set_voltage_mv(int32_t volt);
int32_t ctrl_get_voltage_mv(void);
void ctrl_set_dac(uint16_t dac);
void ctrl_force_dac(uint16_t dac);
void ctrl_request_status(status_info_t *dst);
