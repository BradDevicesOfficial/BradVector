#ifndef BRAD_TIFA_H
#define BRAD_TIFA_H

#include <stdint.h>
#include <stddef.h>

#define TIFA_MAX_MODEL_NAME      128
#define TIFA_MAX_LAYERS          1024
#define TIFA_MAX_EXPERTS         8192

enum tifa_model_type {
    TIFA_MODEL_LLM      = 0,
    TIFA_MODEL_VISION   = 1,
    TIFA_MODEL_MULTIMODAL = 2,
    TIFA_MODEL_DIFFUSION = 3,
    TIFA_MODEL_CUSTOM   = 4,
};

enum tifa_precision {
    TIFA_FP32 = 0,
    TIFA_FP16 = 1,
    TIFA_BF16 = 2,
    TIFA_INT8 = 3,
    TIFA_INT4 = 4,
    TIFA_FP8  = 5,
};

struct tifa_model_config {
    char                  name[TIFA_MAX_MODEL_NAME];
    enum tifa_model_type  type;
    enum tifa_precision   precision;
    unsigned              num_layers;
    size_t                param_count;
    size_t                weight_size_bytes;
    unsigned              moe_num_experts;
    unsigned              moe_top_k;
    int                   use_hb_aim;
};

struct tifa_inference_request {
    const void           *input_tokens;
    size_t                num_tokens;
    unsigned              max_output_tokens;
    float                 temperature;
    float                 top_p;
    int                   stream;
};

struct tifa_inference_result {
    void                 *output_tokens;
    size_t                num_output_tokens;
    float                 tokens_per_ms;
    unsigned              total_ops;
};

int  tifa_init(void);
int  tifa_load_model(const struct tifa_model_config *cfg,
                     const void *weights,
                     size_t weight_size);
int  tifa_unload_model(void);
int  tifa_infer(struct tifa_inference_request *req,
                struct tifa_inference_result *res);
int  tifa_optimize_model(const char *src_path,
                         const char *dst_path,
                         enum tifa_precision precision);
int  tifa_quantize(const void *src, void *dst,
                   size_t count, enum tifa_precision dst_prec);
int  tifa_moe_route(const float *token_embed,
                    unsigned num_tokens,
                    unsigned num_experts,
                    unsigned top_k,
                    unsigned *expert_ids,
                    float *expert_weights);

#endif
