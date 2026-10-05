#include "brad/fabric.h"
#include <string.h>
#include <errno.h>

/* ─── BradFusion Fabric (reference implementation) ───
 *
 * Models the mesh interconnect: registered node topology, message
 * channels, DMA between bound address spaces, and hardware atomics.
 * Distinct from a network sink: it moves real bytes across the node
 * graph and reports path bandwidth/latency from node capabilities.
 *
 * Reference model, not a claim of shipped hardware behavior.
 */

#define FABRIC_QUEUE_DEPTH   32
#define FABRIC_BACKING       4096

struct fabric_queue {
    uint8_t  data[FABRIC_QUEUE_DEPTH * FABRIC_BACKING];
    size_t   head;
    size_t   tail;
    size_t   used;
};

struct fabric_node {
    struct fabric_node_info info;
    int   registered;
    uint8_t *mem;                 /* guest-managed backing storage, optional */
    size_t   mem_len;
};

static struct {
    struct fabric_node nodes[FABRIC_MAX_NODES];
    unsigned num_nodes;
    int initialized;

    struct fabric_queue   q;
    volatile int          dma_lock;      /* spinlock: 0 = free, 1 = held */
    unsigned              dma_bytes;
    unsigned              atomic_ops;
    uint64_t              total_latency_ns;
} fabric_state;

static int node_valid(unsigned id)
{
    return id < fabric_state.num_nodes && fabric_state.nodes[id].registered;
}

static uint8_t *fabric_translate(unsigned node, uint64_t addr);

static int region_valid(unsigned node, uint64_t addr, size_t len)
{
    const struct fabric_node *n = &fabric_state.nodes[node];
    if (addr < n->info.address_base)
        return 0;
    if (addr + len > n->info.address_limit + 1)
        return 0;
    return 1;
}

int fabric_init(void)
{
    memset(&fabric_state, 0, sizeof(fabric_state));
    fabric_state.initialized = 1;
    fabric_state.dma_lock = 0;
    return 0;
}

int fabric_register_node(struct fabric_node_info *info)
{
    if (!fabric_state.initialized)
        return -ENODEV;
    if (!info)
        return -EINVAL;
    if (fabric_state.num_nodes >= FABRIC_MAX_NODES)
        return -ENOSPC;

    for (unsigned i = 0; i < fabric_state.num_nodes; i++) {
        if (fabric_state.nodes[i].registered &&
            info->address_base < fabric_state.nodes[i].info.address_limit + 1 &&
            fabric_state.nodes[i].info.address_base < info->address_limit + 1)
            return -EADDRINUSE;
    }

    info->id = fabric_state.num_nodes;
    memcpy(&fabric_state.nodes[info->id].info, info, sizeof(*info));
    fabric_state.nodes[info->id].registered = 1;
    fabric_state.num_nodes++;
    return 0;
}

int fabric_bind_memory(unsigned node, void *mem, size_t len)
{
    if (!fabric_state.initialized)
        return -ENODEV;
    if (!node_valid(node) || !mem || len == 0)
        return -EINVAL;
    if (len > fabric_state.nodes[node].info.address_limit -
             fabric_state.nodes[node].info.address_base + 1)
        return -EINVAL;

    fabric_state.nodes[node].mem = mem;
    fabric_state.nodes[node].mem_len = len;
    return 0;
}

int fabric_channel_open(unsigned src, unsigned dst,
                        uint32_t flags,
                        struct fabric_channel *ch)
{
    if (!fabric_state.initialized)
        return -ENODEV;
    if (!ch)
        return -EINVAL;
    if (!node_valid(src) || !node_valid(dst))
        return -ENODEV;

    uint32_t valid_flags = FABRIC_CH_READ | FABRIC_CH_WRITE |
                           FABRIC_CH_DMA | FABRIC_CH_ATOMIC |
                           FABRIC_CH_CACHE_COH | FABRIC_CH_ISO;
    if (flags & ~valid_flags)
        return -EINVAL;

    ch->src_node = src;
    ch->dst_node = dst;
    ch->flags = flags;
    ch->mtu = FABRIC_PACKET_MAX_SIZE;
    ch->bytes_tx = 0;
    ch->bytes_rx = 0;
    return 0;
}

