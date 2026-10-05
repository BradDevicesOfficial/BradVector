#ifndef BRAD_DRIVE_H
#define BRAD_DRIVE_H

#include <stdint.h>
#include <stddef.h>

struct fabric_channel;

#define BRAD_DRIVE_MAX_TIERS   5
#define BRAD_DRIVE_MAX_NAME    64

enum brad_drive_tier {
    BRAD_DRIVE_STANDARD   = 0,
    BRAD_DRIVE_PRO        = 1,
    BRAD_DRIVE_ELITE      = 2,
    BRAD_DRIVE_ENTERPRISE = 3,
};

enum brad_nand_type {
    BRAD_NAND_SLC = 0,
    BRAD_NAND_MLC = 1,
    BRAD_NAND_TLC = 2,
    BRAD_NAND_QLC = 3,
};

struct brad_drive_info {
    char                  name[BRAD_DRIVE_MAX_NAME];
    enum brad_drive_tier  tier;
    enum brad_nand_type   nand_type;
    size_t                capacity;
    unsigned              seq_read_mbs;
    unsigned              seq_write_mbs;
    unsigned              rand_read_iops;
    unsigned              rand_write_iops;
};

int  brad_drive_init(void);
int  brad_drive_info(struct brad_drive_info *info);
int  brad_drive_read(uint64_t lba, void *buf, size_t count);
int  brad_drive_write(uint64_t lba, const void *buf, size_t count);
int  brad_drive_trim(uint64_t lba, size_t count);
int  brad_drive_flush(void);
int  brad_drive_direct_load(uint64_t lba, size_t count,
                             unsigned spmp_pool_id,
                             struct fabric_channel *dma_ch,
                             uint64_t *out_spmp_addr);
int  brad_drive_query_stats(uint64_t *bytes_read,
                             uint64_t *bytes_written,
                             unsigned *read_cmds,
                             unsigned *write_cmds,
                             unsigned *trim_cmds,
                             unsigned *flush_cmds);
int  brad_drive_neuro_predict(uint64_t current_lba,
                               unsigned lookahead_ms,
                               uint64_t *predicted_lbas,
                               unsigned *num_predicted);

#endif
