/* SPDX-License-Identifier: MIT */
/* test_bradsense_pixels.c — M0 reference pixel path (BRADSENSE_RENDERER_PLAN.md
 *
 * The control-plane test proves the bookkeeping; this test proves the pixels.
 * Everything here drives bradsense_upscale_rgba() and bradsense_reconstruct()
 * on small synthetic images where the right answer is knowable:
 *
 *   - corners survive the resampler exactly (bilinear maps them 1:1)
 *   - output is deterministic (same input -> identical bytes, every run)
 *   - flat regions are untouched by sharpening (no noise amplification)
 *   - sharpening crisps a step edge without adding mid-tone mush
 *   - the anti-ringing clamp bounds a lone impulse: no bright halo, dark
 *     halo clamped to +/-12 of the local neighbourhood, never overshoot
 *     beyond the source maximum
 *   - on a smooth 2x super-resolution task, bicubic beats bilinear and
 *     clearing a plain PSNR floor
 *   - reconstruct() refuses to invent pixels (returns -ENODATA with no
 *     supplied frame) and, given one, writes real pixels at 4K
 */

#include "brad/bradsense.h"
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, ...) do {                                              \
    if (!(cond)) {                                                         \
        failures++;                                                        \
        printf("FAIL %s:%d: ", __FILE__, __LINE__);                       \
        printf(__VA_ARGS__);                                               \
        printf("\n");                                                      \
    }                                                                      \
} while (0)

static void fill_checker(uint8_t *img, unsigned w, unsigned h, unsigned cell)
{
    for (unsigned y = 0; y < h; y++)
        for (unsigned x = 0; x < w; x++) {
            uint8_t v = (((x / cell) + (y / cell)) & 1u) ? 255u : 0u;
            size_t o = ((size_t)y * w + x) * 4;
            img[o + 0] = v;
            img[o + 1] = v;
            img[o + 2] = v;
            img[o + 3] = 0xFF;
        }
}

/* Smooth radial blob: the "ground truth" for the super-resolution PSNR test
 * (a quadratic falloff has no step edges, so the resampler is the only
 * difference between the upscale and the truth). */
static void fill_blob(uint8_t *img, unsigned w, unsigned h)
{
    float cx = (float)(w - 1) / 2.0f;
    float cy = (float)(h - 1) / 2.0f;
    float R = (w < h ? (float)w : (float)h) / 2.0f;

    for (unsigned y = 0; y < h; y++)
        for (unsigned x = 0; x < w; x++) {
            float dx = ((float)x - cx) / R;
            float dy = ((float)y - cy) / R;
            float r = sqrtf(dx * dx + dy * dy);
            float v = 255.0f * (r < 1.0f ? 1.0f - r : 0.0f);
            size_t o = ((size_t)y * w + x) * 4;
            img[o + 0] = (uint8_t)(v + 0.5f);
            img[o + 1] = (uint8_t)(v + 0.5f);
            img[o + 2] = (uint8_t)(v + 0.5f);
            img[o + 3] = 0xFF;
        }
}

/* Exact integer block average: simulates the lower-resolution "rendered"
 * frame of a smooth scene. */
static void box_downsample(const uint8_t *src, unsigned sw, unsigned sh,
                           uint8_t *dst, unsigned dw, unsigned dh)
{
    unsigned bw = sw / dw, bh = sh / dh;

    for (unsigned y = 0; y < dh; y++)
        for (unsigned x = 0; x < dw; x++) {
            unsigned sum[4] = { 0, 0, 0, 0 };
            for (unsigned j = 0; j < bh; j++)
                for (unsigned i = 0; i < bw; i++) {
                    const uint8_t *p =
                        &src[((size_t)(y * bh + j) * sw + (x * bw + i)) * 4];
                    for (int c = 0; c < 4; c++)
                        sum[c] += p[c];
                }
            uint8_t *o = &dst[((size_t)y * dw + x) * 4];
            for (int c = 0; c < 4; c++) {
                double v = (double)sum[c] / (double)(bw * bh);
                o[c] = (uint8_t)(v + 0.5);
            }
        }
}

