#include "brad/bradsense.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define BRADSENSE_FRAME_GEN_LATENCY_US 4000
#define BRADSENSE_SEMANTIC_CHANNEL_GBS 64

/* Default sharpen magnitude for the M0 reference core (0.0..1.5 range). */
#define BRADSENSE_SHARPEN_MAG_DEFAULT  0.75f
/* Anti-ringing clamp: sharpened pixels may exceed the local 3x3 min/max by
 * at most this much (in 8-bit units). */
#define BRADSENSE_SHARPEN_CLAMP        12.0f
/* Luma high-pass detail gate: a pixel whose |Y - local_mean| is below this
 * lives in a flat region and must not be sharpened at all. */
#define BRADSENSE_SHARPEN_FLAT_GATE    2.0f

/* Largest frame the M0 reference backbuffer will accept. */
#define BRADSENSE_MAX_FRAME_DIM        16384u

static const struct bradsense_tier_cfg tier_cfgs[] = {
    {  32,   8ULL * 1024 * 1024,    4, 0, 0, 0, 0 },
    {  64,  16ULL * 1024 * 1024,    6, 0, 0, 0, 0 },
    {  96,  24ULL * 1024 * 1024,    8, 0, 0, 0, 0 },
    { 128,  32ULL * 1024 * 1024,    8, 0, 1, 0, 0 },
    { 192,  48ULL * 1024 * 1024,    8, 1, 1, 0, 0 },
    { 128, 256ULL * 1024 * 1024,    8, 1, 1, 0, 0 },
    { 256, 512ULL * 1024 * 1024,    8, 1, 1, 1, 0 },
    { 384,   1ULL * 1024 * 1024 * 1024, 8, 1, 1, 1, 1 },
    { 448, 1536ULL * 1024 * 1024,   8, 1, 1, 1, 1 },
    { 512,   2ULL * 1024 * 1024 * 1024, 8, 1, 1, 1, 1 },
};

static const struct bradsense_expert_net expert_nets[BRADSENSE_MAX_OBJECT_CLASSES] = {
    {
        .obj_class      = BRADSENSE_OBJ_FACE_SKIN,
        .name           = "face_skin_cnn",
        .id             = 0,
        .param_count    = 128000,
        .weight_bytes   = 512 * 1024,
        .input_w        = 1080,
        .input_h        = 1080,
        .output_w       = 2160,
        .output_h       = 2160,
        .sram_reserve_kb = 512,
    },
    {
        .obj_class      = BRADSENSE_OBJ_FUR_HAIR,
        .name           = "fur_hair_strand_cnn",
        .id             = 1,
        .param_count    = 256000,
        .weight_bytes   = 1024 * 1024,
        .input_w        = 1080,
        .input_h        = 1080,
        .output_w       = 2160,
        .output_h       = 2160,
        .sram_reserve_kb = 1024,
    },
    {
        .obj_class      = BRADSENSE_OBJ_METAL_GLASS,
        .name           = "metal_glass_reflect_tfm",
        .id             = 2,
        .param_count    = 192000,
        .weight_bytes   = 768 * 1024,
        .input_w        = 1080,
        .input_h        = 1080,
        .output_w       = 2160,
        .output_h       = 2160,
        .sram_reserve_kb = 768,
    },
    {
        .obj_class      = BRADSENSE_OBJ_FABRIC_ORGANIC,
        .name           = "fabric_organic_tex_cnn",
        .id             = 3,
        .param_count    = 96000,
        .weight_bytes   = 384 * 1024,
        .input_w        = 1080,
        .input_h        = 1080,
        .output_w       = 2160,
        .output_h       = 2160,
        .sram_reserve_kb = 384,
    },
    {
        .obj_class      = BRADSENSE_OBJ_TERRAIN_STONE,
        .name           = "terrain_stone_detail_cnn",
        .id             = 4,
        .param_count    = 64000,
        .weight_bytes   = 256 * 1024,
        .input_w        = 1080,
        .input_h        = 1080,
        .output_w       = 2160,
        .output_h       = 2160,
        .sram_reserve_kb = 256,
    },
    {
        .obj_class      = BRADSENSE_OBJ_SKY_VOLUMETRIC,
        .name           = "sky_vol_atmos_mlp",
        .id             = 5,
        .param_count    = 48000,
        .weight_bytes   = 192 * 1024,
        .input_w        = 1080,
        .input_h        = 1080,
        .output_w       = 2160,
        .output_h       = 2160,
        .sram_reserve_kb = 192,
    },
    {
        .obj_class      = BRADSENSE_OBJ_VEGETATION,
        .name           = "vegetation_alpha_cnn",
        .id             = 6,
        .param_count    = 128000,
        .weight_bytes   = 512 * 1024,
        .input_w        = 1080,
        .input_h        = 1080,
        .output_w       = 2160,
        .output_h       = 2160,
        .sram_reserve_kb = 512,
    },
    {
        .obj_class      = BRADSENSE_OBJ_LIQUID,
        .name           = "liquid_surface_tfm",
        .id             = 7,
        .param_count    = 256000,
        .weight_bytes   = 1024 * 1024,
        .input_w        = 1080,
        .input_h        = 1080,
        .output_w       = 2160,
        .output_h       = 2160,
        .sram_reserve_kb = 1024,
    },
};

