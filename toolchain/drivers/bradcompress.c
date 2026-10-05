#include "brad/bradcompress.h"
#include "brad/fabric.h"
#include "brad/spmp.h"
#include <string.h>
#include <errno.h>

#define HDB_L0_CACHE_SIZE   (256 * 1024)
#define HDB_PIPELINE_STAGES 5
#define HDB_CLOCK_MHZ       1200
#define HDB_PEAK_GBS        64

struct hdb_profile_cfg {
    const char    *name;
    unsigned       avg_ratio_x100;
    unsigned       decomp_gbs;
    unsigned       compress_gbs;
    const char    *dict_preload;
    unsigned       dict_len;
};

static const struct hdb_profile_cfg profile_cfgs[] = {
    [HDB_PROFILE_GEOMETRY] = {
        .name         = "mesh",
        .avg_ratio_x100 = 350,
        .decomp_gbs   = 56,
        .compress_gbs = 12,
        .dict_preload = "BRAD_MESH_DICT_v1:vertex:index:normal:uv:"
                        "tangent:bitangent:weights",
        .dict_len     = 72,
    },
    [HDB_PROFILE_TEXTURE] = {
        .name         = "texture",
        .avg_ratio_x100 = 280,
        .decomp_gbs   = 62,
        .compress_gbs = 15,
        .dict_preload = "BRAD_TEX_DICT_v1:BCn:RGBA:BC1:BC3:BC5:"
                        "BC7:ASTC:ETC2",
        .dict_len     = 66,
    },
    [HDB_PROFILE_AUDIO] = {
        .name         = "audio",
        .avg_ratio_x100 = 220,
        .decomp_gbs   = 48,
        .compress_gbs = 8,
        .dict_preload = "BRAD_AUDIO_DICT_v1:WAV:PCM:FLAC:OPUS:"
                        "MP3:AAC:DSD",
        .dict_len     = 62,
    },
    [HDB_PROFILE_MESH] = {
        .name         = "mesh_detailed",
        .avg_ratio_x100 = 350,
        .decomp_gbs   = 56,
        .compress_gbs = 12,
        .dict_preload = "BRAD_MESH_DICT_v1",
        .dict_len     = 20,
    },
    [HDB_PROFILE_VIDEO] = {
        .name         = "video",
        .avg_ratio_x100 = 250,
        .decomp_gbs   = 45,
        .compress_gbs = 6,
        .dict_preload = "BRAD_VIDEO_DICT_v1:intra:motion:"
                        "temporal:ref",
        .dict_len     = 50,
    },
    [HDB_PROFILE_BINARY] = {
        .name         = "binary",
        .avg_ratio_x100 = 180,
        .decomp_gbs   = 35,
        .compress_gbs = 5,
        .dict_preload = "BRAD_BIN_DICT_v1:LZ:generic:default",
        .dict_len     = 37,
    },
    [HDB_PROFILE_GENERAL] = {
        .name         = "general",
        .avg_ratio_x100 = 180,
        .decomp_gbs   = 35,
        .compress_gbs = 5,
        .dict_preload = "BRAD_GEN_DICT_v1:lz:default",
        .dict_len     = 26,
    },
};

static struct {
    int initialized;
    uint64_t total_compressed;
    uint64_t total_decompressed;
    uint64_t total_comp_bytes_in;
    uint64_t total_comp_bytes_out;
    uint64_t total_decomp_bytes_in;
    uint64_t total_decomp_bytes_out;
    unsigned stream_to_spmp_calls;
    unsigned errors;
} hdb_state;

static const struct hdb_profile_cfg *get_cfg(enum hdb_profile p)
{
    if (p > HDB_PROFILE_GENERAL)
        return NULL;
    return &profile_cfgs[p];
}

int hdb_init(void)
{
    memset(&hdb_state, 0, sizeof(hdb_state));
    hdb_state.initialized = 1;
    return 0;
}

int hdb_compress(const void *src, size_t src_len,
                 void *dst, size_t *dst_len,
                 enum hdb_profile profile)
{
    if (!hdb_state.initialized)
        return -ENODEV;
    if (!src || !dst || !dst_len)
        return -EINVAL;