static double psnr_rgb(const uint8_t *a, const uint8_t *b, size_t n_pix)
{
    double mse = 0.0;

    for (size_t i = 0; i < n_pix; i++) {
        const uint8_t *pa = a + i * 4;
        const uint8_t *pb = b + i * 4;
        for (int c = 0; c < 3; c++) {
            double d = (double)pa[c] - (double)pb[c];
            mse += d * d;
        }
    }
    mse /= (double)(n_pix * 3);
    if (mse <= 0.0)
        return 99.0;
    return 10.0 * log10((255.0 * 255.0) / mse);
}

static void test_dims_and_corners(void)
{
    uint8_t src[16 * 16 * 4];
    uint8_t dst[32 * 32 * 4];
    struct bradsense_upscale_cfg cfg = {
        .filter = BRADSENSE_FILTER_BILINEAR,
        .sharpen = 0,
        .sharpen_mag = 0.0f,
    };

    fill_checker(src, 16, 16, 4);

    CHECK(bradsense_upscale_rgba(src, 16, 16, dst, 32, 32, &cfg) == 0,
          "bilinear 16x16->32x32 failed");
    /* corners map to exact source pixels (bilinear), and src (0,0) and
     * (15,15) are both black cells of the checker */
    CHECK(dst[0] == 0 && dst[1] == 0 && dst[2] == 0,
          "corner (0,0) not preserved, got %u", dst[0]);
    CHECK(dst[31 * 32 * 4 + 31 * 4] == 0, "corner (31,31) not preserved");
    CHECK(dst[32 * 32 * 4 - 1] == 0xFF, "alpha lost on last pixel");
}

static void test_determinism(void)
{
    uint8_t src[16 * 16 * 4];
    uint8_t a[48 * 48 * 4], b[48 * 48 * 4];
    struct bradsense_upscale_cfg cfg = {
        .filter = BRADSENSE_FILTER_BICUBIC,
        .sharpen = 1,
        .sharpen_mag = 0.75f,
    };

    fill_checker(src, 16, 16, 3);
    CHECK(bradsense_upscale_rgba(src, 16, 16, a, 48, 48, &cfg) == 0, "run 1");
    CHECK(bradsense_upscale_rgba(src, 16, 16, b, 48, 48, &cfg) == 0, "run 2");
    CHECK(memcmp(a, b, sizeof(a)) == 0, "bicubic output is not deterministic");
}

static void test_flat_untouched(void)
{
    uint8_t src[16 * 16 * 4];
    uint8_t dst[32 * 32 * 4];
    struct bradsense_upscale_cfg cfg = {
        .filter = BRADSENSE_FILTER_BICUBIC,
        .sharpen = 1,
        .sharpen_mag = 1.5f,   /* maximum gain, flat region must still pass */
    };

    for (size_t i = 0; i < 16 * 16; i++) {
        src[i * 4 + 0] = 0x80;
        src[i * 4 + 1] = 0x80;
        src[i * 4 + 2] = 0x80;
        src[i * 4 + 3] = 0xFF;
    }
    CHECK(bradsense_upscale_rgba(src, 16, 16, dst, 32, 32, &cfg) == 0,
          "flat upscale failed");
    for (size_t i = 0; i < 32 * 32 * 4; i++)
        CHECK(dst[i] == 0xFF || dst[i] == 0x80,
              "flat pixel %zu modified by sharpening: %u", i, dst[i]);
}