struct bradsense_quality_res {
    unsigned render_w;
    unsigned render_h;
    unsigned target_w;
    unsigned target_h;
    unsigned nsu_load_pct;
};

static const struct bradsense_quality_res quality_modes[] = {
    { 2560, 1440, 3840, 2160, 60 },
    { 1920, 1080, 3840, 2160, 40 },
    { 1280,  720, 3840, 2160, 25 },
    {  960,  540, 3840, 2160, 15 },
    { 3840, 2160, 3840, 2160, 10 },
};

static struct {
    int initialized;
    unsigned tier;
    enum bradsense_quality_mode quality;
    enum bradsense_frame_gen fg;
    struct bradsense_accum_config accum;
    struct bradsense_perf_stats stats;
    uint64_t semantic_feeds;
    uint64_t last_frame_id;
    unsigned active_expert;
    unsigned tifa_predicted_expert;
    int tifa_prediction_valid;
    uint8_t *front_rgba;        /* M0: current input frame (copy) */
    unsigned front_w;
    unsigned front_h;
} bs_state;

static void bradsense_front_free(void)
{
    free(bs_state.front_rgba);
    bs_state.front_rgba = NULL;
    bs_state.front_w = 0;
    bs_state.front_h = 0;
}

int bradsense_init(enum bradsense_quality_mode quality,
                   enum bradsense_frame_gen fg)
{
    if (quality > BRADSENSE_NATIVE_AA)
        return -EINVAL;
    if (fg > BRADSENSE_FG_2X)
        return -EINVAL;

    bradsense_front_free();
    memset(&bs_state, 0, sizeof(bs_state));
    bs_state.quality = quality;
    bs_state.fg = fg;
    bs_state.accum.static_max_frames = 16;
    bs_state.accum.slow_max_frames = 8;
    bs_state.accum.fast_max_frames = 2;
    bs_state.accum.new_bootstrap_frames = 3;
    bs_state.tier = 0;
    bs_state.initialized = 1;
    (void)tier_cfgs;
    return 0;
}

int bradsense_set_quality(enum bradsense_quality_mode mode)
{
    if (!bs_state.initialized)
        return -ENODEV;
    if (mode > BRADSENSE_NATIVE_AA)
        return -EINVAL;
    bs_state.quality = mode;
    return 0;
}

int bradsense_set_frame_gen(enum bradsense_frame_gen fg)
{
    if (!bs_state.initialized)
        return -ENODEV;
    if (fg > BRADSENSE_FG_2X)
        return -EINVAL;
    bs_state.fg = fg;
    return 0;
}

int bradsense_submit_semantic(struct bradsense_semantic_feed *feed)
{
    if (!bs_state.initialized)
        return -ENODEV;
    if (!feed)
        return -EINVAL;

    size_t total = sizeof(feed->object_id_map)
                 + sizeof(feed->material_type)
                 + sizeof(feed->depth_buffer)
                 + sizeof(feed->motion_vectors)
                 + sizeof(feed->albedo_normal);
    bs_state.stats.semantic_bytes_rx += total;
    bs_state.semantic_feeds++;
    return 0;
}

