#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include "cli.h"
#include "flash_driver.h"
#include "minio.h"
#include "nvmtnvj.h"
#include "utils.h"

#define NVMTNVJ_BLOCKS 3
#define NVMTNVJ_SECTORS_PER_BLOCK 2
#define NVMTNVJ_TAG_SIZE 12

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