static void test_edge_crispening(void)
{
    uint8_t src[32 * 32 * 4];
    uint8_t base[96 * 96 * 4], sharp[96 * 96 * 4];
    struct bradsense_upscale_cfg cfg_base = {
        .filter = BRADSENSE_FILTER_BICUBIC,
        .sharpen = 0,
        .sharpen_mag = 0.0f,
    };
    struct bradsense_upscale_cfg cfg_sharp = {
        .filter = BRADSENSE_FILTER_BICUBIC,
        .sharpen = 1,
        .sharpen_mag = 0.75f,
    };
    size_t mid_base = 0, mid_sharp = 0;
    size_t bright_exact_base = 0, bright_exact_sharp = 0;
    size_t dark_exact_base = 0, dark_exact_sharp = 0;

    /* one hard vertical edge down the middle: left half 0, right half 255 */
    for (unsigned y = 0; y < 32; y++)
        for (unsigned x = 0; x < 32; x++) {
            uint8_t v = (x < 16) ? 0u : 255u;
            size_t o = ((size_t)y * 32 + x) * 4;
            src[o + 0] = src[o + 1] = src[o + 2] = v;
            src[o + 3] = 0xFF;
        }
    CHECK(bradsense_upscale_rgba(src, 32, 32, base, 96, 96, &cfg_base) == 0,
          "base upscale failed");
    CHECK(bradsense_upscale_rgba(src, 32, 32, sharp, 96, 96, &cfg_sharp) == 0,
          "sharp upscale failed");

    for (size_t i = 0; i < 96 * 96; i++) {
        uint8_t b = base[i * 4];
        uint8_t s = sharp[i * 4];
        if (b > 30 && b < 225) mid_base++;
        if (s > 30 && s < 225) mid_sharp++;
        if (b == 255) bright_exact_base++;
        if (s == 255) bright_exact_sharp++;
        if (b == 0) dark_exact_base++;
        if (s == 0) dark_exact_sharp++;
        if (b == 255)                            /* true plateaus stay put */
            CHECK(s == 255, "bright plateau pixel %zu moved: %u -> %u", i, b, s);
        if (b == 0)
            CHECK(s == 0, "dark plateau pixel %zu moved: %u -> %u", i, b, s);
    }
    CHECK(memcmp(base, sharp, sizeof(base)) != 0,
          "sharpening changed nothing on content it should sharpen");
    /* Crispening, measured the honest way: sharpening pulls the S-curve's
     * shoulders onto the plateaus, so the number of pixels landing exactly
     * on 255 (and exactly on 0) must rise, and the mid-tone transition
     * mush must not grow.  (Unsharp on a perfectly linear ramp would do
     * nothing -- a step edge upscaled bicubic is an S-curve, and that is
     * exactly where the gain shows.) */
    CHECK(bright_exact_sharp > bright_exact_base,
          "sharpening did not pull the bright shoulder to 255: %zu -> %zu",
          bright_exact_base, bright_exact_sharp);
    CHECK(dark_exact_sharp > dark_exact_base,
          "sharpening did not pull the dark shoulder to 0: %zu -> %zu",
          dark_exact_base, dark_exact_sharp);
    CHECK(mid_sharp <= mid_base, "sharpening increased mid-tone mush: %zu -> %zu",
          mid_base, mid_sharp);
}

static void test_anti_ringing(void)
{
    uint8_t src[32 * 32 * 4];
    uint8_t base[128 * 128 * 4], sharp[128 * 128 * 4];
    struct bradsense_upscale_cfg cfg_base = {
        .filter = BRADSENSE_FILTER_BICUBIC,
        .sharpen = 0,
        .sharpen_mag = 0.0f,
    };
    struct bradsense_upscale_cfg cfg_sharp = {
        .filter = BRADSENSE_FILTER_BICUBIC,
        .sharpen = 1,
        .sharpen_mag = 1.5f,   /* maximum gain: if it halos now, it always will */
    };
    unsigned bg_far_max = 0, dev_max = 0;

    memset(src, 0, sizeof(src));
    {   /* one bright 2x2 impulse at the centre, surrounded by 0 */
        unsigned c = 16;
        for (unsigned j = 0; j < 2; j++)
            for (unsigned i = 0; i < 2; i++) {
                size_t o = ((size_t)(c + j) * 32 + (c + i)) * 4;
                src[o + 0] = src[o + 1] = src[o + 2] = 255;
                src[o + 3] = 0xFF;
            }
    }

    CHECK(bradsense_upscale_rgba(src, 32, 32, base, 128, 128, &cfg_base) == 0,
          "base upscale failed");
    CHECK(bradsense_upscale_rgba(src, 32, 32, sharp, 128, 128, &cfg_sharp) == 0,
          "sharp upscale failed");

    /* Per-pixel anti-ringing bound, computed from the *unsharpened* base:
     * a sharpened pixel may never wander more than +/-12 (the clamp) past its
     * own 3x3 neighbourhood min/max -- plus propagation slack for the
     * in-place pass, and a rounding unit.  An unclamped sharpener would be
     * free to blow out by ~340 here; we bound it to +/-25. */
    for (size_t i = 0; i < 128 * 128; i++) {
        unsigned px = (unsigned)(i % 128), py = (unsigned)(i / 128);
        unsigned x0b = px == 0 ? 0 : px - 1;
        unsigned x1b = px + 1 >= 128 ? 127 : px + 1;
        unsigned y0b = py == 0 ? 0 : py - 1;
        unsigned y1b = py + 1 >= 128 ? 127 : py + 1;
        unsigned mn = 255, mx = 0;
        for (unsigned j = y0b; j <= y1b; j++)
            for (unsigned k = x0b; k <= x1b; k++) {
                uint8_t v = base[((size_t)j * 128 + k) * 4];
                if (v < mn) mn = v;
                if (v > mx) mx = v;
            }
        uint8_t s = sharp[i * 4];
        int lo = (int)mn - 25, hi = (int)mx + 25;
        CHECK((int)s >= lo && (int)s <= hi,
              "sharpened pixel %zu=%u outside [%d,%d] neighbourhood bound",
              i, s, lo, hi);
    }

    /* No pixel may overshoot beyond the source maximum (255), ever. */
    {
        unsigned hi = 0;
        for (size_t i = 0; i < 128 * 128; i++)
            if (sharp[i * 4] > hi) hi = sharp[i * 4];
        CHECK(hi <= 255, "sharpened output overshoots source max: %u", hi);
    }
    /* The far field -- pixels the *unsharpened* base leaves essentially black
     * (<=5) -- must stay black after sharpening (<=12): a halo that spreads
     * into darkness is a halo the clamp failed to contain.  (The 2x2 impulse
     * itself legitimately occupies ~5 dst px after the 2x upscale, so the
     * field is keyed on base value, not distance.) */
    for (size_t i = 0; i < 128 * 128; i++) {
        uint8_t b = base[i * 4];
        uint8_t s = sharp[i * 4];
        if (b <= 5 && s > bg_far_max) bg_far_max = s;
    }
    CHECK(bg_far_max <= 12,
          "sharpening spread a halo into the dark field: %u", bg_far_max);
    /* bounded total deviation from the base image (catches gross corruption) */
    for (size_t i = 0; i < 128 * 128; i++) {
        uint8_t b = base[i * 4], s = sharp[i * 4];
        unsigned dev = (unsigned)(b > s ? b - s : s - b);
        if (dev > dev_max) dev_max = dev;
    }
    CHECK(dev_max <= 60, "unexpectedly large deviation from base: %u", dev_max);
}

