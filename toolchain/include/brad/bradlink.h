#ifndef BRAD_LINK_H
#define BRAD_LINK_H

#include <stdint.h>
#include <stddef.h>

#define BRAD_LINK_MAX_DEVICES     16
#define BRAD_LINK_MAX_NAME        64
#define BRAD_LINK_FABRIC_60GHZ    0
#define BRAD_LINK_FABRIC_BRIDGE   1
#define BRAD_LINK_MAX_CHANNELS    8

enum brad_link_device_type {
    BRAD_LINK_PHONE     = 0,
    BRAD_LINK_SLATE     = 1,
    BRAD_LINK_BOOK      = 2,
    BRAD_LINK_DESKTOP   = 3,
    BRAD_LINK_CONSOLE   = 4,
    BRAD_LINK_SERVER    = 5,
};

enum brad_link_capability {
    BRAD_LINK_CAP_AI     = (1U << 0),
    BRAD_LINK_CAP_RENDER = (1U << 1),
    BRAD_LINK_CAP_STORAGE = (1U << 2),
    BRAD_LINK_CAP_NET    = (1U << 3),
};

struct brad_link_device_info {
    char                          name[BRAD_LINK_MAX_NAME];
    enum brad_link_device_type    type;
    uint32_t                      capabilities;
    uint64_t                      total_mem;
    uint64_t                      free_mem;
    unsigned                      ncc_count;
    unsigned                      vec_count;
    unsigned                      fabric_bandwidth_mbs;
};

struct brad_link_channel {
    int                           active;
    uint32_t                      device_id;
    enum brad_link_capability     capability;
    uint64_t                      context_token;
};

int  brad_link_init(void);
int  brad_link_scan(struct brad_link_device_info *devices,
                    unsigned *count);
int  brad_link_tether(unsigned device_id,
                      enum brad_link_capability capability);
int  brad_link_untether(unsigned device_id);
int  brad_link_open_channel(uint32_t device_id,
                             enum brad_link_capability cap,
                             struct brad_link_channel *ch);
int  brad_link_close_channel(struct brad_link_channel *ch);
int  brad_link_offload_ai(const void *model,
                          const void *input,
                          void *output,
                          size_t in_size,
                          size_t out_size,
                          struct brad_link_channel *ch);
int  brad_link_sync_context(struct brad_link_channel *ch,
                            uint64_t context_id);
int  brad_link_discover(struct brad_link_device_info **infos,
                        unsigned *count);

#endif