int fabric_channel_close(struct fabric_channel *ch)
{
    if (!ch)
        return -EINVAL;
    memset(ch, 0, sizeof(*ch));
    return 0;
}

int fabric_send(struct fabric_channel *ch,
                const void *data, size_t len)
{
    if (!fabric_state.initialized)
        return -ENODEV;
    if (!ch || !data)
        return -EINVAL;
    if (len == 0)
        return -EINVAL;
    if (len > ch->mtu)
        return -EMSGSIZE;
    if (fabric_state.q.used > 0 &&
        fabric_state.q.used + len > sizeof(fabric_state.q.data))
        return -ENOSPC;

    for (size_t i = 0; i < len; i++) {
        size_t pos = (fabric_state.q.tail + i) & (sizeof(fabric_state.q.data) - 1);
        fabric_state.q.data[pos] = ((const uint8_t *)data)[i];
    }
    fabric_state.q.tail = (fabric_state.q.tail + len) &
                          (sizeof(fabric_state.q.data) - 1);
    fabric_state.q.used += len;

    /* Store into the destination's bound address space at its base. */
    struct fabric_node *dst = &fabric_state.nodes[ch->dst_node];
    if (dst->mem) {
        uint8_t *dst_ptr = fabric_translate(ch->dst_node,
                                            dst->info.address_base);
        if (dst_ptr && len <= (uint64_t)(dst->mem + dst->mem_len - dst_ptr))
            memcpy(dst_ptr, data, len);
    }

    ch->bytes_tx += len;
    fabric_state.total_latency_ns +=
        (uint64_t)fabric_state.nodes[ch->dst_node].info.latency_ns;
    return (int)len;
}

int fabric_recv(struct fabric_channel *ch,
                void *buf, size_t *len)
{
    if (!fabric_state.initialized)
        return -ENODEV;
    if (!ch || !buf || !len)
        return -EINVAL;

    size_t want = *len;
    size_t n = (want < fabric_state.q.used) ? want : fabric_state.q.used;
    for (size_t i = 0; i < n; i++) {
        size_t pos = (fabric_state.q.head + i) & (sizeof(fabric_state.q.data) - 1);
        ((uint8_t *)buf)[i] = fabric_state.q.data[pos];
    }
    fabric_state.q.head = (fabric_state.q.head + n) &
                          (sizeof(fabric_state.q.data) - 1);
    fabric_state.q.used -= n;
    *len = n;
    ch->bytes_rx += n;
    fabric_state.total_latency_ns +=
        (uint64_t)fabric_state.nodes[ch->src_node].info.latency_ns;
    return (int)n;
}

static uint8_t *fabric_translate(unsigned node, uint64_t addr)
{
    struct fabric_node *n = &fabric_state.nodes[node];
    if (!n->mem)
        return NULL;
    if (addr < n->info.address_base || addr >= n->info.address_base + n->mem_len)
        return NULL;
    return n->mem + (size_t)(addr - n->info.address_base);
}

int fabric_dma(struct fabric_channel *ch,
               uint64_t src_addr, uint64_t dst_addr,
               size_t len)
{
    if (!fabric_state.initialized)
        return -ENODEV;
    if (!ch || len == 0)
        return -EINVAL;
    if (!(ch->flags & FABRIC_CH_DMA))
        return -EPERM;
    if (!region_valid(ch->src_node, src_addr, len))
        return -EFAULT;
    if (!region_valid(ch->dst_node, dst_addr, len))
        return -EFAULT;

    while (__atomic_test_and_set(&fabric_state.dma_lock, __ATOMIC_ACQUIRE))
        ;

    uint8_t *src = fabric_translate(ch->src_node, src_addr);
    uint8_t *dst = fabric_translate(ch->dst_node, dst_addr);
    if (src && dst)
        memmove(dst, src, len);

    fabric_state.dma_bytes += (unsigned)len;
    ch->bytes_tx += len;
    fabric_state.dma_lock = 0;

    unsigned lat = fabric_state.nodes[ch->src_node].info.latency_ns +
                   fabric_state.nodes[ch->dst_node].info.latency_ns;
    fabric_state.total_latency_ns += (uint64_t)lat;
    return (int)len;
}