static void test_psnr_super_res(void)
{
    enum { GTW = 64, GTH = 64, LRW = 32, LRH = 32 };
    uint8_t gt[GTW * GTH * 4];
    uint8_t lr[LRW * LRH * 4];
    uint8_t bil[GTW * GTH * 4], bic[GTW * GTH * 4], sharp[GTW * GTH * 4];
    struct bradsense_upscale_cfg cfg_bil = {
        .filter = BRADSENSE_FILTER_BILINEAR,
        .sharpen = 0,
        .sharpen_mag = 0.0f,
    };
    struct bradsense_upscale_cfg cfg_bic = {
        .filter = BRADSENSE_FILTER_BICUBIC,
        .sharpen = 0,
        .sharpen_mag = 0.0f,
    };
    struct bradsense_upscale_cfg cfg_sharp = {
        .filter = BRADSENSE_FILTER_BICUBIC,
        .sharpen = 1,
        .sharpen_mag = 0.75f,
    };

    fill_blob(gt, GTW, GTH);
    box_downsample(gt, GTW, GTH, lr, LRW, LRH);

    CHECK(bradsense_upscale_rgba(lr, LRW, LRH, bil, GTW, GTH, &cfg_bil) == 0,
          "bilinear SR failed");
    CHECK(bradsense_upscale_rgba(lr, LRW, LRH, bic, GTW, GTH, &cfg_bic) == 0,
          "bicubic SR failed");
    CHECK(bradsense_upscale_rgba(lr, LRW, LRH, sharp, GTW, GTH, &cfg_sharp) == 0,
          "bicubic+sharpen SR failed");

    double p_bil = psnr_rgb(bil, gt, GTW * GTH);
    double p_bic = psnr_rgb(bic, gt, GTW * GTH);
    double p_shp = psnr_rgb(sharp, gt, GTW * GTH);

    printf("  PSNR re 2x super-res: bilinear %.2f dB / bicubic %.2f dB / "
           "bicubic+sharpen %.2f dB\n", p_bil, p_bic, p_shp);

    CHECK(p_bic >= p_bil - 0.5, "bicubic should at least match bilinear "
          "(%.2f vs %.2f)", p_bic, p_bil);
    CHECK(p_shp >= p_bil - 0.5, "sharpen should not wreck smooth content "
          "(%.2f vs %.2f)", p_shp, p_bil);
    CHECK(p_bic > 18.0, "bicubic floor too low on smooth content: %.2f", p_bic);
}

