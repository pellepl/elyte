#include "cli.h"
#include "events.h"
#include "flash_driver.h"
#include "minio.h"
#include "nvmtnvj.h"
#include "settings.h"
#include "utils.h"

#define NVMTNVJ_BLOCKS 3
#define NVMTNVJ_SECTORS_PER_BLOCK 2
#define NVMTNVJ_TAG_SIZE 12

const setting_def_t defs[] = {
    [SETTING_SCREEN_ALIVE_S] = {.id = SETTING_SCREEN_ALIVE_S,
                                .name = "Screen alive",
                                .unit = "s",
                                .def = 60,
                                .max = 3600,
                                .min = 5,
                                .e = 0,
                                .tag = 1},
    [SETTING_CURR_CYCLE_PERIOD_S] = {.id = SETTING_CURR_CYCLE_PERIOD_S,
                                     .name = "Current cycle period",
                                     .descr = "Cycle current on/off within this amount of time. 0 to disable.",
                                     .unit = "s",
                                     .def = 0,
                                     .max = 300,
                                     .min = 0,
                                     .e = 0,
                                     .tag = 2},
    [SETTING_CURR_CYCLE_DUTY_S] = {.id = SETTING_CURR_CYCLE_DUTY_S,
                                   .name = "Current cycle duty",
                                   .descr = "During one current cycle, have current enabled this time.",
                                   .unit = "s",
                                   .def = 1,
                                   .max = 300,
                                   .min = 1,
                                   .e = 0,
                                   .tag = 3},
    [SETTING_CURR_CYCLE_LIMIT_MV] = {.id = SETTING_CURR_CYCLE_LIMIT_MV,
                                     .name = "Current cycle limit",
                                     .descr = "Start cycling current when voltage exceeds this value.",
                                     .unit = "mV",
                                     .def = 0,
                                     .max = 2500,
                                     .min = 0,
                                     .e = 0,
                                     .tag = 4},
    [SETTING_SERVO_DELTA_S] = {.id = SETTING_SERVO_DELTA_S,
                               .name = "Servo period",
                               .unit = "s",
                               .def = 0,
                               .max = 300,
                               .min = 0,
                               .e = 0,
                               .tag = 5},
};

static struct
{
    int32_t setting_vals[SETTING_COUNT];
    event_t ev_setting_change;
} me;

setting_t *setting_get(setting_id_t id, setting_t *s)
{
    if (id >= SETTING_COUNT)
        return NULL;
    s->def = &defs[id];
    s->value = me.setting_vals[id];
    return s;
}

void settings_init(void)
{
    for (size_t i = 0; i < ARRAY_LEN(defs); i++)
    {
        const setting_def_t *def = &defs[i];
        if (def->tag == 0)
        {
            me.setting_vals[def->id] = def->def;
            continue;
        }
        uint8_t data[NVMTNVJ_TAG_SIZE];
        int res = nvmtnvj_read(def->tag, data);
        if (res < 0)
        {
            me.setting_vals[def->id] = def->def;
            if (res != ERR_NVMTNVJ_NOENT)
                printf("ERROR: settings init %d\n", res);
        }
        else
        {
            int32_t v = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
            v = clamp_i32(def->min, v, def->max);
            me.setting_vals[def->id] = v;
        }
    }
}

float setting_get_val(setting_id_t id)
{
    static const float pow10[] = {
        1e-6f, 1e-5f, 1e-4f, 1e-3f, 1e-2f, 1e-1f,
        1e0f,
        1e1f, 1e2f, 1e3f, 1e4f, 1e5f, 1e6f};
    if (id >= SETTING_COUNT)
        return NAN;
    int8_t e_ix = defs[id].e + ARRAY_LEN(pow10) / 2;
    if (e_ix < 0 || e_ix >= (int8_t)ARRAY_LEN(pow10))
        return NAN;
    return (float)me.setting_vals[id] * pow10[e_ix];
}

int setting_set(setting_id_t id, int val)
{
    int err = 0;
    if (id >= SETTING_COUNT)
        return -1;
    val = clamp_i32(defs[id].min, val, defs[id].max);
    if (defs[id].tag)
    {
        uint8_t data[NVMTNVJ_TAG_SIZE];
        data[0] = val & 0xff;
        data[1] = (val >> 8) & 0xff;
        data[2] = (val >> 16) & 0xff;
        data[3] = (val >> 24) & 0xff;
        err = nvmtnvj_write(defs[id].tag, data, 4);
    }
    if (!err)
    {
        me.setting_vals[id] = val;
        event_add(&me.ev_setting_change, EVENT_SETTING_CHANGE, (void *)id);
    }
    return err;
}