static uint64_t *fabric_atomic_ref(unsigned node, uint64_t addr)
{
    if (!node_valid(node))
        return NULL;
    uint8_t *p = fabric_translate(node, addr);
    if (!p)
        return NULL;
    if ((uintptr_t)p % 8 != 0)
        return NULL;
    return (uint64_t *)p;
}

int fabric_atomic_add(struct fabric_channel *ch,
                      uint64_t addr, uint64_t val)
{
    if (!fabric_state.initialized)
        return -ENODEV;
    if (!ch)
        return -EINVAL;
    if (!(ch->flags & FABRIC_CH_ATOMIC))
        return -EPERM;
    if (!region_valid(ch->dst_node, addr, sizeof(uint64_t)))
        return -EFAULT;

    uint64_t *ref = fabric_atomic_ref(ch->dst_node, addr);
    if (!ref)
        return -ENODEV;
    __atomic_fetch_add(ref, val, __ATOMIC_SEQ_CST);
    fabric_state.atomic_ops++;
    return 0;
}

int fabric_atomic_cas(struct fabric_channel *ch,
                      uint64_t addr,
                      uint64_t expected,
                      uint64_t desired,
                      uint64_t *prev)
{
    if (!fabric_state.initialized)
        return -ENODEV;
    if (!ch)
        return -EINVAL;
    if (!(ch->flags & FABRIC_CH_ATOMIC))
        return -EPERM;
    if (!region_valid(ch->dst_node, addr, sizeof(uint64_t)))
        return -EFAULT;

    uint64_t *ref = fabric_atomic_ref(ch->dst_node, addr);
    if (!ref)
        return -ENODEV;
    uint64_t old = __atomic_exchange_n(ref, desired, __ATOMIC_SEQ_CST);
    if (old != expected) {
        __atomic_exchange_n(ref, old, __ATOMIC_SEQ_CST);   /* roll back */
        if (prev) *prev = old;
        return 1;
    }
    if (prev) *prev = old;
    fabric_state.atomic_ops++;
    return 0;
}

int fabric_atomic_exch(struct fabric_channel *ch,
                       uint64_t addr, uint64_t val, uint64_t *prev)
{
    if (!fabric_state.initialized)
        return -ENODEV;
    if (!ch)
        return -EINVAL;
    if (!(ch->flags & FABRIC_CH_ATOMIC))
        return -EPERM;
    if (!region_valid(ch->dst_node, addr, sizeof(uint64_t)))
        return -EFAULT;

    uint64_t *ref = fabric_atomic_ref(ch->dst_node, addr);
    if (!ref)
        return -ENODEV;
    if (prev)
        *prev = __atomic_exchange_n(ref, val, __ATOMIC_SEQ_CST);
    else
        __atomic_store_n(ref, val, __ATOMIC_SEQ_CST);
    fabric_state.atomic_ops++;
    return 0;
}

int fabric_query_bandwidth(unsigned src, unsigned dst,
                           uint64_t *bw_gbs)
{
    if (!fabric_state.initialized)
        return -ENODEV;
    if (!node_valid(src) || !node_valid(dst))
        return -ENODEV;
    if (!bw_gbs)
        return -EINVAL;

    uint64_t a = fabric_state.nodes[src].info.bandwidth_gbs;
    uint64_t b = fabric_state.nodes[dst].info.bandwidth_gbs;
    /* A link moves data at the slower end's limit (plus a fabric tax). */
    uint64_t link = (a < b) ? a : b;
    *bw_gbs = link * 4 / 5;
    return 0;
}

int fabric_query_latency(unsigned src, unsigned dst,
                         unsigned *lat_ns)
{
    if (!fabric_state.initialized)
        return -ENODEV;
    if (!node_valid(src) || !node_valid(dst))
        return -ENODEV;
    if (!lat_ns)
        return -EINVAL;

    unsigned a = fabric_state.nodes[src].info.latency_ns;
    unsigned b = fabric_state.nodes[dst].info.latency_ns;
    *lat_ns = (a > b) ? a : b;
    return 0;
}