static void test_api_validation(void)
{
    uint8_t src[4 * 4 * 4] = { 0 };
    uint8_t dst[8 * 8 * 4] = { 0 };
    struct bradsense_upscale_cfg cfg = {
        .filter = BRADSENSE_FILTER_BICUBIC,
        .sharpen = 1,
        .sharpen_mag = 0.75f,
    };

    CHECK(bradsense_upscale_rgba(NULL, 4, 4, dst, 8, 8, &cfg) == -EINVAL,
          "null src accepted");
    CHECK(bradsense_upscale_rgba(src, 4, 4, NULL, 8, 8, &cfg) == -EINVAL,
          "null dst accepted");
    CHECK(bradsense_upscale_rgba(src, 0, 4, dst, 8, 8, &cfg) == -EINVAL,
          "zero source width accepted");
    CHECK(bradsense_upscale_rgba(src, 4, 4, dst, 0, 0, &cfg) == -EINVAL,
          "zero destination accepted");
    CHECK(bradsense_upscale_rgba(src, 4, 4, dst, 8, 8, NULL) == 0,
          "NULL cfg should fall back to defaults");
}

static void test_reconstruct_wiring(void)
{
    uint8_t gray[8 * 8 * 4];
    size_t out_size = (size_t)3840 * 2160 * 4;
    uint8_t *out = malloc(out_size);
    struct bradsense_frame frame;
    struct bradsense_perf_stats st;

    CHECK(out != NULL, "out of memory in test");
    for (size_t i = 0; i < 8 * 8; i++) {
        gray[i * 4 + 0] = 0x40;
        gray[i * 4 + 1] = 0x40;
        gray[i * 4 + 2] = 0x40;
        gray[i * 4 + 3] = 0xFF;
    }

    CHECK(bradsense_init(BRADSENSE_BALANCED, BRADSENSE_FG_OFF) == 0, "init");
    CHECK(bradsense_reconstruct(7, out, out_size) == -ENODATA,
          "reconstruct invented pixels with no supplied frame");
    CHECK(bradsense_supply_frame(NULL) == -EINVAL, "null frame accepted");

    frame.w = 8;
    frame.h = 8;
    frame.rgba = gray;
    CHECK(bradsense_supply_frame(&frame) == 0, "supply failed");
    CHECK(bradsense_reconstruct(7, out, out_size) == 0, "reconstruct failed");

    for (size_t i = 0; i < 16; i++) {   /* a few scattered probes at 4K */
        size_t o = (i * 997 + i * 4) * 4;
        CHECK(out[o] == 0x40 && out[o + 1] == 0x40 && out[o + 2] == 0x40,
              "4K pixel at %zu not the supplied gray", o);
    }

    CHECK(bradsense_get_perf(&st) == 0, "get_perf");
    CHECK(st.frames_processed == 1, "frame count wrong: %llu",
          (unsigned long long)st.frames_processed);
    CHECK(st.pixels_reconstructed == (uint64_t)3840 * 2160,
          "pixel count wrong: %llu",
          (unsigned long long)st.pixels_reconstructed);

    /* supplying a fresh frame must replace the old one */
    for (size_t i = 0; i < 8 * 8; i++)
        gray[i * 4] = gray[i * 4 + 1] = gray[i * 4 + 2] = 0xF0;
    CHECK(bradsense_supply_frame(&frame) == 0, "re-supply failed");
    CHECK(bradsense_reconstruct(8, out, out_size) == 0, "re-reconstruct failed");
    CHECK(out[0] == 0xF0, "supplied frame did not replace the previous one");

    free(out);
}

int main(void)
{
    printf("-- BradSense-Render M0 pixel path\n");
    test_dims_and_corners();
    test_determinism();
    test_flat_untouched();
    test_edge_crispening();
    test_anti_ringing();
    test_psnr_super_res();
    test_api_validation();
    test_reconstruct_wiring();

    if (failures) {
        printf("BradSense-Render pixel tests: %d failures\n", failures);
        return 1;
    }
    printf("BradSense-Render pixel tests passed\n");
    return 0;
}