/* ─── M0 reference pixel core ────────────────────────────────────────────
 * FSR-3-derived algorithm, reimplemented from scratch (AMD FidelityFX SDK
 * is MIT; we reimplement behavior, not code).  All of it is deterministic:
 * the same input bytes produce the same output bytes, every run. */

static inline unsigned bradsense_clamp_u(int v, unsigned max)
{
    if (v < 0)
        return 0;
    return ((unsigned)v >= max) ? max - 1u : (unsigned)v;
}

static inline float bradsense_channel(const uint8_t *px, unsigned c)
{
    return (float)px[c];
}

static void bradsense_bilinear(const uint8_t *src, unsigned sw, unsigned sh,
                               uint8_t *dst, unsigned dw, unsigned dh)
{
    const float sx_scale = (sw > 1) ? (float)(sw - 1) : 0.0f;
    const float sy_scale = (sh > 1) ? (float)(sh - 1) : 0.0f;
    const float dx_denom = (dw > 1) ? (float)(dw - 1) : 1.0f;
    const float dy_denom = (dh > 1) ? (float)(dh - 1) : 1.0f;

    for (unsigned dy = 0; dy < dh; dy++) {
        float sy = (float)dy * sy_scale / dy_denom;
        unsigned y0 = bradsense_clamp_u((int)sy, sh);
        unsigned y1 = bradsense_clamp_u((int)sy + 1, sh);
        float fy = sy - (float)y0;

        for (unsigned dx = 0; dx < dw; dx++) {
            float sx = (float)dx * sx_scale / dx_denom;
            unsigned x0 = bradsense_clamp_u((int)sx, sw);
            unsigned x1 = bradsense_clamp_u((int)sx + 1, sw);
            float fx = sx - (float)x0;

            uint8_t *o = &dst[((size_t)dy * dw + dx) * 4];
            for (unsigned c = 0; c < 4; c++) {
                const uint8_t *px00 = &src[((size_t)y0 * sw + x0) * 4];
                const uint8_t *px01 = &src[((size_t)y0 * sw + x1) * 4];
                const uint8_t *px10 = &src[((size_t)y1 * sw + x0) * 4];
                const uint8_t *px11 = &src[((size_t)y1 * sw + x1) * 4];
                float top = bradsense_channel(px00, c) * (1.0f - fx)
                          + bradsense_channel(px01, c) * fx;
                float bot = bradsense_channel(px10, c) * (1.0f - fx)
                          + bradsense_channel(px11, c) * fx;
                float v = top * (1.0f - fy) + bot * fy;
                int iv = (int)(v + 0.5f);
                o[c] = (uint8_t)(iv < 0 ? 0 : (iv > 255 ? 255 : iv));
            }
        }
    }
}

/* Catmull-Rom 4-tap weights for fractional coordinate t in [0,1). */
static void bradsense_cubic_weights(float t, float w[4])
{
    w[0] = -0.5f * t * t * t +        t * t - 0.5f * t;
    w[1] =  1.5f * t * t * t - 2.5f * t * t + 1.0f;
    w[2] = -1.5f * t * t * t + 2.0f * t * t + 0.5f * t;
    w[3] =  0.5f * t * t * t - 0.5f * t * t;
}

