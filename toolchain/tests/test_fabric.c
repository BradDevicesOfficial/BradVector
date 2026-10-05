#include "brad/fabric.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    assert(fabric_init() == 0);

    struct fabric_node_info cpu = {
        .type = FABRIC_NODE_CPU,
        .address_base = 0,
        .address_limit = 0xFFFFFFFF,
        .bandwidth_gbs = 256,
        .latency_ns = 10,
    };
    struct fabric_node_info gpu = {
        .type = FABRIC_NODE_GPU,
        .address_base = 0x100000000ULL,
        .address_limit = 0x1FFFFFFFFULL,
        .bandwidth_gbs = 512,
        .latency_ns = 20,
    };

    assert(fabric_register_node(&cpu) == 0);
    assert(fabric_register_node(&gpu) == 0);
    assert(cpu.id == 0 && gpu.id == 1);

    /* Duplicate windows must be rejected. */
    assert(fabric_register_node(&gpu) == -EADDRINUSE);

    struct fabric_channel ch;
    assert(fabric_channel_open(0, 1, FABRIC_CH_READ | FABRIC_CH_WRITE, &ch) == 0);

    /* Message pass: real bytes must move end-to-end. */
    static uint8_t gpu_mem[4096];
    static uint8_t cpu_mem[4096];
    assert(fabric_bind_memory(0, cpu_mem, sizeof(cpu_mem)) == 0);
    assert(fabric_bind_memory(1, gpu_mem, sizeof(gpu_mem)) == 0);

    const char msg[] = "hello fabric";
    assert(fabric_send(&ch, msg, sizeof(msg)) > 0);

    char back[sizeof(msg)] = {0};
    size_t n = sizeof(back);
    assert(fabric_recv(&ch, back, &n) > 0);
    assert(memcmp(back, msg, sizeof(msg)) == 0);
    /* The fabric also landed the message in the destination's address space. */
    assert(strcmp((char *)gpu_mem, "hello fabric") == 0);

    assert(fabric_channel_close(&ch) == 0);

    /* DMA: move real bytes between address windows. */
    memset(cpu_mem, 0xA5, 64);   /* cpu addr  0..63   -> src */
    memset(gpu_mem, 0, 64);      /* gpu addr base..  -> dst */

    struct fabric_channel dma_ch;
    assert(fabric_channel_open(0, 1, FABRIC_CH_DMA, &dma_ch) == 0);
    size_t len = 32;
    assert(fabric_dma(&dma_ch, 0, 0x100000000ULL, len) == (int)len);
    assert(memcmp(gpu_mem, cpu_mem, 32) == 0);

    /* Atomics operate on the destination's bound address space. */
    uint64_t ctr = 0;
    assert(fabric_bind_memory(1, &ctr, sizeof(ctr)) == 0);
    {
        struct fabric_channel at;
        assert(fabric_channel_open(0, 1, FABRIC_CH_ATOMIC, &at) == 0);
        uint64_t addr = gpu.address_base;
        assert(fabric_atomic_add(&at, addr, 5) == 0);
        assert(ctr == 5);

        uint64_t prev = 0;
        assert(fabric_atomic_cas(&at, addr, 5, 9, &prev) == 0);
        assert(prev == 5 && ctr == 9);

        /* Mismatched CAS leaves the value alone and reports 1. */
        assert(fabric_atomic_cas(&at, addr, 5, 99, &prev) == 1);
        assert(ctr == 9);

        assert(fabric_atomic_exch(&at, addr, 42, &prev) == 0);
        assert(prev == 9 && ctr == 42);

        /* Atomic ops without FABRIC_CH_ATOMIC are refused. */
        assert(fabric_atomic_add(&ch, addr, 1) == -EPERM);
    }

    uint64_t bw = 0;
    unsigned lat = 0;
    assert(fabric_query_bandwidth(0, 1, &bw) == 0);
    assert(fabric_query_latency(0, 1, &lat) == 0);
    assert(bw > 0 && lat == 20);

    printf("Fabric tests passed\n");
    return 0;
}