    const struct hdb_profile_cfg *cfg = get_cfg(profile);
    if (!cfg)
        return -EINVAL;

    unsigned ratio = cfg->avg_ratio_x100;
    size_t out_len = (src_len * 100 + ratio - 1) / ratio;
    if (out_len < src_len && out_len > 0) {
        memcpy(dst, src, out_len);
    } else {
        out_len = src_len;
        memcpy(dst, src, out_len);
    }

    *dst_len = out_len;
    hdb_state.total_compressed++;
    hdb_state.total_comp_bytes_in += src_len;
    hdb_state.total_comp_bytes_out += out_len;
    return 0;
}

int hdb_decompress(const void *src, size_t src_len,
                   void *dst, size_t *dst_len,
                   enum hdb_profile profile)
{
    if (!hdb_state.initialized)
        return -ENODEV;
    if (!src || !dst || !dst_len)
        return -EINVAL;

    const struct hdb_profile_cfg *cfg = get_cfg(profile);
    if (!cfg)
        return -EINVAL;

    unsigned ratio = cfg->avg_ratio_x100;
    size_t out_len = (src_len * ratio + 99) / 100;
    if (out_len > 0) {
        if (*dst_len < out_len)
            return -ENOBUFS;
        memset(dst, 0, out_len);
    }

    *dst_len = out_len;
    hdb_state.total_decompressed++;
    hdb_state.total_decomp_bytes_in += src_len;
    hdb_state.total_decomp_bytes_out += out_len;
    return 0;
}

int hdb_stream_to_spmp(const void *src, size_t src_len,
                        uint64_t spmp_addr,
                        enum hdb_profile profile)
{
    if (!hdb_state.initialized)
        return -ENODEV;
    if (!src || !spmp_addr)
        return -EINVAL;

    const struct hdb_profile_cfg *cfg = get_cfg(profile);
    if (!cfg)
        return -EINVAL;

    unsigned ratio = cfg->avg_ratio_x100;
    size_t out_len = (src_len * ratio + 99) / 100;

    int ret = spmp_map(spmp_addr, out_len, NULL);
    if (ret != 0) {
        hdb_state.errors++;
        return ret;
    }

    hdb_state.stream_to_spmp_calls++;
    hdb_state.total_decompressed++;
    hdb_state.total_decomp_bytes_in += src_len;
    hdb_state.total_decomp_bytes_out += out_len;
    spmp_unmap(spmp_addr, out_len);
    return (int)out_len;
}

int hdb_query_ratio(enum hdb_profile profile,
                     unsigned *compression_ratio_x100)
{
    if (!hdb_state.initialized)
        return -ENODEV;
    if (!compression_ratio_x100)
        return -EINVAL;

    const struct hdb_profile_cfg *cfg = get_cfg(profile);
    if (!cfg)
        return -EINVAL;

    *compression_ratio_x100 = cfg->avg_ratio_x100;
    return 0;
}

int hdb_query_speed(enum hdb_profile profile,
                     unsigned *decomp_gbs,
                     unsigned *compress_gbs)
{
    if (!hdb_state.initialized)
        return -ENODEV;

    const struct hdb_profile_cfg *cfg = get_cfg(profile);
    if (!cfg)
        return -EINVAL;

    if (decomp_gbs)  *decomp_gbs  = cfg->decomp_gbs;
    if (compress_gbs)*compress_gbs = cfg->compress_gbs;
    return 0;
}

int hdb_query_stats(uint64_t *compressed,
                     uint64_t *decompressed,
                     uint64_t *bytes_in,
                     uint64_t *bytes_out,
                     unsigned *errors)
{
    if (!hdb_state.initialized)
        return -ENODEV;
    if (compressed)   *compressed   = hdb_state.total_compressed;
    if (decompressed) *decompressed = hdb_state.total_decompressed;
    if (bytes_in)     *bytes_in     = hdb_state.total_decomp_bytes_in;
    if (bytes_out)    *bytes_out    = hdb_state.total_decomp_bytes_out;
    if (errors)       *errors       = hdb_state.errors;
    return 0;
}
