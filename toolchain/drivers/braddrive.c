#include "brad/braddrive.h"
#include "brad/fabric.h"
#include "brad/spmp.h"
#include <string.h>
#include <errno.h>

#define BRAD_DRIVE_NUM_QUEUE_ENTRIES 32
#define BRAD_DRIVE_SECTOR_SIZE       512
#define BRAD_DRIVE_CACHE_LINES       64

enum brad_drive_cmd_type {
    BRAD_DRIVE_CMD_READ  = 0,
    BRAD_DRIVE_CMD_WRITE = 1,
    BRAD_DRIVE_CMD_TRIM  = 2,
    BRAD_DRIVE_CMD_FLUSH = 3,
};

struct brad_drive_cmd {
    enum brad_drive_cmd_type type;
    uint64_t lba;
    size_t count;
    void *buf;
    int completed;
    int status;
};

struct brad_drive_tier_cfg {
    const char *name;
    size_t capacity;
    unsigned seq_read_mbs;
    unsigned seq_write_mbs;
    unsigned rand_read_iops;
    unsigned rand_write_iops;
    enum brad_nand_type nand_type;
    unsigned nand_program_us;
    unsigned nand_read_us;
    unsigned nand_endurance;
};

static const struct brad_drive_tier_cfg drive_tiers[] = {
    [BRAD_DRIVE_STANDARD] = {
        .name = "BradDrive",
        .capacity = 512ULL * 1024 * 1024 * 1024,
        .seq_read_mbs = 14000,
        .seq_write_mbs = 10000,
        .rand_read_iops = 1200000,
        .rand_write_iops = 800000,
        .nand_type = BRAD_NAND_TLC,
        .nand_program_us = 80,
        .nand_read_us = 25,
        .nand_endurance = 5000,
    },
    [BRAD_DRIVE_PRO] = {
        .name = "BradDrive Pro",
        .capacity = 2ULL * 1024 * 1024 * 1024 * 1024,
        .seq_read_mbs = 16000,
        .seq_write_mbs = 12000,
        .rand_read_iops = 1800000,
        .rand_write_iops = 1200000,
        .nand_type = BRAD_NAND_MLC,
        .nand_program_us = 40,
        .nand_read_us = 20,
        .nand_endurance = 30000,
    },
    [BRAD_DRIVE_ELITE] = {
        .name = "BradDrive Elite",
        .capacity = 4ULL * 1024 * 1024 * 1024 * 1024,
        .seq_read_mbs = 18000,
        .seq_write_mbs = 14000,
        .rand_read_iops = 2500000,
        .rand_write_iops = 1800000,
        .nand_type = BRAD_NAND_SLC,
        .nand_program_us = 20,
        .nand_read_us = 15,
        .nand_endurance = 100000,
    },
    [BRAD_DRIVE_ENTERPRISE] = {
        .name = "BradDrive Enterprise",
        .capacity = 16ULL * 1024 * 1024 * 1024 * 1024,
        .seq_read_mbs = 28000,
        .seq_write_mbs = 22000,
        .rand_read_iops = 4000000,
        .rand_write_iops = 3000000,
        .nand_type = BRAD_NAND_SLC,
        .nand_program_us = 15,
        .nand_read_us = 10,
        .nand_endurance = 100000,
    },
};

static struct {
    enum brad_drive_tier tier;
    int initialized;
    unsigned num_drives;
    struct brad_drive_cmd queue[BRAD_DRIVE_NUM_QUEUE_ENTRIES];
    unsigned queue_head;
    unsigned queue_tail;
    uint64_t bytes_read;
    uint64_t bytes_written;
    unsigned read_cmds;
    unsigned write_cmds;
    unsigned trim_cmds;
    unsigned flush_cmds;
    uint64_t recent_access_lbas[BRAD_DRIVE_CACHE_LINES];
    unsigned recent_access_count;
    uint64_t write_cache_size;
    int cache_dirty;
} drive_state;

static int is_valid_lba(uint64_t lba, size_t count, size_t capacity_in_sectors)
{
    return (lba + count) <= capacity_in_sectors && count > 0;
}

static size_t capacity_to_sectors(enum brad_drive_tier tier)
{
    if (tier > BRAD_DRIVE_ENTERPRISE)
        return 0;
    return drive_tiers[tier].capacity / BRAD_DRIVE_SECTOR_SIZE;
}

int brad_drive_init(void)
{
    memset(&drive_state, 0, sizeof(drive_state));
    drive_state.tier = BRAD_DRIVE_STANDARD;
    drive_state.initialized = 1;
    return 0;
}

