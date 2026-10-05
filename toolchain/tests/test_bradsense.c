#include "brad/bradsense.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void)
{
    assert(bradsense_init(BRADSENSE_BALANCED, BRADSENSE_FG_1X) == 0);

    assert(bradsense_set_quality(BRADSENSE_PERFORMANCE) == 0);
    assert(bradsense_set_frame_gen(BRADSENSE_FG_2X) == 0);

    /* M0: reconstruct() needs a real input frame now.  Supply an 8x8 solid
     * gray and prove the 4K output is actually that gray -- real pixels,
     * not the zero-filled placeholder this call used to be. */
    {
        uint8_t gray[8 * 8 * 4];
        for (size_t i = 0; i < 8 * 8; i++) {
            gray[i * 4 + 0] = 0x40;
            gray[i * 4 + 1] = 0x40;
            gray[i * 4 + 2] = 0x40;
            gray[i * 4 + 3] = 0xFF;
        }
        struct bradsense_frame frame = { .w = 8, .h = 8, .rgba = gray };
        assert(bradsense_supply_frame(&frame) == 0);
    }

    struct bradsense_expert_net net;
    assert(bradsense_query_expert(BRADSENSE_OBJ_FACE_SKIN, &net) == 0);
    assert(net.param_count == 128000);
    assert(net.weight_bytes == 512 * 1024);
    assert(strcmp(net.name, "face_skin_cnn") == 0);

    assert(bradsense_query_expert(BRADSENSE_OBJ_METAL_GLASS, &net) == 0);
    assert(net.param_count == 192000);

    struct bradsense_semantic_feed *feed = calloc(1, sizeof(*feed));
    assert(feed);
    assert(bradsense_submit_semantic(feed) == 0);
    free(feed);

    size_t out_size = 3840 * 2160 * 4;
    void *output = malloc(out_size);
    assert(output);
    assert(bradsense_reconstruct(1, output, out_size) == 0);
    {
        unsigned char *px = (unsigned char *)output;
        assert(px[0] == 0x40 && px[1] == 0x40 && px[2] == 0x40);
        /* a corner away from pixel 0, in case the first four bytes lie */
        size_t last = (size_t)3840 * 2160 * 4 - 4;
        assert(px[last + 0] == 0x40 && px[last + 1] == 0x40 &&
               px[last + 2] == 0x40);
    }

    assert(bradsense_generate_frame(1, 2, output, out_size) == 0);
    free(output);

    struct bradsense_perf_stats stats;
    assert(bradsense_get_perf(&stats) == 0);
    assert(stats.frames_processed == 2);
    assert(stats.pixels_reconstructed > 0);
    assert(stats.semantic_bytes_rx > 0);
    assert(stats.fg.gen_frames == 1);
    assert(stats.fg.gen_latency_us == 4000);

    struct bradsense_accum_config accum;
    assert(bradsense_get_accum_config(&accum) == 0);
    assert(accum.static_max_frames == 16);
    assert(accum.new_bootstrap_frames == 3);

    accum.static_max_frames = 8;
    accum.slow_max_frames = 4;
    accum.fast_max_frames = 2;
    accum.new_bootstrap_frames = 2;
    assert(bradsense_set_accum_config(&accum) == 0);
    assert(bradsense_get_accum_config(&accum) == 0);
    assert(accum.static_max_frames == 8);

    unsigned util, complexity, rec_q;
    assert(bradsense_tifa_telemetry(&util, &complexity, &rec_q) == 0);
    assert(util == 25);
    assert(complexity >= 30);
    assert(rec_q == BRADSENSE_PERFORMANCE);

    printf("BradSense-Render tests passed\n");
    return 0;
}