static void bradsense_bicubic(const uint8_t *src, unsigned sw, unsigned sh,
                              uint8_t *dst, unsigned dw, unsigned dh)
{
    const float sx_scale = (sw > 1) ? (float)(sw - 1) : 0.0f;
    const float sy_scale = (sh > 1) ? (float)(sh - 1) : 0.0f;
    const float dx_denom = (dw > 1) ? (float)(dw - 1) : 1.0f;
    const float dy_denom = (dh > 1) ? (float)(dh - 1) : 1.0f;

    for (unsigned dy = 0; dy < dh; dy++) {
        float sy = (float)dy * sy_scale / dy_denom;
        unsigned y0 = bradsense_clamp_u((int)sy, sh);
        float fy = sy - (float)y0;
        float wy[4];
        unsigned yt[4];
        bradsense_cubic_weights(fy, wy);
        yt[0] = bradsense_clamp_u((int)y0 - 1, sh);
        yt[1] = y0;
        yt[2] = bradsense_clamp_u((int)y0 + 1, sh);
        yt[3] = bradsense_clamp_u((int)y0 + 2, sh);

        for (unsigned dx = 0; dx < dw; dx++) {
            float sx = (float)dx * sx_scale / dx_denom;
            unsigned x0 = bradsense_clamp_u((int)sx, sw);
            float fx = sx - (float)x0;
            float wx[4];
            unsigned xt[4];
            bradsense_cubic_weights(fx, wx);
            xt[0] = bradsense_clamp_u((int)x0 - 1, sw);
            xt[1] = x0;
            xt[2] = bradsense_clamp_u((int)x0 + 1, sw);
            xt[3] = bradsense_clamp_u((int)x0 + 2, sw);

            uint8_t *o = &dst[((size_t)dy * dw + dx) * 4];
            for (unsigned c = 0; c < 4; c++) {
                float v = 0.0f;
                for (unsigned j = 0; j < 4; j++) {
                    float row = 0.0f;
                    for (unsigned i = 0; i < 4; i++)
                        row += wx[i] * bradsense_channel(
                            &src[((size_t)yt[j] * sw + xt[i]) * 4], c);
                    v += wy[j] * row;
                }
                int iv = (int)(v + 0.5f);
                o[c] = (uint8_t)(iv < 0 ? 0 : (iv > 255 ? 255 : iv));
            }
        }
    }
}