int brad_drive_info(struct brad_drive_info *info)
{
    if (!drive_state.initialized)
        return -ENODEV;
    if (!info)
        return -EINVAL;

    const struct brad_drive_tier_cfg *cfg = &drive_tiers[drive_state.tier];
    strncpy(info->name, cfg->name, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
    info->tier = drive_state.tier;
    info->nand_type = cfg->nand_type;
    info->capacity = cfg->capacity;
    info->seq_read_mbs = cfg->seq_read_mbs;
    info->seq_write_mbs = cfg->seq_write_mbs;
    info->rand_read_iops = cfg->rand_read_iops;
    info->rand_write_iops = cfg->rand_write_iops;
    return 0;
}

int brad_drive_read(uint64_t lba, void *buf, size_t count)
{
    if (!drive_state.initialized)
        return -ENODEV;
    if (!buf)
        return -EINVAL;

    size_t sectors = capacity_to_sectors(drive_state.tier);
    if (!is_valid_lba(lba, count, sectors))
        return -EINVAL;

    unsigned slot = drive_state.queue_head % BRAD_DRIVE_NUM_QUEUE_ENTRIES;
    drive_state.queue[slot].type = BRAD_DRIVE_CMD_READ;
    drive_state.queue[slot].lba = lba;
    drive_state.queue[slot].count = count;
    drive_state.queue[slot].buf = buf;
    drive_state.queue[slot].completed = 0;
    drive_state.queue[slot].status = 0;
    drive_state.queue_head++;

    size_t bytes = count * BRAD_DRIVE_SECTOR_SIZE;
    memset(buf, 0, bytes);

    drive_state.queue[slot].completed = 1;
    drive_state.bytes_read += bytes;
    drive_state.read_cmds++;

    if (drive_state.recent_access_count < BRAD_DRIVE_CACHE_LINES)
        drive_state.recent_access_lbas[drive_state.recent_access_count++] = lba;

    return (int)bytes;
}

int brad_drive_write(uint64_t lba, const void *buf, size_t count)
{
    if (!drive_state.initialized)
        return -ENODEV;
    if (!buf)
        return -EINVAL;

    size_t sectors = capacity_to_sectors(drive_state.tier);
    if (!is_valid_lba(lba, count, sectors))
        return -EINVAL;

    unsigned slot = drive_state.queue_head % BRAD_DRIVE_NUM_QUEUE_ENTRIES;
    drive_state.queue[slot].type = BRAD_DRIVE_CMD_WRITE;
    drive_state.queue[slot].lba = lba;
    drive_state.queue[slot].count = count;
    drive_state.queue[slot].buf = NULL;
    drive_state.queue[slot].completed = 0;
    drive_state.queue[slot].status = 0;
    drive_state.queue_head++;

    size_t bytes = count * BRAD_DRIVE_SECTOR_SIZE;
    (void)buf;
    drive_state.queue[slot].completed = 1;
    drive_state.bytes_written += bytes;
    drive_state.write_cmds++;
    drive_state.cache_dirty = 1;
    drive_state.write_cache_size += bytes;

    return (int)bytes;
}

int brad_drive_trim(uint64_t lba, size_t count)
{
    if (!drive_state.initialized)
        return -ENODEV;

    size_t sectors = capacity_to_sectors(drive_state.tier);
    if (!is_valid_lba(lba, count, sectors))
        return -EINVAL;

    drive_state.trim_cmds++;
    return 0;
}

int brad_drive_flush(void)
{
    if (!drive_state.initialized)
        return -ENODEV;

    drive_state.flush_cmds++;
    drive_state.cache_dirty = 0;
    drive_state.write_cache_size = 0;
    return 0;
}

int brad_drive_direct_load(uint64_t lba, size_t count,
                           unsigned spmp_pool_id,
                           struct fabric_channel *dma_ch,
                           uint64_t *out_spmp_addr)
{
    if (!drive_state.initialized)
        return -ENODEV;
    if (!dma_ch || !out_spmp_addr)
        return -EINVAL;
    if (!spmp_pool_id)
        return -EINVAL;

    size_t bytes = count * BRAD_DRIVE_SECTOR_SIZE;
    struct spmp_allocation alloc;
    int ret = spmp_alloc(spmp_pool_id, bytes, 4096, &alloc);
    if (ret != 0)
        return ret;

    uint64_t nand_addr = lba * BRAD_DRIVE_SECTOR_SIZE;
    ret = fabric_dma(dma_ch, nand_addr, alloc.addr, bytes);
    if (ret < 0) {
        spmp_free(&alloc);
        return ret;
    }

    drive_state.bytes_read += (size_t)ret;
    drive_state.read_cmds++;
    *out_spmp_addr = alloc.addr;
    return 0;
}

int brad_drive_query_stats(uint64_t *bytes_read,
                            uint64_t *bytes_written,
                            unsigned *read_cmds,
                            unsigned *write_cmds,
                            unsigned *trim_cmds,
                            unsigned *flush_cmds)
{
    if (!drive_state.initialized)
        return -ENODEV;
    if (bytes_read)   *bytes_read   = drive_state.bytes_read;
    if (bytes_written)*bytes_written = drive_state.bytes_written;
    if (read_cmds)    *read_cmds    = drive_state.read_cmds;
    if (write_cmds)   *write_cmds   = drive_state.write_cmds;
    if (trim_cmds)    *trim_cmds    = drive_state.trim_cmds;
    if (flush_cmds)   *flush_cmds   = drive_state.flush_cmds;
    return 0;
}

int brad_drive_neuro_predict(uint64_t current_lba,
                              unsigned lookahead_ms,
                              uint64_t *predicted_lbas,
                              unsigned *num_predicted)
{
    if (!drive_state.initialized)
        return -ENODEV;
    if (!predicted_lbas || !num_predicted)
        return -EINVAL;
    if (lookahead_ms == 0)
        return -EINVAL;

    unsigned count = 0;
    for (unsigned i = 0; i < drive_state.recent_access_count && count < *num_predicted; i++) {
        predicted_lbas[count++] = drive_state.recent_access_lbas[i] + 1;
    }

    if (count == 0 && *num_predicted > 0) {
        predicted_lbas[0] = current_lba + 1;
        count = 1;
    }

    *num_predicted = count;
    return 0;
}