static void persistence_init(void)
{
    int res;
    res = flash_init();
    ASSERT(res == 0);
    uint32_t starting_sector;
    uint32_t nbr_of_sectors;
    ASSERT(flash_get_sectors_for_type(FLASH_TYPE_CODE_BANK0, &starting_sector, &nbr_of_sectors) >= 0);
    const uint32_t nvmtnvj_sector = starting_sector + nbr_of_sectors - NVMTNVJ_BLOCKS * NVMTNVJ_SECTORS_PER_BLOCK;
    nvmtnvj_init();
    res = nvmtnvj_mount(nvmtnvj_sector, NVMTNVJ_SECTORS_PER_BLOCK + 1);
    if (res == ERR_NVMTNVJ_NOFS)
    {
        printf("formatting persistence\n");
        res = nvmtnvj_format(nvmtnvj_sector, NVMTNVJ_SECTORS_PER_BLOCK, NVMTNVJ_BLOCKS, NVMTNVJ_TAG_SIZE);
        ASSERT(res == 0);
        res = nvmtnvj_mount(nvmtnvj_sector, NVMTNVJ_SECTORS_PER_BLOCK + 1);
    }
    if (res == ERR_NVMTNVJ_FS_ABORTED)
    {
        printf("fixing aborted persistence\n");
        res = nvmtnvj_fix();
    }
    if (res != 0)
        printf("persistence error:%d\n", res);
}

static void printf_tag_id(uint16_t id)
{
    uint8_t data[NVMTNVJ_TAG_SIZE];
    int res = nvmtnvj_read(id, data);
    if (res == ERR_NVMTNVJ_NOENT)
    {
        printf("%04x\t<n/a>\n", id);
    }
    else if (res >= 0)
    {
        printf("%04x\t", id);
        for (int i = 0; i < res; i++)
        {
            printf("%02x", data[i]);
        }
        printf("\n");
    }
    else
    {
        printf("%04x\tERROR %d\n", id, res);
    }
}

static int cli_persistence_init(int argc, const char **argv)
{
    persistence_init();
    return 0;
}
CLI_FUNCTION(cli_persistence_init, "pers_init", ": init");

static int cli_persistence_read(int argc, const char **argv)
{
    for (int i = 0; i < argc; i++)
    {
        printf_tag_id(strtol(argv[i], NULL, 0));
    }
    return 0;
}
CLI_FUNCTION(cli_persistence_read, "pers_read", "(id)*: read tag(s)");

static int cli_persistence_write(int argc, const char **argv)
{
    if (argc == 0)
        return ERR_CLI_EINVAL;
    uint16_t tag_id = strtol(argv[0], NULL, 0);
    uint8_t data[NVMTNVJ_TAG_SIZE];
    int len = argc - 1 > NVMTNVJ_TAG_SIZE ? NVMTNVJ_TAG_SIZE : (argc - 1);
    for (int i = 0; i < len; i++)
    {
        data[i] = strtol(argv[i + 1], NULL, 0);
    }
    return nvmtnvj_write(tag_id, data, len);
}
CLI_FUNCTION(cli_persistence_write, "pers_write", "id (data)*: write tag");

static int cli_persistence_delete(int argc, const char **argv)
{
    if (argc == 0)
        return ERR_CLI_EINVAL;
    for (int i = 0; i < argc; i++)
    {
        uint16_t tag_id = strtol(argv[i], NULL, 0);
        int res = nvmtnvj_delete(tag_id);
        if (res < 0)
            return res;
    }
    return 0;
}
CLI_FUNCTION(cli_persistence_delete, "pers_delete", "id*: delete tag(s)");

static int cli_persistence_format(int argc, const char **argv)
{
    uint32_t starting_sector;
    uint32_t nbr_of_sectors;
    ASSERT(flash_get_sectors_for_type(FLASH_TYPE_CODE_BANK0, &starting_sector, &nbr_of_sectors) >= 0);
    const uint32_t nvmtnvj_sector = starting_sector + nbr_of_sectors - NVMTNVJ_BLOCKS * NVMTNVJ_SECTORS_PER_BLOCK;
    return nvmtnvj_format(nvmtnvj_sector, NVMTNVJ_SECTORS_PER_BLOCK, NVMTNVJ_BLOCKS, NVMTNVJ_TAG_SIZE);
}
CLI_FUNCTION(cli_persistence_format, "pers_format", ":formats persistence");
