#ifndef BRAD_FABRIC_H
#define BRAD_FABRIC_H

#include <stdint.h>
#include <stddef.h>

#define FABRIC_MAX_NODES         64
#define FABRIC_MAX_CHANNELS      16
#define FABRIC_PACKET_MAX_SIZE   4096

enum fabric_node_type {
    FABRIC_NODE_CPU      = 0,
    FABRIC_NODE_GPU      = 1,
    FABRIC_NODE_NPU      = 2,
    FABRIC_NODE_MEM      = 3,
    FABRIC_NODE_RT       = 4,
    FABRIC_NODE_IO       = 5,
    FABRIC_NODE_HSM      = 6,
};

enum fabric_channel_flags {
    FABRIC_CH_READ       = (1U << 0),
    FABRIC_CH_WRITE      = (1U << 1),
    FABRIC_CH_DMA        = (1U << 2),
    FABRIC_CH_ATOMIC     = (1U << 3),
    FABRIC_CH_CACHE_COH  = (1U << 4),
    FABRIC_CH_ISO        = (1U << 5),
};

struct fabric_node_info {
    unsigned                 id;
    enum fabric_node_type    type;
    uint64_t                 address_base;
    uint64_t                 address_limit;
    unsigned                 bandwidth_gbs;
    unsigned                 latency_ns;
};

struct fabric_channel {
    unsigned                 src_node;
    unsigned                 dst_node;
    uint32_t                 flags;
    size_t                   mtu;
    uint64_t                 bytes_tx;
    uint64_t                 bytes_rx;
};

int  fabric_init(void);
int  fabric_register_node(struct fabric_node_info *info);

/* Attach host memory as a node's address-space window (starting at
 * the node's address_base).  Enables real byte movement for send /
 * DMA / atomics.  len must fit inside the node's declared window. */
int  fabric_bind_memory(unsigned node, void *mem, size_t len);

int  fabric_channel_open(unsigned src, unsigned dst,
                         uint32_t flags,
                         struct fabric_channel *ch);
int  fabric_channel_close(struct fabric_channel *ch);
int  fabric_send(struct fabric_channel *ch,
                 const void *data, size_t len);
int  fabric_recv(struct fabric_channel *ch,
                 void *buf, size_t *len);
int  fabric_dma(struct fabric_channel *ch,
                uint64_t src_addr, uint64_t dst_addr,
                size_t len);
int  fabric_atomic_add(struct fabric_channel *ch,
                       uint64_t addr, uint64_t val);
int  fabric_atomic_cas(struct fabric_channel *ch,
                       uint64_t addr,
                       uint64_t expected,
                       uint64_t desired,
                       uint64_t *prev);
int  fabric_atomic_exch(struct fabric_channel *ch,
                        uint64_t addr, uint64_t val, uint64_t *prev);
int  fabric_query_bandwidth(unsigned src, unsigned dst,
                            uint64_t *bw_gbs);
int  fabric_query_latency(unsigned src, unsigned dst,
                          unsigned *lat_ns);

#endif
