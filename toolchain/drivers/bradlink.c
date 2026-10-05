#include "brad/bradlink.h"
#include <string.h>
#include <errno.h>

static struct {
    struct brad_link_device_info devices[BRAD_LINK_MAX_DEVICES];
    unsigned num_devices;
    int initialized;
} link_state;

int brad_link_init(void)
{
    memset(&link_state, 0, sizeof(link_state));
    link_state.initialized = 1;
    return 0;
}

int brad_link_scan(struct brad_link_device_info *devices,
                    unsigned *count)
{
    if (!link_state.initialized)
        return -ENODEV;
    if (!devices || !count)
        return -EINVAL;

    unsigned n = link_state.num_devices;
    if (n > *count)
        n = *count;

    memcpy(devices, link_state.devices,
           n * sizeof(struct brad_link_device_info));
    *count = n;
    return (int)n;
}

int brad_link_tether(unsigned device_id,
                      enum brad_link_capability capability)
{
    if (!link_state.initialized)
        return -ENODEV;
    if (device_id >= link_state.num_devices)
        return -ENODEV;
    (void)capability;
    return 0;
}

int brad_link_untether(unsigned device_id)
{
    if (!link_state.initialized)
        return -ENODEV;
    if (device_id >= link_state.num_devices)
        return -ENODEV;
    return 0;
}

int brad_link_open_channel(uint32_t device_id,
                            enum brad_link_capability cap,
                            struct brad_link_channel *ch)
{
    if (!link_state.initialized)
        return -ENODEV;
    if (device_id >= link_state.num_devices || !ch)
        return -EINVAL;
    (void)cap;
    ch->active = 1;
    ch->device_id = device_id;
    return 0;
}

int brad_link_close_channel(struct brad_link_channel *ch)
{
    if (!ch)
        return -EINVAL;
    ch->active = 0;
    return 0;
}

int brad_link_offload_ai(const void *model,
                          const void *input,
                          void *output,
                          size_t in_size,
                          size_t out_size,
                          struct brad_link_channel *ch)
{
    if (!link_state.initialized)
        return -ENODEV;
    if (!model || !input || !output || !ch)
        return -EINVAL;
    (void)in_size;
    (void)out_size;
    return 0;
}

int brad_link_sync_context(struct brad_link_channel *ch,
                            uint64_t context_id)
{
    if (!ch)
        return -EINVAL;
    (void)context_id;
    return 0;
}
