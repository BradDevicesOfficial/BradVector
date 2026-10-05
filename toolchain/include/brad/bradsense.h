#ifndef BRAD_SENSE_H
#define BRAD_SENSE_H

#include <stdint.h>
#include <stddef.h>

#define BRADSENSE_SEMANTIC_W      1920
#define BRADSENSE_SEMANTIC_H      1080
#define BRADSENSE_MAX_OBJECT_CLASSES 8
#define BRADSENSE_MAX_EXPERTS       16
#define BRADSENSE_NSU_MAX           512
#define BRADSENSE_MAX_ACCUM_FRAMES  16

enum bradsense_object_class {
    BRADSENSE_OBJ_FACE_SKIN      = 0,
    BRADSENSE_OBJ_FUR_HAIR       = 1,
    BRADSENSE_OBJ_METAL_GLASS    = 2,
    BRADSENSE_OBJ_FABRIC_ORGANIC = 3,
    BRADSENSE_OBJ_TERRAIN_STONE  = 4,
    BRADSENSE_OBJ_SKY_VOLUMETRIC = 5,
    BRADSENSE_OBJ_VEGETATION     = 6,
    BRADSENSE_OBJ_LIQUID         = 7,
};

enum bradsense_quality_mode {
    BRADSENSE_QUALITY            = 0,
    BRADSENSE_BALANCED           = 1,
    BRADSENSE_PERFORMANCE        = 2,
    BRADSENSE_ULTRA_PERFORMANCE  = 3,
    BRADSENSE_NATIVE_AA          = 4,
};

enum bradsense_frame_gen {
    BRADSENSE_FG_OFF  = 0,
    BRADSENSE_FG_1X   = 1,
    BRADSENSE_FG_2X   = 2,
};

struct bradsense_semantic_feed {
    uint8_t  object_id_map[BRADSENSE_SEMANTIC_W * BRADSENSE_SEMANTIC_H];
    uint8_t  material_type[BRADSENSE_SEMANTIC_W * BRADSENSE_SEMANTIC_H / 2];
    uint16_t depth_buffer[BRADSENSE_SEMANTIC_W * BRADSENSE_SEMANTIC_H];
    uint16_t motion_vectors[BRADSENSE_SEMANTIC_W * BRADSENSE_SEMANTIC_H * 2];
    uint32_t albedo_normal[BRADSENSE_SEMANTIC_W * BRADSENSE_SEMANTIC_H];
};

struct bradsense_expert_net {
    enum bradsense_object_class obj_class;
    const char                 *name;
    unsigned                    id;
    unsigned                    param_count;
    size_t                      weight_bytes;
    unsigned                    input_w;
    unsigned                    input_h;
    unsigned                    output_w;
    unsigned                    output_h;
    unsigned                    sram_reserve_kb;
};

struct bradsense_tier_cfg {
    unsigned nsu_count;
    size_t   sram_bytes;
    unsigned obj_classes;
    int      frame_gen;
    int      temporal_accum;
    int      variable_precision;
    int      nerf_capable;
};

struct bradsense_accum_config {
    unsigned static_max_frames;
    unsigned slow_max_frames;
    unsigned fast_max_frames;
    unsigned new_bootstrap_frames;
};

/* ─── M0: the reference pixel path ───────────────────────────────────────
 * These are the software reference upscaler (BRADSENSE_RENDERER_PLAN.md,
 * rung M0).  bradsense_reconstruct() runs this core against the frame
 * supplied with bradsense_supply_frame(); bradsense_upscale_rgba() is the
 * raw core, exposed so the pixel-path tests can drive small images directly.
 *
 * FSR-3-derived *algorithm*, reimplemented (AMD FidelityFX SDK is MIT).
 * Nothing here is FSR-branded and nothing here is hardware.
 */

enum bradsense_filter {
    BRADSENSE_FILTER_BILINEAR = 0,
    BRADSENSE_FILTER_BICUBIC  = 1,   /* Catmull-Rom */
};

struct bradsense_upscale_cfg {
    enum bradsense_filter filter;
    int   sharpen;            /* 0 = off, 1 = luma-adaptive sharpen */
    float sharpen_mag;        /* 0.0..1.5 gain; 0.75 is the default     */
};

struct bradsense_frame {
    unsigned w;
    unsigned h;
    const uint8_t *rgba;      /* straight RGBA, w*h*4 bytes             */
};

int bradsense_supply_frame(const struct bradsense_frame *frame);
int bradsense_upscale_rgba(const uint8_t *src, unsigned sw, unsigned sh,
                           uint8_t *dst, unsigned dw, unsigned dh,
                           const struct bradsense_upscale_cfg *cfg);

struct bradsense_frame_gen_stats {
    unsigned real_frames;
    unsigned gen_frames;
    unsigned gen_latency_us;
};

struct bradsense_perf_stats {
    unsigned nsu_load_pct;
    unsigned avg_latency_us;
    unsigned peak_latency_us;
    uint64_t frames_processed;
    uint64_t pixels_reconstructed;
    uint64_t semantic_bytes_rx;
    struct bradsense_frame_gen_stats fg;
    unsigned expert_switches;
    unsigned tifa_predictions;
    unsigned tifa_hits;
};

int bradsense_init(enum bradsense_quality_mode quality,
                   enum bradsense_frame_gen fg);
int bradsense_set_quality(enum bradsense_quality_mode mode);
int bradsense_set_frame_gen(enum bradsense_frame_gen fg);
int bradsense_submit_semantic(struct bradsense_semantic_feed *feed);
int bradsense_reconstruct(uint64_t frame_id,
                           void *output, size_t output_size);
int bradsense_generate_frame(uint64_t frame_n,
                              uint64_t frame_n1,
                              void *output, size_t output_size);
int bradsense_get_perf(struct bradsense_perf_stats *stats);
int bradsense_get_accum_config(struct bradsense_accum_config *cfg);
int bradsense_set_accum_config(struct bradsense_accum_config *cfg);
int bradsense_query_expert(enum bradsense_object_class cls,
                            struct bradsense_expert_net *net);
int bradsense_tifa_telemetry(unsigned *util_pct,
                              unsigned *complexity,
                              unsigned *recommended_quality);

#endif