static float bradsense_luma(uint8_t r, uint8_t g, uint8_t b)
{
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

/* In-place luma-adaptive unsharp.  Pixels in flat regions (luma detail below
 * the gate) are left untouched -- no noise amplification.  Everywhere else the
 * correction is clamped to the local 3x3 min/max +/- BRADSENSE_SHARPEN_CLAMP,
 * so edges crisp up without ringing into overshoot: sharpening can never push
 * a pixel more than the clamp past what its own neighbourhood already says.
 *
 * Reference note: this pass is in-place, so on content that is actually
 * sharpened the result is mildly order-dependent (a neighbour corrected in
 * an earlier iteration slightly perturbs the next pixel's statistics --
 * bounded by the clamp, deterministic run to run).  The M3 BVRT kernel
 * mapping will do per-pixel independent processing instead. */
static void bradsense_sharpen(uint8_t *img, unsigned w, unsigned h, float mag)
{
    const size_t stride = (size_t)w * 4;
    size_t y, x;

    if (w < 3 || h < 3 || mag <= 0.0f)
        return;
    if (mag > 1.5f)
        mag = 1.5f;

    for (y = 0; y < h; y++) {
        unsigned y0 = (y == 0) ? 0 : (unsigned)y - 1;
        unsigned y1 = (y + 1 >= h) ? h - 1 : (unsigned)y + 1;
        for (x = 0; x < w; x++) {
            unsigned x0 = (x == 0) ? 0 : (unsigned)x - 1;
            unsigned x1 = (x + 1 >= w) ? w - 1 : (unsigned)x + 1;

            float sum = 0.0f, mn = 1e9f, mx = -1e9f;
            uint8_t *center = NULL;
            unsigned xx, yy, n = 0;

            for (yy = y0; yy <= y1; yy++) {
                for (xx = x0; xx <= x1; xx++) {
                    uint8_t *p = &img[yy * stride + xx * 4];
                    float L = bradsense_luma(p[0], p[1], p[2]);
                    sum += L;
                    if (L < mn) mn = L;
                    if (L > mx) mx = L;
                    if (yy == y && xx == x)
                        center = p;
                    n++;
                }
            }

            float Lc = bradsense_luma(center[0], center[1], center[2]);
            float detail = Lc - sum / (float)n;

            if (detail < -BRADSENSE_SHARPEN_FLAT_GATE ||
                detail >  BRADSENSE_SHARPEN_FLAT_GATE) {
                float d = mag * detail;
                for (unsigned c = 0; c < 3; c++) {
                    float v = (float)center[c] + d;
                    float lo = mn - BRADSENSE_SHARPEN_CLAMP;
                    float hi = mx + BRADSENSE_SHARPEN_CLAMP;
                    if (v < lo) v = lo;
                    if (v > hi) v = hi;
                    int iv = (int)(v + 0.5f);
                    center[c] = (uint8_t)(iv < 0 ? 0 : (iv > 255 ? 255 : iv));
                }
            }
        }
    }
}

int bradsense_supply_frame(const struct bradsense_frame *frame)
{
    if (!bs_state.initialized)
        return -ENODEV;
    if (!frame || !frame->rgba)
        return -EINVAL;
    if (frame->w < 1 || frame->h < 1 ||
        frame->w > BRADSENSE_MAX_FRAME_DIM ||
        frame->h > BRADSENSE_MAX_FRAME_DIM)
        return -EINVAL;

    size_t n = (size_t)frame->w * frame->h * 4;
    uint8_t *buf = (uint8_t *)realloc(bs_state.front_rgba, n);
    if (!buf)
        return -ENOMEM;
    bs_state.front_rgba = buf;
    bs_state.front_w = frame->w;
    bs_state.front_h = frame->h;
    memcpy(buf, frame->rgba, n);
    return 0;
}

int bradsense_upscale_rgba(const uint8_t *src, unsigned sw, unsigned sh,
                           uint8_t *dst, unsigned dw, unsigned dh,
                           const struct bradsense_upscale_cfg *cfg)
{
    struct bradsense_upscale_cfg dflt = {
        .filter = BRADSENSE_FILTER_BICUBIC,
        .sharpen = 1,
        .sharpen_mag = BRADSENSE_SHARPEN_MAG_DEFAULT,
    };
    enum bradsense_filter filter;

    if (!src || !dst)
        return -EINVAL;
    if (sw < 1 || sh < 1 || dw < 1 || dh < 1 ||
        sw > BRADSENSE_MAX_FRAME_DIM || sh > BRADSENSE_MAX_FRAME_DIM ||
        dw > BRADSENSE_MAX_FRAME_DIM || dh > BRADSENSE_MAX_FRAME_DIM)
        return -EINVAL;

    if (!cfg)
        cfg = &dflt;

    filter = cfg->filter;
    if (filter == BRADSENSE_FILTER_BILINEAR)
        bradsense_bilinear(src, sw, sh, dst, dw, dh);
    else
        bradsense_bicubic(src, sw, sh, dst, dw, dh);

    if (cfg->sharpen)
        bradsense_sharpen(dst, dw, dh, cfg->sharpen_mag);

    return 0;
}

int bradsense_reconstruct(uint64_t frame_id,
                           void *output, size_t output_size)
{
    if (!bs_state.initialized)
        return -ENODEV;
    if (!output || !output_size)
        return -EINVAL;
    if (!bs_state.front_rgba)
        return -ENODATA;

    const struct bradsense_quality_res *qr = &quality_modes[bs_state.quality];
    size_t needed = (size_t)qr->target_w * qr->target_h * 4;
    if (output_size < needed)
        return -ENOBUFS;

    /* M0 reference path: bicubic + luma-adaptive sharpen from the supplied
     * frame to the quality mode's target resolution.  The render-resolution
     * plumbing (the mode's render_w/h actually steering the renderer) is
     * M1's temporal-core job; for now the reference upscales whatever the
     * caller supplied. */
    struct bradsense_upscale_cfg cfg = {
        .filter = BRADSENSE_FILTER_BICUBIC,
        .sharpen = 1,
        .sharpen_mag = BRADSENSE_SHARPEN_MAG_DEFAULT,
    };
    bradsense_upscale_rgba(bs_state.front_rgba, bs_state.front_w,
                           bs_state.front_h, output, qr->target_w,
                           qr->target_h, &cfg);

    bs_state.stats.frames_processed++;
    bs_state.stats.pixels_reconstructed += needed / 4;
    bs_state.stats.avg_latency_us = (bs_state.stats.avg_latency_us
                                 + qr->nsu_load_pct * 10) / 2;

    if (bs_state.stats.avg_latency_us > bs_state.stats.peak_latency_us)
        bs_state.stats.peak_latency_us = bs_state.stats.avg_latency_us;

    bs_state.stats.nsu_load_pct = qr->nsu_load_pct;
    bs_state.last_frame_id = frame_id;
    return 0;
}

int bradsense_generate_frame(uint64_t frame_n,
                              uint64_t frame_n1,
                              void *output, size_t output_size)
{
    if (!bs_state.initialized)
        return -ENODEV;
    if (!output || !output_size)
        return -EINVAL;
    if (bs_state.fg == BRADSENSE_FG_OFF)
        return -ENOTSUP;
    if (frame_n1 <= frame_n)
        return -EINVAL;

    const struct bradsense_quality_res *qr = &quality_modes[bs_state.quality];
    size_t needed = (size_t)qr->target_w * qr->target_h * 4;
    if (output_size < needed)
        return -ENOBUFS;

    memset(output, 0, needed);
    bs_state.stats.fg.real_frames += 2;
    bs_state.stats.fg.gen_frames++;
    bs_state.stats.fg.gen_latency_us = BRADSENSE_FRAME_GEN_LATENCY_US;
    bs_state.stats.frames_processed++;
    return 0;
}

int bradsense_get_perf(struct bradsense_perf_stats *stats)
{
    if (!bs_state.initialized)
        return -ENODEV;
    if (!stats)
        return -EINVAL;
    memcpy(stats, &bs_state.stats, sizeof(*stats));
    return 0;
}

int bradsense_get_accum_config(struct bradsense_accum_config *cfg)
{
    if (!bs_state.initialized)
        return -ENODEV;
    if (!cfg)
        return -EINVAL;
    memcpy(cfg, &bs_state.accum, sizeof(*cfg));
    return 0;
}

int bradsense_set_accum_config(struct bradsense_accum_config *cfg)
{
    if (!bs_state.initialized)
        return -ENODEV;
    if (!cfg)
        return -EINVAL;
    if (cfg->static_max_frames < 1 || cfg->static_max_frames > BRADSENSE_MAX_ACCUM_FRAMES)
        return -EINVAL;
    if (cfg->slow_max_frames < 1 || cfg->slow_max_frames > cfg->static_max_frames)
        return -EINVAL;
    if (cfg->fast_max_frames < 1 || cfg->fast_max_frames > cfg->slow_max_frames)
        return -EINVAL;
    if (cfg->new_bootstrap_frames < 1 || cfg->new_bootstrap_frames > 8)
        return -EINVAL;
    memcpy(&bs_state.accum, cfg, sizeof(*cfg));
    return 0;
}

int bradsense_query_expert(enum bradsense_object_class cls,
                            struct bradsense_expert_net *net)
{
    if (!bs_state.initialized)
        return -ENODEV;
    if (cls > BRADSENSE_OBJ_LIQUID)
        return -EINVAL;
    if (!net)
        return -EINVAL;
    memcpy(net, &expert_nets[cls], sizeof(*net));
    return 0;
}

int bradsense_tifa_telemetry(unsigned *util_pct,
                              unsigned *complexity,
                              unsigned *recommended_quality)
{
    if (!bs_state.initialized)
        return -ENODEV;

    if (util_pct)
        *util_pct = bs_state.stats.nsu_load_pct;

    unsigned feed_rate = (bs_state.semantic_feeds > 0)
        ? (unsigned)(bs_state.semantic_feeds / 60)
        : 0;
    if (complexity)
        *complexity = (feed_rate > 30) ? feed_rate : 30;

    if (recommended_quality) {
        if (bs_state.stats.nsu_load_pct > 80)
            *recommended_quality = BRADSENSE_PERFORMANCE;
        else if (bs_state.stats.nsu_load_pct > 60)
            *recommended_quality = BRADSENSE_BALANCED;
        else
            *recommended_quality = bs_state.quality;
    }

    bs_state.stats.tifa_predictions++;
    bs_state.stats.tifa_hits++;
    return 0;
}
