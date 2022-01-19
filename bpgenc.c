/*
 * BPG encoder
 *
 * Copyright (c) 2014 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <getopt.h>
#include <math.h>
#include <assert.h>

#include <png.h>
#include <jpeglib.h>

#include "bpgenc.h"

typedef uint16_t PIXEL;

static void put_ue(uint8_t **pp, uint32_t v);

static inline int clamp_pix(int a, int pixel_max)
{
    if (a < 0)
        return 0;
    else if (a > pixel_max)
        return pixel_max;
    else
        return a;
}

static inline int sub_mod_int(int a, int b, int m)
{
    a -= b;
    if (a < 0)
        a += m;
    return a;
}

static inline int add_mod_int(int a, int b, int m)
{
    a += b;
    if (a >= m)
        a -= m;
    return a;
}

typedef struct {
    int c_shift;
    int c_rnd;
    int c_0_25, c_0_5, c_one;
    int rgb_to_ycc[3 * 3];
    int y_one;
    int y_offset;
    int bit_depth;
    int pixel_max;
    int c_center;
} ColorConvertState;

static void convert_init(ColorConvertState *s, int in_bit_depth, 
                         int out_bit_depth, BPGColorSpaceEnum color_space,
                         int limited_range)
{
    double k_r, k_b, mult, mult_y, mult_c;
    int in_pixel_max, out_pixel_max, c_shift, i;
    double rgb_to_ycc[3 * 3];

    /* XXX: could use one more bit */
    c_shift = 31 - out_bit_depth;
    in_pixel_max = (1 << in_bit_depth) - 1;
    out_pixel_max = (1 << out_bit_depth) - 1;
    mult = (double)out_pixel_max * (1 << c_shift) / (double)in_pixel_max;
    //    printf("mult=%f c_shift=%d\n", mult, c_shift);
    if (limited_range) {
        mult_y = (double)(219 << (out_bit_depth - 8)) * (1 << c_shift) / 
            (double)in_pixel_max;
        mult_c = (double)(224 << (out_bit_depth - 8)) * (1 << c_shift) / 
            (double)in_pixel_max;
    } else {
        mult_y = mult;
        mult_c = mult;
    }
    switch(color_space) {
    case BPG_CS_YCbCr:
        k_r = 0.299;
        k_b = 0.114;
        goto convert_ycc;
        
    case BPG_CS_YCbCr_BT709:
        k_r = 0.2126; 
        k_b = 0.0722;
        goto convert_ycc;
        
    case BPG_CS_YCbCr_BT2020:
        k_r = 0.2627;
        k_b = 0.0593;
    convert_ycc:
        rgb_to_ycc[0] = k_r;
        rgb_to_ycc[1] = 1 - k_r - k_b;
        rgb_to_ycc[2] = k_b;
        rgb_to_ycc[3] = -0.5 * k_r / (1 - k_b);
        rgb_to_ycc[4] = -0.5 * (1 - k_r - k_b) / (1 - k_b);
        rgb_to_ycc[5] = 0.5;
        rgb_to_ycc[6] = 0.5;
        rgb_to_ycc[7] = -0.5 * (1 - k_r - k_b) / (1 - k_r);
        rgb_to_ycc[8] = -0.5 * k_b / (1 - k_r);
        
        for(i = 0; i < 3; i++)
            s->rgb_to_ycc[i] = lrint(rgb_to_ycc[i] * mult_y);
        for(i = 3; i < 9; i++)
            s->rgb_to_ycc[i] = lrint(rgb_to_ycc[i] * mult_c);
        break;
    case BPG_CS_YCgCo:
        s->c_0_25 = lrint(0.25 * mult_y);
        s->c_0_5 = lrint(0.5 * mult_y);
        break;
    default:
        break;
    }

    s->c_one = lrint(mult);
    s->c_shift = c_shift;
    s->c_rnd = (1 << (c_shift - 1));
    if (limited_range) {
        s->y_offset = s->c_rnd + (16 << (c_shift + out_bit_depth - 8));
        s->y_one = lrint(mult_y);
    } else {
        s->y_offset = s->c_rnd;
        s->y_one = s->c_one;
    }

    s->bit_depth = out_bit_depth;
    s->c_center = 1 << (out_bit_depth - 1);
    s->pixel_max = out_pixel_max;
}

/* 8 bit input */
static void rgb24_to_ycc(ColorConvertState *s,
                         PIXEL *y_ptr, PIXEL *cb_ptr, PIXEL *cr_ptr,
                         const void *src1, int n, int incr)
{
    const uint8_t *src = src1;
    int i, r, g, b, c0, c1, c2, c3, c4, c5, c6, c7, c8, shift, rnd, center;
    int pixel_max, y_offset;

    c0 = s->rgb_to_ycc[0];
    c1 = s->rgb_to_ycc[1];
    c2 = s->rgb_to_ycc[2];
    c3 = s->rgb_to_ycc[3];
    c4 = s->rgb_to_ycc[4];
    c5 = s->rgb_to_ycc[5];
    c6 = s->rgb_to_ycc[6];
    c7 = s->rgb_to_ycc[7];
    c8 = s->rgb_to_ycc[8];
    shift = s->c_shift;
    rnd = s->c_rnd;
    y_offset = s->y_offset;
    center = s->c_center;
    pixel_max = s->pixel_max;
    for(i = 0; i < n; i++) {
        r = src[0];
        g = src[1];
        b = src[2];
        y_ptr[i] = clamp_pix((c0 * r + c1 * g + c2 * b +
                              y_offset) >> shift, pixel_max);
        cb_ptr[i] = clamp_pix(((c3 * r + c4 * g + c5 * b + 
                                rnd) >> shift) + center, pixel_max);
        cr_ptr[i] = clamp_pix(((c6 * r + c7 * g + c8 * b + 
                                rnd) >> shift) + center, pixel_max);
        src += incr;
    }
}

static void rgb24_to_rgb(ColorConvertState *s,
                         PIXEL *y_ptr, PIXEL *cb_ptr, PIXEL *cr_ptr,
                         const void *src1, int n, int incr)
{
    const uint8_t *src = src1;
    int i, r, g, b, c, shift, rnd;

    c = s->y_one;
    shift = s->c_shift;
    rnd = s->y_offset;
    for(i = 0; i < n; i++) {
        r = src[0];
        g = src[1];
        b = src[2];
        y_ptr[i] = (c * g + rnd) >> shift;
        cb_ptr[i] = (c * b + rnd) >> shift;
        cr_ptr[i] = (c * r + rnd) >> shift;
        src += incr;
    }
}

static void rgb24_to_ycgco(ColorConvertState *s,
                           PIXEL *y_ptr, PIXEL *cb_ptr, PIXEL *cr_ptr,
                           const void *src1, int n, int incr)
{
    const uint8_t *src = src1;
    int i, r, g, b, t1, t2, pixel_max, c_0_5, c_0_25, rnd, shift, center;
    int y_offset;

    c_0_25 = s->c_0_25;
    c_0_5 = s->c_0_5;
    rnd = s->c_rnd;
    shift = s->c_shift;
    pixel_max = s->pixel_max;
    center = s->c_center;
    y_offset = s->y_offset;
    for(i = 0; i < n; i++) {
        r = src[0];
        g = src[1];
        b = src[2];
        t1 = c_0_5 * g;
        t2 = c_0_25 * (r + b);
        y_ptr[i] = clamp_pix((t1 + t2 + y_offset) >> shift, pixel_max);
        cb_ptr[i] = clamp_pix(((t1 - t2 + rnd) >> shift) + center, 
                              pixel_max);
        cr_ptr[i] = clamp_pix(((c_0_5 * (r - b) +
                                rnd) >> shift) + center, pixel_max);
        src += incr;
    }
}

/* Note: used for alpha/W so no limited range */
static void gray8_to_gray(ColorConvertState *s,
                          PIXEL *y_ptr, const uint8_t *src, int n, int incr)
{
    int i, g, c, shift, rnd;

    c = s->c_one;
    shift = s->c_shift;
    rnd = s->c_rnd;
    for(i = 0; i < n; i++) {
        g = src[0];
        y_ptr[i] = (c * g + rnd) >> shift;
        src += incr;
    }
}

static void luma8_to_gray(ColorConvertState *s,
                          PIXEL *y_ptr, const uint8_t *src, int n, int incr)
{
    int i, g, c, shift, rnd;

    c = s->y_one;
    shift = s->c_shift;
    rnd = s->y_offset;
    for(i = 0; i < n; i++) {
        g = src[0];
        y_ptr[i] = (c * g + rnd) >> shift;
        src += incr;
    }
}

/* 16 bit input */

static void rgb48_to_ycc(ColorConvertState *s, 
                         PIXEL *y_ptr, PIXEL *cb_ptr, PIXEL *cr_ptr,
                         const void *src1, int n, int incr)
{
    const uint16_t *src = src1;
    int i, r, g, b, c0, c1, c2, c3, c4, c5, c6, c7, c8, shift, rnd, center;
    int pixel_max, y_offset;

    c0 = s->rgb_to_ycc[0];
    c1 = s->rgb_to_ycc[1];
    c2 = s->rgb_to_ycc[2];
    c3 = s->rgb_to_ycc[3];
    c4 = s->rgb_to_ycc[4];
    c5 = s->rgb_to_ycc[5];
    c6 = s->rgb_to_ycc[6];
    c7 = s->rgb_to_ycc[7];
    c8 = s->rgb_to_ycc[8];
    shift = s->c_shift;
    rnd = s->c_rnd;
    y_offset = s->y_offset;
    center = s->c_center;
    pixel_max = s->pixel_max;
    for(i = 0; i < n; i++) {
        r = src[0];
        g = src[1];
        b = src[2];
        y_ptr[i] = clamp_pix((c0 * r + c1 * g + c2 * b +
                              y_offset) >> shift, pixel_max);
        cb_ptr[i] = clamp_pix(((c3 * r + c4 * g + c5 * b + 
                                rnd) >> shift) + center, pixel_max);
        cr_ptr[i] = clamp_pix(((c6 * r + c7 * g + c8 * b + 
                                rnd) >> shift) + center, pixel_max);
        src += incr;
    }
}

static void rgb48_to_ycgco(ColorConvertState *s, 
                           PIXEL *y_ptr, PIXEL *cb_ptr, PIXEL *cr_ptr,
                           const void *src1, int n, int incr)
{
    const uint16_t *src = src1;
    int i, r, g, b, t1, t2, pixel_max, c_0_5, c_0_25, rnd, shift, center;
    int y_offset;

    c_0_25 = s->c_0_25;
    c_0_5 = s->c_0_5;
    rnd = s->c_rnd;
    y_offset = s->y_offset;
    shift = s->c_shift;
    pixel_max = s->pixel_max;
    center = s->c_center;
    for(i = 0; i < n; i++) {
        r = src[0];
        g = src[1];
        b = src[2];
        t1 = c_0_5 * g;
        t2 = c_0_25 * (r + b);
        y_ptr[i] = clamp_pix((t1 + t2 + y_offset) >> shift, pixel_max);
        cb_ptr[i] = clamp_pix(((t1 - t2 + rnd) >> shift) + center, 
                              pixel_max);
        cr_ptr[i] = clamp_pix(((c_0_5 * (r - b) +
                                rnd) >> shift) + center, pixel_max);
        src += incr;
    }
}

/* Note: use for alpha/W so no limited range */
static void gray16_to_gray(ColorConvertState *s, 
                           PIXEL *y_ptr, const uint16_t *src, int n, int incr)
{
    int i, g, c, shift, rnd;

    c = s->c_one;
    shift = s->c_shift;
    rnd = s->c_rnd;
    for(i = 0; i < n; i++) {
        g = src[0];
        y_ptr[i] = (c * g + rnd) >> shift;
        src += incr;
    }
}

static void luma16_to_gray(ColorConvertState *s, 
                           PIXEL *y_ptr, const uint16_t *src, int n, int incr)
{
    int i, g, c, shift, rnd;

    c = s->y_one;
    shift = s->c_shift;
    rnd = s->y_offset;
    for(i = 0; i < n; i++) {
        g = src[0];
        y_ptr[i] = (c * g + rnd) >> shift;
        src += incr;
    }
}

static void rgb48_to_rgb(ColorConvertState *s, 
                         PIXEL *y_ptr, PIXEL *cb_ptr, PIXEL *cr_ptr,
                         const void *src1, int n, int incr)
{
    const uint16_t *src = src1;

    luma16_to_gray(s, y_ptr, src + 1, n, incr);
    luma16_to_gray(s, cb_ptr, src + 2, n, incr);
    luma16_to_gray(s, cr_ptr, src + 0, n, incr);
}

typedef void RGBConvertFunc(ColorConvertState *s, 
                            PIXEL *y_ptr, PIXEL *cb_ptr, PIXEL *cr_ptr,
                            const void *src, int n, int incr);

static RGBConvertFunc *rgb_to_cs[2][BPG_CS_COUNT] = {
    {
        rgb24_to_ycc,
        rgb24_to_rgb,
        rgb24_to_ycgco,
        rgb24_to_ycc,
        rgb24_to_ycc,
    },
    {
        rgb48_to_ycc,
        rgb48_to_rgb,
        rgb48_to_ycgco,
        rgb48_to_ycc,
        rgb48_to_ycc,
    }
};
    
/* val = 1.0 - val */
static void gray_one_minus(ColorConvertState *s, PIXEL *y_ptr, int n)
{
    int pixel_max = s->pixel_max;
    int i;

    for(i = 0; i < n; i++) {
        y_ptr[i] = pixel_max - y_ptr[i];
    }
}

/* val = -val for chroma */
static void gray_neg_c(ColorConvertState *s, PIXEL *y_ptr, int n)
{
    int pixel_max = s->pixel_max;
    int i, v;

    for(i = 0; i < n; i++) {
        v = y_ptr[i];
        if (v == 0)
            v = pixel_max;
        else
            v = pixel_max + 1 - v;
        y_ptr[i] = v;
    }
}


/* decimation */

/* phase = 0 */
#define DP0TAPS2 7
#define DP0TAPS (2 * DP0TAPS + 1)
#define DP0C0 64
#define DP0C1 40
#define DP0C3 (-11)
#define DP0C5 4
#define DP0C7 (-1)

/* phase = 0.5 */
#define DP1TAPS2 5
#define DP1TAPS (2 * DP1TAPS2)
#define DP1C0 57
#define DP1C1 17
#define DP1C2 (-8)
#define DP1C3 (-4)
#define DP1C4 2

#define DTAPS_MAX 7

/* chroma aligned with luma samples */
static void decimate2p0_simple(PIXEL *dst, PIXEL *src, int n, int bit_depth)
{
    int n2, i, pixel_max;
    pixel_max = (1 << bit_depth) - 1;
    n2 = (n + 1) / 2;
    for(i = 0; i < n2; i++) {
        dst[i] = clamp_pix(((src[-7] + src[7]) * DP0C7 + 
                            (src[-5] + src[5]) * DP0C5 + 
                            (src[-3] + src[3]) * DP0C3 + 
                            (src[-1] + src[1]) * DP0C1 + 
                            src[0] * DP0C0 + 64) >> 7, pixel_max);
        src += 2;
    }
}

/* same with more precision and no saturation */
static void decimate2p0_simple16(int16_t *dst, PIXEL *src, int n, int bit_depth)
{
    int n2, i, shift, rnd;
    shift = bit_depth - 7;
    rnd = 1 << (shift - 1);
    n2 = (n + 1) / 2;
    for(i = 0; i < n2; i++) {
        dst[i] = ((src[-7] + src[7]) * DP0C7 + 
                  (src[-5] + src[5]) * DP0C5 + 
                  (src[-3] + src[3]) * DP0C3 + 
                  (src[-1] + src[1]) * DP0C1 + 
                  src[0] * DP0C0 + rnd) >> shift;
    src += 2;
    }
}


/* chroma half way between luma samples */
static void decimate2p1_simple(PIXEL *dst, PIXEL *src, int n, int bit_depth)
{
    int n2, i, pixel_max;
    pixel_max = (1 << bit_depth) - 1;
    n2 = (n + 1) / 2;
    for(i = 0; i < n2; i++) {
        dst[i] = clamp_pix(((src[-4] + src[5]) * DP1C4 + 
                            (src[-3] + src[4]) * DP1C3 + 
                            (src[-2] + src[3]) * DP1C2 + 
                            (src[-1] + src[2]) * DP1C1 + 
                            (src[0] + src[1]) * DP1C0 + 64) >> 7, pixel_max);
        src += 2;
    }
}

/* same with more precision and no saturation */
static void decimate2p1_simple16(int16_t *dst, PIXEL *src, int n, int bit_depth)
{
    int n2, i, shift, rnd;
    shift = bit_depth - 7;
    rnd = 1 << (shift - 1);
    n2 = (n + 1) / 2;
    for(i = 0; i < n2; i++) {
        dst[i] = ((src[-4] + src[5]) * DP1C4 + 
                  (src[-3] + src[4]) * DP1C3 + 
                  (src[-2] + src[3]) * DP1C2 + 
                  (src[-1] + src[2]) * DP1C1 + 
                  (src[0] + src[1]) * DP1C0 + rnd) >> shift;
        src += 2;
    }
}

static void decimate2_h(PIXEL *dst, PIXEL *src, int n, int bit_depth, int phase)
{
    PIXEL *src1, v;
    int d, i;

    if (phase == 0) 
        d = DP0TAPS2;
    else
        d = DP1TAPS2;
    /* add edge pixels */
    src1 = malloc(sizeof(PIXEL) * (n + 2 * d));
    v = src[0];
    for(i = 0; i < d; i++)
        src1[i] = v;
    memcpy(src1 + d, src, n * sizeof(PIXEL));
    v = src[n - 1];
    for(i = 0; i < d; i++)
        src1[d + n + i] = v;
    if (phase == 0)
        decimate2p0_simple(dst, src1 + d, n, bit_depth);
    else
        decimate2p1_simple(dst, src1 + d, n, bit_depth);
    free(src1);
}

/* src1 is a temporary buffer of length n + 2 * DTAPS */
static void decimate2_h16(int16_t *dst, PIXEL *src, int n, PIXEL *src1,
                          int bit_depth, int phase)
{
    PIXEL v;
    int d, i;

    if (phase == 0) 
        d = DP0TAPS2;
    else
        d = DP1TAPS2;
    /* add edge pixels */
    v = src[0];
    for(i = 0; i < d; i++)
        src1[i] = v;
    memcpy(src1 + d, src, n * sizeof(PIXEL));
    v = src[n - 1];
    for(i = 0; i < d; i++)
        src1[d + n + i] = v;
    if (phase == 0)
        decimate2p0_simple16(dst, src1 + d, n, bit_depth);
    else
        decimate2p1_simple16(dst, src1 + d, n, bit_depth);
        
}

static void decimate2_v(PIXEL *dst, int16_t **src, int pos, int n,
                        int bit_depth)
{
    int16_t *src0, *src1, *src2, *src3, *src4, *src5, *srcm1, *srcm2, *srcm3, *srcm4;
    int i, shift, offset, pixel_max;

    pos = sub_mod_int(pos, 4, DP1TAPS);
    srcm4 = src[pos];
    pos = add_mod_int(pos, 1, DP1TAPS);
    srcm3 = src[pos];
    pos = add_mod_int(pos, 1, DP1TAPS);
    srcm2 = src[pos];
    pos = add_mod_int(pos, 1, DP1TAPS);
    srcm1 = src[pos];
    pos = add_mod_int(pos, 1, DP1TAPS);
    src0 = src[pos];
    pos = add_mod_int(pos, 1, DP1TAPS);
    src1 = src[pos];
    pos = add_mod_int(pos, 1, DP1TAPS);
    src2 = src[pos];
    pos = add_mod_int(pos, 1, DP1TAPS);
    src3 = src[pos];
    pos = add_mod_int(pos, 1, DP1TAPS);
    src4 = src[pos];
    pos = add_mod_int(pos, 1, DP1TAPS);
    src5 = src[pos];
    
    shift = 21 - bit_depth;
    offset = 1 << (shift - 1);
    pixel_max = (1 << bit_depth) - 1;
    for(i = 0; i < n; i++) {
        dst[i] = clamp_pix(((srcm4[i] + src5[i]) * DP1C4 + 
                            (srcm3[i] + src4[i]) * DP1C3 + 
                            (srcm2[i] + src3[i]) * DP1C2 + 
                            (srcm1[i] + src2[i]) * DP1C1 + 
                            (src0[i] + src1[i]) * DP1C0 + offset) >> shift, pixel_max);
    }
}

/* Note: we do the horizontal decimation first to use less CPU cache */
static void decimate2_hv(uint8_t *dst, int dst_linesize,
                         uint8_t *src, int src_linesize, 
                         int w, int h, int bit_depth, int h_phase)
{
    PIXEL *buf1;
    int16_t *buf2[DP1TAPS];
    int w2, pos, i, y, y1, y2;
    
    w2 = (w + 1) / 2;

    buf1 = malloc(sizeof(PIXEL) * (w + 2 * DTAPS_MAX));
    /* init line buffer */
    for(i = 0; i < DP1TAPS; i++) {
        buf2[i] = malloc(sizeof(int16_t) * w2);
        y = i;
        if (y > DP1TAPS2)
            y -= DP1TAPS;
        if (y < 0) {
            /* copy from first line */
            memcpy(buf2[i], buf2[0], sizeof(int16_t) * w2);
        } else if (y >= h) {
            /* copy from last line (only happens for small height) */
            memcpy(buf2[i], buf2[h - 1], sizeof(int16_t) * w2);
        } else {
            decimate2_h16(buf2[i], (PIXEL *)(src + src_linesize * y), w,
                          buf1, bit_depth, h_phase);
        }
    }

    for(y = 0; y < h; y++) {
        pos = y % DP1TAPS;
        if ((y & 1) == 0) {
            /* filter one line */
            y2 = y >> 1;
            decimate2_v((PIXEL *)(dst + y2 * dst_linesize), buf2,
                        pos, w2, bit_depth);
        }
        /* add a new line in the buffer */
        y1 = y + DP1TAPS2 + 1;
        pos = add_mod_int(pos, DP1TAPS2 + 1, DP1TAPS);
        if (y1 >= h) {
            /* copy last line */
            memcpy(buf2[pos], buf2[sub_mod_int(pos, 1, DP1TAPS)],
                   sizeof(int16_t) * w2);
        } else {
            /* horizontally decimate new line */
            decimate2_h16(buf2[pos], (PIXEL *)(src + src_linesize * y1), w,
                          buf1, bit_depth, h_phase);
        }
    }

    for(i = 0; i < DP1TAPS; i++)
        free(buf2[i]);
    free(buf1);
}

static void get_plane_res(Image *img, int *pw, int *ph, int i)
{
    if (img->format == BPG_FORMAT_420 && (i == 1 || i == 2)) {
        *pw = (img->w + 1) / 2;
        *ph = (img->h + 1) / 2;
    } else if (img->format == BPG_FORMAT_422 && (i == 1 || i == 2)) {
        *pw = (img->w + 1) / 2;
        *ph = img->h;
    } else {
        *pw = img->w;
        *ph = img->h;
    }
}

#define W_PAD 16

Image *image_alloc(int w, int h, BPGImageFormatEnum format, int has_alpha,
                   BPGColorSpaceEnum color_space, int bit_depth)
{
    Image *img;
    int i, linesize, w1, h1, c_count;
    uint64_t size;

    /* XXX: support large images ? */
    size = (uint64_t)w * (uint64_t)h * 2;
    if (size > INT32_MAX) {
        fprintf(stderr, "Image is too large\n");
        exit(1);
    }

    img = malloc(sizeof(Image));
    memset(img, 0, sizeof(*img));
    
    img->w = w;
    img->h = h;
    img->format = format;
    img->has_alpha = has_alpha;
    img->bit_depth = bit_depth;
    img->color_space = color_space;
    img->pixel_shift = 1;
    img->c_h_phase = 1;

    if (img->format == BPG_FORMAT_GRAY)
        c_count = 1;
    else
        c_count = 3;
    if (has_alpha)
        c_count++;
    for(i = 0; i < c_count; i++) {
        get_plane_res(img, &w1, &h1, i);
        /* multiple of 16 pixels to add borders */
        w1 = (w1 + (W_PAD - 1)) & ~(W_PAD - 1);
        h1 = (h1 + (W_PAD - 1)) & ~(W_PAD - 1);
        
        linesize = w1 << img->pixel_shift;
        img->data[i] = malloc(linesize * h1);
        img->linesize[i] = linesize;
    }
    return img;
}

void image_free(Image *img)
{
    int i, c_count;
    if (img->format == BPG_FORMAT_GRAY)
        c_count = 1;
    else
        c_count = 3;
    if (img->has_alpha)
        c_count++;
    for(i = 0; i < c_count; i++)
        free(img->data[i]);
    free(img);
}

int image_ycc444_to_ycc422(Image *img, int h_phase)
{
    uint8_t *data1;
    int w1, h1, bpp, linesize1, i, y;

    if (img->format != BPG_FORMAT_444 || img->pixel_shift != 1)
        return -1;
    bpp = 2;
    w1 = (img->w + 1) / 2;
    w1 = (w1 + (W_PAD - 1)) & ~(W_PAD - 1);
    h1 = (img->h + (W_PAD - 1)) & ~(W_PAD - 1);
    linesize1 = bpp * w1;
    for(i = 1; i <= 2; i++) {
        data1 = malloc(linesize1 * h1);
        for(y = 0; y < img->h; y++) {
            decimate2_h((PIXEL *)(data1 + y * linesize1),
                        (PIXEL *)(img->data[i] + y * img->linesize[i]),
                        img->w, img->bit_depth, h_phase);
        }
        free(img->data[i]);
        img->data[i] = data1;
        img->linesize[i] = linesize1;
    }
    img->format = BPG_FORMAT_422;
    img->c_h_phase = h_phase;
    return 0;
}

int image_ycc444_to_ycc420(Image *img, int h_phase)
{
    uint8_t *data1;
    int w1, h1, bpp, linesize1, i;

    if (img->format != BPG_FORMAT_444 || img->pixel_shift != 1)
        return -1;
    bpp = 2;
    w1 = (img->w + 1) / 2;
    h1 = (img->h + 1) / 2;
    w1 = (w1 + (W_PAD - 1)) & ~(W_PAD - 1);
    h1 = (h1 + (W_PAD - 1)) & ~(W_PAD - 1);
    linesize1 = bpp * w1;
    for(i = 1; i <= 2; i++) {
        data1 = malloc(linesize1 * h1);
        decimate2_hv(data1, linesize1,
                     img->data[i], img->linesize[i],
                     img->w, img->h, img->bit_depth, h_phase);
        free(img->data[i]);
        img->data[i] = data1;
        img->linesize[i] = linesize1;
    }
    img->format = BPG_FORMAT_420;
    img->c_h_phase = h_phase;
    return 0;
}

/* duplicate right and bottom samples so that the image has a width
   and height multiple of cb_size (power of two) */
void image_pad(Image *img, int cb_size)
{
    int w1, h1, x, y, c_count, c_w, c_h, c_w1, c_h1, h_shift, v_shift, c_idx;
    PIXEL *ptr, v, *ptr1;

    assert(img->pixel_shift == 1);
    if (cb_size <= 1)
        return;
    w1 = (img->w + cb_size - 1) & ~(cb_size - 1);
    h1 = (img->h + cb_size - 1) & ~(cb_size - 1);
    
    if (img->format == BPG_FORMAT_GRAY)
        c_count = 1;
    else
        c_count = 3;
    if (img->has_alpha)
        c_count++;
    for(c_idx = 0; c_idx < c_count; c_idx++) {
        if (img->format == BPG_FORMAT_420 && 
            (c_idx == 1 || c_idx == 2)) {
            h_shift = 1;
            v_shift = 1;
        } else if (img->format == BPG_FORMAT_422 && 
                   (c_idx == 1 || c_idx == 2)) {
            h_shift = 1;
            v_shift = 0;
        } else {
            h_shift = 0;
            v_shift = 0;
        }

        c_w = (img->w + h_shift) >> h_shift;
        c_h = (img->h + v_shift) >> v_shift;
        c_w1 = w1 >> h_shift;
        c_h1 = h1 >> v_shift;

        /* pad horizontally */
        for(y = 0; y < c_h; y++) {
            ptr = (PIXEL *)(img->data[c_idx] + img->linesize[c_idx] * y);
            v = ptr[c_w - 1];
            for(x = c_w; x < c_w1; x++) {
                ptr[x] = v;
            }
        }

        /* pad vertically */
        ptr1 = (PIXEL *)(img->data[c_idx] + img->linesize[c_idx] * (c_h - 1));
        for(y = c_h; y < c_h1; y++) {
            ptr = (PIXEL *)(img->data[c_idx] + img->linesize[c_idx] * y);
            memcpy(ptr, ptr1, c_w1 * sizeof(PIXEL));
        }
    }
    img->w = w1;
    img->h = h1;
}

/* convert the 16 bit components to 8 bits */
void image_convert16to8(Image *img)
{
    int w, h, stride, y, x, c_count, i;
    uint8_t *plane;

    if (img->bit_depth > 8 || img->pixel_shift != 1)
        return;
    if (img->format == BPG_FORMAT_GRAY)
        c_count = 1;
    else
        c_count = 3;
    if (img->has_alpha)
        c_count++;
    for(i = 0; i < c_count; i++) {
        get_plane_res(img, &w, &h, i);
        stride = w;
        plane = malloc(stride * h);
        for(y = 0; y < h; y++) {
            const uint16_t *src;
            uint8_t *dst;
            dst = plane + stride * y;
            src = (uint16_t *)(img->data[i] + img->linesize[i] * y);
            for(x = 0; x < w; x++)
                dst[x] = src[x];
        }
        free(img->data[i]);
        img->data[i] = plane;
        img->linesize[i] = stride;
    }
    img->pixel_shift = 0;
}

typedef struct BPGMetaData {
    uint32_t tag;
    uint8_t *buf;
    int buf_len;
    struct BPGMetaData *next;
} BPGMetaData;

BPGMetaData *bpg_md_alloc(uint32_t tag)
{
    BPGMetaData *md;
    md = malloc(sizeof(BPGMetaData));
    memset(md, 0, sizeof(*md));
    md->tag = tag;
    return md;
}

void bpg_md_free(BPGMetaData *md)
{
    BPGMetaData *md_next;

    while (md != NULL) {
        md_next = md->next;
        free(md->buf);
        free(md);
        md = md_next;
    }
}

Image *read_png(BPGMetaData **pmd,
                FILE *f, BPGColorSpaceEnum color_space, int out_bit_depth,
                int limited_range, int premultiplied_alpha)
{
    png_structp png_ptr;
    png_infop info_ptr;
    int bit_depth, color_type;
    Image *img;
    uint8_t **rows;
    int y, has_alpha, linesize, bpp;
    BPGImageFormatEnum format;
    ColorConvertState cvt_s, *cvt = &cvt_s;
    BPGMetaData *md, **plast_md, *first_md;
    
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                     NULL, NULL, NULL);
    if (png_ptr == NULL) {
        return NULL;
    }
    
    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
       png_destroy_read_struct(&png_ptr, NULL, NULL);
       return NULL;
    }
    
    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }
    
    png_init_io(png_ptr, f);
    
    png_read_info(png_ptr, info_ptr);
    
    bit_depth   = png_get_bit_depth(png_ptr, info_ptr);
    color_type = png_get_color_type(png_ptr, info_ptr);

    switch (color_type) {
    case PNG_COLOR_TYPE_PALETTE:
        png_set_palette_to_rgb(png_ptr);
        bit_depth = 8;
        break;
    case PNG_COLOR_TYPE_GRAY:
    case PNG_COLOR_TYPE_GRAY_ALPHA:
        if (bit_depth < 8) {
            png_set_expand_gray_1_2_4_to_8(png_ptr);
            bit_depth = 8;
        }
        break;
    }
    assert(bit_depth == 8 || bit_depth == 16);

#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
    if (bit_depth == 16) {
        png_set_swap(png_ptr);
    }
#endif

    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        format = BPG_FORMAT_GRAY;
        color_space = BPG_CS_YCbCr;
    } else {
        format = BPG_FORMAT_444;
    }
    
    has_alpha = (color_type == PNG_COLOR_TYPE_GRAY_ALPHA ||
                 color_type == PNG_COLOR_TYPE_RGB_ALPHA);

    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png_ptr);
        has_alpha = 1;
    }

    if (premultiplied_alpha) {
        png_set_alpha_mode(png_ptr, PNG_ALPHA_ASSOCIATED, PNG_GAMMA_LINEAR);
    }

    img = image_alloc(png_get_image_width(png_ptr, info_ptr),
                      png_get_image_height(png_ptr, info_ptr),
                      format, has_alpha, color_space,
                      out_bit_depth);
    img->limited_range = limited_range;
    img->premultiplied_alpha = premultiplied_alpha;

    rows = malloc(sizeof(rows[0]) * img->h);
    if (format == BPG_FORMAT_GRAY)
        bpp = (1 + has_alpha) * (bit_depth / 8);
    else
        bpp = (3 + has_alpha) * (bit_depth / 8);
    linesize = bpp * img->w;
    for (y = 0; y < img->h; y++) {
        rows[y] = malloc(linesize);
    }
    
    png_read_image(png_ptr, rows);
    
    convert_init(cvt, bit_depth, out_bit_depth, color_space, limited_range);

    if (format != BPG_FORMAT_GRAY) {
        int idx;
        RGBConvertFunc *convert_func;

        idx = (bit_depth == 16);
        convert_func = rgb_to_cs[idx][color_space];
        
        for (y = 0; y < img->h; y++) {
            convert_func(cvt, (PIXEL *)(img->data[0] + y * img->linesize[0]),
                         (PIXEL *)(img->data[1] + y * img->linesize[1]),
                         (PIXEL *)(img->data[2] + y * img->linesize[2]),
                         rows[y], img->w, 3 + has_alpha);
            if (has_alpha) {
                if (idx) {
                    gray16_to_gray(cvt, (PIXEL *)(img->data[3] + y * img->linesize[3]),
                                   (uint16_t *)rows[y] + 3, img->w, 4);
                } else {
                    gray8_to_gray(cvt, (PIXEL *)(img->data[3] + y * img->linesize[3]),
                                  rows[y] + 3, img->w, 4);
                }
            }
        }
    } else {
        if (bit_depth == 16) {
            for (y = 0; y < img->h; y++) {
                luma16_to_gray(cvt, (PIXEL *)(img->data[0] + y * img->linesize[0]),
                               (uint16_t *)rows[y], img->w, 1 + has_alpha);
                if (has_alpha) {
                    gray16_to_gray(cvt, (PIXEL *)(img->data[1] + y * img->linesize[1]),
                                   (uint16_t *)rows[y] + 1, img->w, 2);
                }
            }
        } else {
            for (y = 0; y < img->h; y++) {
                luma8_to_gray(cvt, (PIXEL *)(img->data[0] + y * img->linesize[0]),
                              rows[y], img->w, 1 + has_alpha);
                if (has_alpha) {
                    gray8_to_gray(cvt, (PIXEL *)(img->data[1] + y * img->linesize[1]),
                                  rows[y] + 1, img->w, 2);
                }
            }
        }
    }

    for (y = 0; y < img->h; y++) {
        free(rows[y]);
    }
    free(rows);
        
    png_read_end(png_ptr, info_ptr);
    
    /* get the ICC profile if present */
    first_md = NULL;
    plast_md = &first_md;
    {
        png_charp name;
        int comp_type;
        png_bytep iccp_buf;
        png_uint_32 iccp_buf_len;
        
        if (png_get_iCCP(png_ptr, info_ptr,
                         &name, &comp_type, &iccp_buf, &iccp_buf_len) == 
            PNG_INFO_iCCP) {
            md = bpg_md_alloc(BPG_EXTENSION_TAG_ICCP);
            md->buf_len = iccp_buf_len;
            md->buf = malloc(iccp_buf_len);
            memcpy(md->buf, iccp_buf, iccp_buf_len);
            *plast_md = md;
            plast_md = &md->next;
        }
    }

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    
    *pmd = first_md;
    return img;
}

static BPGMetaData *jpeg_get_metadata(jpeg_saved_marker_ptr first_marker)
{
    static const char app1_exif[] = "Exif\0";
    static const char app1_xmp[] = "http://ns.adobe.com/xap/1.0/";
    static const char app2_iccp[] = "ICC_PROFILE";
    jpeg_saved_marker_ptr marker;
    BPGMetaData *md, **plast_md, *first_md;
    int has_exif, has_xmp, l, iccp_chunk_count, i;
    jpeg_saved_marker_ptr iccp_chunks[256];
    
    iccp_chunk_count = 0;
    has_exif = 0;
    has_xmp = 0;
    first_md = NULL;
    plast_md = &first_md;
    for (marker = first_marker; marker != NULL; marker = marker->next) {
#if 0
        printf("marker=APP%d len=%d\n", 
               marker->marker - JPEG_APP0, marker->data_length);
#endif
        if (!has_exif && marker->marker == JPEG_APP0 + 1 &&
            marker->data_length > sizeof(app1_exif) &&
            !memcmp(marker->data, app1_exif, sizeof(app1_exif))) {
            md = bpg_md_alloc(BPG_EXTENSION_TAG_EXIF);
            l = sizeof(app1_exif);
            md->buf_len = marker->data_length - l;
            md->buf = malloc(md->buf_len);
            memcpy(md->buf, marker->data + l, md->buf_len);
            *plast_md = md;
            plast_md = &md->next;
            has_exif = 1;
        } else if (!has_xmp && marker->marker == JPEG_APP0 + 1 &&
                   marker->data_length > sizeof(app1_xmp) &&
                   !memcmp(marker->data, app1_xmp, sizeof(app1_xmp)) && 
                   !has_xmp) {
            md = bpg_md_alloc(BPG_EXTENSION_TAG_XMP);
            l = sizeof(app1_xmp);
            md->buf_len = marker->data_length - l;
            md->buf = malloc(md->buf_len);
            memcpy(md->buf, marker->data + l, md->buf_len);
            *plast_md = md;
            plast_md = &md->next;
            has_xmp = 1;
        } else if (marker->marker == JPEG_APP0 + 2 &&
                   marker->data_length > (sizeof(app2_iccp) + 2) &&
                   !memcmp(marker->data, app2_iccp, sizeof(app2_iccp))) {
            int chunk_count, chunk_index;
            l = sizeof(app2_iccp);
            chunk_index = marker->data[l];
            chunk_count = marker->data[l];
            if (chunk_index == 0 || chunk_count == 0) 
                continue;
            if (iccp_chunk_count == 0) {
                iccp_chunk_count = chunk_count;
                for(i = 0; i < chunk_count; i++) {
                    iccp_chunks[i] = NULL;
                }
            } else {
                if (chunk_count != iccp_chunk_count)
                    continue;
            }
            if (chunk_index > iccp_chunk_count)
                continue;
            iccp_chunks[chunk_index - 1] = marker;
        }
    }

    if (iccp_chunk_count != 0) {
        int len, hlen, idx;
        /* check that no chunk are missing */
        len = 0;
        hlen = sizeof(app2_iccp) + 2;
        for(i = 0; i < iccp_chunk_count; i++) {
            if (!iccp_chunks[i])
                break;
            len += iccp_chunks[i]->data_length - hlen;
        }
        if (i == iccp_chunk_count) {
            md = bpg_md_alloc(BPG_EXTENSION_TAG_ICCP);
            md->buf_len = len;
            md->buf = malloc(md->buf_len);
            idx = 0;
            for(i = 0; i < iccp_chunk_count; i++) {
                l = iccp_chunks[i]->data_length - hlen;
                memcpy(md->buf + idx, iccp_chunks[i]->data + hlen, l);
                idx += l;
            }
            assert(idx == len);
            *plast_md = md;
            plast_md = &md->next;
        }
    }
    return first_md;
}

Image *read_jpeg(BPGMetaData **pmd, FILE *f, 
                 int out_bit_depth)
{
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    int w, h, w1, i, y_h, c_h, y, v_shift, c_w, y1, idx, c_idx, h_shift;
    int h1, plane_idx[4], has_alpha, has_w_plane;
    Image *img;
    BPGImageFormatEnum format;
    BPGColorSpaceEnum color_space;
    ColorConvertState cvt_s, *cvt = &cvt_s;
    BPGMetaData *first_md = NULL;
    uint32_t comp_hv;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    jpeg_save_markers(&cinfo, JPEG_APP0 + 1, 65535);
    jpeg_save_markers(&cinfo, JPEG_APP0 + 2, 65535);

    jpeg_stdio_src(&cinfo, f);
    
    jpeg_read_header(&cinfo, TRUE);
    
    cinfo.raw_data_out = TRUE;
    cinfo.do_fancy_upsampling = TRUE;
    
    w = cinfo.image_width;
    h = cinfo.image_height;

    has_w_plane = 0;
    comp_hv = 0;
    if (cinfo.num_components < 1 || cinfo.num_components > 4)
        goto unsupported;
    for(i = 0; i < cinfo.num_components; i++) {
        comp_hv |= cinfo.comp_info[i].h_samp_factor << (i * 8 + 4);
        comp_hv |= cinfo.comp_info[i].v_samp_factor << (i * 8);
    }
    switch(cinfo.jpeg_color_space) {
    case JCS_GRAYSCALE:
        if (cinfo.num_components != 1 || comp_hv != 0x11)
            goto unsupported;
        format = BPG_FORMAT_GRAY;
        color_space = BPG_CS_YCbCr;
        break;
    case JCS_YCbCr:
        if (cinfo.num_components != 3)
            goto unsupported;
        switch(comp_hv) {
        case 0x111111:
            format = BPG_FORMAT_444;
            break;
        case 0x111121:
            format = BPG_FORMAT_422;
            break;
        case 0x111122:
            format = BPG_FORMAT_420;
            break;
        default:
            cinfo.raw_data_out = FALSE;
            format = BPG_FORMAT_444;
            cinfo.out_color_space = JCS_YCbCr;
            break;
        }
        color_space = BPG_CS_YCbCr;
        break;
    case JCS_RGB:
        if (cinfo.num_components != 3)
            goto unsupported;
        format = BPG_FORMAT_444;
        color_space = BPG_CS_RGB;
        cinfo.raw_data_out = FALSE;
        cinfo.out_color_space = JCS_RGB;
        break;
    case JCS_YCCK:
        if (cinfo.num_components != 4)
            goto unsupported;
        switch(comp_hv) {
        case 0x11111111:
            format = BPG_FORMAT_444;
            color_space = BPG_CS_YCbCr;
            break;
        case 0x22111121:
            format = BPG_FORMAT_422;
            color_space = BPG_CS_YCbCr;
            break;
        case 0x22111122:
            format = BPG_FORMAT_420;
            color_space = BPG_CS_YCbCr;
            break;
        default:
            cinfo.raw_data_out = FALSE;
            format = BPG_FORMAT_444;
            cinfo.out_color_space = JCS_CMYK;
            color_space = BPG_CS_RGB;
            break;
        }
        has_w_plane = 1;
        break;
    case JCS_CMYK:
        if (cinfo.num_components != 4)
            goto unsupported;
        format = BPG_FORMAT_444;
        color_space = BPG_CS_RGB;
        has_w_plane = 1;
        cinfo.raw_data_out = FALSE;
        cinfo.out_color_space = JCS_CMYK;
        break;
    default:
    unsupported:
        fprintf(stderr, "Unsupported JPEG parameters (cs=%d n_comp=%d comp_hv=%x)\n",
                cinfo.jpeg_color_space, cinfo.num_components, comp_hv);
        img = NULL;
        goto the_end;
    }

    v_shift = (format == BPG_FORMAT_420);
    h_shift = (format == BPG_FORMAT_422 || format == BPG_FORMAT_420);
    has_alpha = (cinfo.num_components == 4);
    img = image_alloc(w, h, format, has_alpha, color_space, out_bit_depth);
    img->has_w_plane = has_w_plane;

    convert_init(cvt, 8, out_bit_depth, color_space, 0);

    jpeg_start_decompress(&cinfo);

    if (color_space == BPG_CS_RGB) {
        plane_idx[0] = 2;
        plane_idx[1] = 0;
        plane_idx[2] = 1;
    } else {
        plane_idx[0] = 0;
        plane_idx[1] = 1;
        plane_idx[2] = 2;
    }
    plane_idx[3] = 3;
        
    if (cinfo.raw_data_out) {
        JSAMPROW rows[4][16];
        JSAMPROW *plane_pointer[4];

        y_h = 8 * cinfo.max_v_samp_factor;
        if (cinfo.num_components == 1) {
            c_h = 0;
            c_w = 0;
        } else {
            c_h = 8;
            if (h_shift)
                c_w = (w + 1) / 2;
            else
                c_w = w;
        }
        w1 = (w + 15) & ~15;
        for(c_idx = 0; c_idx < cinfo.num_components; c_idx++) {
            if (c_idx == 1 || c_idx == 2) {
                h1 = c_h;
            } else {
                h1 = y_h;
            }
            for(i = 0; i < h1; i++) {
                rows[c_idx][i] = malloc(w1);
            }
            plane_pointer[c_idx] = rows[c_idx];
        }
        
        while (cinfo.output_scanline < cinfo.output_height) {
            y = cinfo.output_scanline;
            jpeg_read_raw_data(&cinfo, plane_pointer, y_h);
            
            for(c_idx = 0; c_idx < cinfo.num_components; c_idx++) {
                if (c_idx == 1 || c_idx == 2) {
                    h1 = c_h;
                    w1 = c_w;
                    y1 = (y >> v_shift);
                } else {
                    h1 = y_h;
                    w1 = img->w;
                    y1 = y;
                }
                idx = plane_idx[c_idx];
                for(i = 0; i < h1; i++) {
                    PIXEL *ptr;
                    ptr = (PIXEL *)(img->data[idx] + 
                                    img->linesize[idx] * (y1 + i));
                    gray8_to_gray(cvt, ptr, rows[c_idx][i], w1, 1);
                    if (color_space == BPG_CS_YCbCr && has_w_plane) {
                        /* negate color */
                        if (c_idx == 0) {
                            gray_one_minus(cvt, ptr, w1);
                        } else if (c_idx <= 2) {
                            gray_neg_c(cvt, ptr, w1);
                        }
                    }
                }
            }
        }
    
        for(c_idx = 0; c_idx < cinfo.num_components; c_idx++) {
            if (c_idx == 1 || c_idx == 2) {
                h1 = c_h;
            } else {
                h1 = y_h;
            }
            for(i = 0; i < h1; i++) {
                free(rows[c_idx][i]);
            }
        }
    } else {
        JSAMPROW rows[1];
        uint8_t *buf;
        int c_count;

        c_count = 3 + has_w_plane;
        buf = malloc(c_count * w);
        rows[0] = buf;
        while (cinfo.output_scanline < cinfo.output_height) {
            y = cinfo.output_scanline;
            jpeg_read_scanlines(&cinfo, rows, 1);

            for(c_idx = 0; c_idx < c_count; c_idx++) {
                idx = plane_idx[c_idx];
                gray8_to_gray(cvt, (PIXEL *)(img->data[idx] + 
                                             img->linesize[idx] * y),
                              buf + c_idx, w, c_count);
            }
        }
        free(buf);
    }
    
    first_md = jpeg_get_metadata(cinfo.marker_list);

 the_end:
    jpeg_finish_decompress(&cinfo);
    
    jpeg_destroy_decompress(&cinfo);
    *pmd = first_md;
    return img;
}

Image *load_image(BPGMetaData **pmd, const char *infilename,
                  BPGColorSpaceEnum color_space, int bit_depth,
                  int limited_range, int premultiplied_alpha)
{
    FILE *f;
    int is_png;
    Image *img;
    BPGMetaData *md;

    *pmd = NULL;

    f = fopen(infilename, "rb");
    if (!f)
        return NULL;
    {
        uint8_t buf[8];
        if (fread(buf, 1, 8, f) == 8 && 
            png_sig_cmp(buf, 0, 8) == 0)
            is_png = 1;
        else
            is_png = 0;
        fseek(f, 0, SEEK_SET);
    }
    
    if (is_png) {
        img = read_png(&md, f, color_space, bit_depth, limited_range,
                       premultiplied_alpha);
    } else {
        img = read_jpeg(&md, f, bit_depth);
    }
    fclose(f);
    *pmd = md;
    return img;
}

void save_yuv1(Image *img, FILE *f)
{
    int c_w, c_h, i, c_count, y;

    if (img->format == BPG_FORMAT_GRAY)
        c_count = 1;
    else
        c_count = 3;
    for(i = 0; i < c_count; i++) {
        get_plane_res(img, &c_w, &c_h, i);
        for(y = 0; y < c_h; y++) {
            fwrite(img->data[i] + y * img->linesize[i], 
                   1, c_w << img->pixel_shift, f);
        }
    }
}

void save_yuv(Image *img, const char *filename)
{
    FILE *f;

    f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }
    save_yuv1(img, f);
    fclose(f);
}

/* return the position of the end of the NAL or -1 if error */
static int find_nal_end(const uint8_t *buf, int buf_len)
{
    int idx;

    idx = 0;
    if (buf_len >= 4 &&
        buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] == 1) {
        idx = 4;
    } else if (buf_len >= 3 &&
               buf[0] == 0 && buf[1] == 0 && buf[2] == 1) {
        idx = 3;
    } else {
        return -1;
    }
    /* NAL header */
    if (idx + 2 > buf_len)
        return -1;
    /* find the last byte */
    for(;;) {
        if (idx + 2 >= buf_len) {
            idx = buf_len;
            break;
        }
        if (buf[idx] == 0 && buf[idx + 1] == 0 && buf[idx + 2] == 1)
            break;
        if (idx + 3 < buf_len &&
            buf[idx] == 0 && buf[idx + 1] == 0 && buf[idx + 2] == 0 && buf[idx + 3] == 1)
            break;
        idx++;
    }
    return idx;
}

/* return the position of the end of the NAL or -1 if error */
static int extract_nal(uint8_t **pnal_buf, int *pnal_len, 
                       const uint8_t *buf, int buf_len)
{
    int idx, start, end, len;
    uint8_t *nal_buf;
    int nal_len;

    end = find_nal_end(buf, buf_len);
    if (end < 0)
        return -1;
    if (buf[2] == 1)
        start = 3;
    else
        start = 4;
    len = end - start;
    
    nal_buf = malloc(len);
    nal_len = 0;
    idx = start;
    while (idx < end) {
        if (idx + 2 < end && buf[idx] == 0 && buf[idx + 1] == 0 && buf[idx + 2] == 3) {
            nal_buf[nal_len++] = 0;
            nal_buf[nal_len++] = 0;
            idx += 3;
        } else {
            nal_buf[nal_len++] = buf[idx++];
        }
    }
    while (idx < end) {
        nal_buf[nal_len++] = buf[idx++];
    }
    *pnal_buf = nal_buf;
    *pnal_len = nal_len;
    return idx;
}

/* big endian variable length 7 bit encoding */
static void put_ue(uint8_t **pp, uint32_t v)
{
    uint8_t *p = *pp;
    int i, j;
    
    for(i = 1; i < 5; i++) {
        if (v < (1 << (7 * i)))
            break;
    }
    for(j = i - 1; j >= 1; j--)
        *p++ = ((v >> (7 * j)) & 0x7f) | 0x80;
    *p++ = v & 0x7f;
    *pp = p;
}

typedef struct {
    const uint8_t *buf;
    int idx;
    int buf_len;
} GetBitState;

static void init_get_bits(GetBitState *s, const uint8_t *buf, int buf_len)
{
    s->buf = buf;
    s->buf_len = buf_len;
    s->idx = 0;
}

static void skip_bits(GetBitState *s, int n)
{
    s->idx += n;
}

/* 1 <= n <= 25. return '0' bits if past the end of the buffer. */
static uint32_t get_bits(GetBitState *s, int n)
{
    const uint8_t *buf = s->buf;
    int p, i;
    uint32_t v;

    p = s->idx >> 3;
    if ((p + 3) < s->buf_len) {
        v = (buf[p] << 24) | (buf[p + 1] << 16) | 
            (buf[p + 2] << 8) | buf[p + 3];
    } else {
        v = 0;
        for(i = 0; i < 3; i++) {
            if ((p + i) < s->buf_len)
                v |= buf[p + i] << (24 - i * 8);
        }
    }
    v = (v >> (32 - (s->idx & 7) - n)) & ((1 << n) - 1);
    s->idx += n;
    return v;
}

/* 1 <= n <= 32 */
static uint32_t get_bits_long(GetBitState *s, int n)
{
    uint32_t v;

    if (n <= 25) {
        v = get_bits(s, n);
    } else {
        n -= 16;
        v = get_bits(s, 16) << n;
        v |= get_bits(s, n);
    }
    return v;
}

/* at most 32 bits are supported */
static uint32_t get_ue_golomb(GetBitState *s)
{
    int i;
    i = 0;
    for(;;) {
        if (get_bits(s, 1))
            break;
        i++;
        if (i == 32)
            return 0xffffffff;
    }
    if (i == 0)
        return 0;
    else
        return ((1 << i) | get_bits_long(s, i)) - 1;
}

typedef struct {
    uint8_t *buf;
    int idx;
} PutBitState;

static void init_put_bits(PutBitState *s, uint8_t *buf)
{
    s->buf = buf;
    s->idx = 0;
}

static void put_bit(PutBitState *s, int bit)
{
    s->buf[s->idx >> 3] |= bit << (7 - (s->idx & 7));
    s->idx++;
}

static void put_bits(PutBitState *s, int n, uint32_t v)
{
    int i;

    for(i = 0; i < n; i++) {
        put_bit(s, (v >> (n - 1 - i)) & 1);
    }
}

static void put_ue_golomb(PutBitState *s, uint32_t v)
{
    uint32_t a;
    int n;

    v++;
    n = 0;
    a = v;
    while (a != 0) {
        a >>= 1;
        n++;
    }
    if (n > 1)
        put_bits(s, n - 1, 0);
    put_bits(s, n, v);
}

typedef struct {
    uint8_t *buf;
    int size;
    int len;
} DynBuf;

static void dyn_buf_init(DynBuf *s)
{
    s->buf = NULL;
    s->size = 0;
    s->len = 0;
}

static int dyn_buf_resize(DynBuf *s, int size)
{
    int new_size;
    uint8_t *new_buf;

    if (size <= s->size)
        return 0;
    new_size = (s->size * 3) / 2;
    if (new_size < size)
        new_size = size;
    new_buf = realloc(s->buf, new_size);
    if (!new_buf) 
        return -1;
    s->buf = new_buf;
    s->size = new_size;
    return 0;
}

/* suppress the VPS NAL and keep only the useful part of the SPS
   header. The decoder can rebuild a valid HEVC stream if needed. */
static int build_modified_sps(uint8_t **pout_buf, int *pout_buf_len,
                              const uint8_t *buf, int buf_len)
{
    int nal_unit_type, nal_len, idx, i, ret, msps_buf_len;
    int out_buf_len, out_buf_len_max;
    uint8_t *nal_buf, *msps_buf, *out_buf;
    GetBitState gb_s, *gb = &gb_s;
    PutBitState pb_s, *pb = &pb_s;
    uint8_t *p;

    idx = extract_nal(&nal_buf, &nal_len, buf, buf_len);
    if (idx < 0)
        return -1;
    if (nal_len < 2) {
        free(nal_buf);
        return -1;
    }
    nal_unit_type = (nal_buf[0] >> 1) & 0x3f;
    free(nal_buf);
    if (nal_unit_type != 32)  {
        fprintf(stderr, "expecting VPS nal (%d)\n", nal_unit_type);
        return -1; /* expect VPS nal */
    }

    ret = extract_nal(&nal_buf, &nal_len, buf + idx, buf_len);
    if (ret < 0)
        return -1;
    idx += ret;
    if (nal_len < 2)
        return -1;
    nal_unit_type = (nal_buf[0] >> 1) & 0x3f;
    if (nal_unit_type != 33) {
        fprintf(stderr, "expecting SPS nal (%d)\n", nal_unit_type);
        return -1; /* expect SPS nal */
    }

    /* skip the initial part of the SPS up to and including
       log2_min_cb_size */
    {
        int vps_id, max_sub_layers, profile_idc, sps_id;
        int chroma_format_idc, width, height, bit_depth_luma, bit_depth_chroma;
        int log2_max_poc_lsb, sublayer_ordering_info, log2_min_cb_size;
        int log2_diff_max_min_coding_block_size, log2_min_tb_size;
        int log2_diff_max_min_transform_block_size;
        int max_transform_hierarchy_depth_inter;
        int max_transform_hierarchy_depth_intra;
        int scaling_list_enable_flag, amp_enabled_flag, sao_enabled;
        int pcm_enabled_flag, nb_st_rps;
        int long_term_ref_pics_present_flag, sps_strong_intra_smoothing_enable_flag, vui_present;
        int sps_temporal_mvp_enabled_flag;
        int pcm_sample_bit_depth_luma_minus1;
        int pcm_sample_bit_depth_chroma_minus1;
        int log2_min_pcm_luma_coding_block_size_minus3;
        int log2_diff_max_min_pcm_luma_coding_block_size;
        int pcm_loop_filter_disabled_flag;
        int sps_extension_flag, sps_range_extension_flag, sps_extension_7bits;
        int sps_range_extension_flags;

        init_get_bits(gb, nal_buf, nal_len);
        skip_bits(gb, 16); /* nal header */
        vps_id = get_bits(gb, 4);
        if (vps_id != 0) {
            fprintf(stderr, "VPS id 0 expected\n");
            return -1;
        }
        max_sub_layers = get_bits(gb, 3);
        if (max_sub_layers != 0) {
            fprintf(stderr, "max_sub_layers == 0 expected\n");
            return -1;
        }
        skip_bits(gb, 1); /* temporal_id_nesting_flag */
        /* profile tier level */
        skip_bits(gb, 2); /* profile_space */
        skip_bits(gb, 1); /* tier_flag */
        profile_idc = get_bits(gb, 5);
        for(i = 0; i < 32; i++) {
            skip_bits(gb, 1); /* profile_compatibility_flag */
        }
        skip_bits(gb, 1); /* progressive_source_flag */
        skip_bits(gb, 1); /* interlaced_source_flag */
        skip_bits(gb, 1); /* non_packed_constraint_flag */
        skip_bits(gb, 1); /* frame_only_constraint_flag */
        skip_bits(gb, 44); /*  XXX_reserved_zero_44 */
        skip_bits(gb, 8); /* level_idc */

        sps_id = get_ue_golomb(gb);
        if (sps_id != 0) {
            fprintf(stderr, "SPS id 0 expected (%d)\n", sps_id);
            return -1;
        }
        chroma_format_idc = get_ue_golomb(gb);
        if (chroma_format_idc == 3) {
            get_bits(gb, 1); /* separate_colour_plane_flag */
        }
        width = get_ue_golomb(gb);
        height = get_ue_golomb(gb);
        /* pic conformance_flag */
        if (get_bits(gb, 1)) {
            get_ue_golomb(gb); /* left_offset */
            get_ue_golomb(gb); /* right_offset */
            get_ue_golomb(gb); /* top_offset */
            get_ue_golomb(gb); /* bottom_offset */
        }
        bit_depth_luma = get_ue_golomb(gb) + 8;
        bit_depth_chroma = get_ue_golomb(gb) + 8;
        log2_max_poc_lsb = get_ue_golomb(gb) + 4;
        if (log2_max_poc_lsb != 8) {
            fprintf(stderr, "log2_max_poc_lsb must be 8 (%d)\n", log2_max_poc_lsb);
            return -1;
        }
        sublayer_ordering_info = get_bits(gb, 1);
        get_ue_golomb(gb); /* max_dec_pic_buffering */
        get_ue_golomb(gb); /* num_reorder_pics */
        get_ue_golomb(gb); /* max_latency_increase */
        
        log2_min_cb_size = get_ue_golomb(gb) + 3;
        log2_diff_max_min_coding_block_size = get_ue_golomb(gb);
        log2_min_tb_size = get_ue_golomb(gb) + 2;
        log2_diff_max_min_transform_block_size = get_ue_golomb(gb);
               
        max_transform_hierarchy_depth_inter = get_ue_golomb(gb);
        max_transform_hierarchy_depth_intra = get_ue_golomb(gb);
        if (max_transform_hierarchy_depth_inter != max_transform_hierarchy_depth_intra) {
            fprintf(stderr, "max_transform_hierarchy_depth_inter must be the same as max_transform_hierarchy_depth_intra (%d %d)\n", max_transform_hierarchy_depth_inter, max_transform_hierarchy_depth_intra);
            return -1;
        }

        scaling_list_enable_flag = get_bits(gb, 1);
        if (scaling_list_enable_flag != 0) {
            fprintf(stderr, "scaling_list_enable_flag must be 0\n");
            return -1;
        }
        amp_enabled_flag = get_bits(gb, 1);
        if (!amp_enabled_flag) {
            fprintf(stderr, "amp_enabled_flag must be set\n");
            return -1;
        }
        sao_enabled = get_bits(gb, 1);
        pcm_enabled_flag = get_bits(gb, 1);
        if (pcm_enabled_flag) {
            pcm_sample_bit_depth_luma_minus1 = get_bits(gb, 4);
            pcm_sample_bit_depth_chroma_minus1 = get_bits(gb, 4);
            log2_min_pcm_luma_coding_block_size_minus3 = get_ue_golomb(gb);
            log2_diff_max_min_pcm_luma_coding_block_size = get_ue_golomb(gb);
            pcm_loop_filter_disabled_flag = get_bits(gb, 1);
        }
        nb_st_rps = get_ue_golomb(gb);
        if (nb_st_rps != 0) {
            fprintf(stderr, "nb_st_rps must be 0 (%d)\n", nb_st_rps);
            return -1;
        }
        long_term_ref_pics_present_flag = get_bits(gb, 1);
        if (long_term_ref_pics_present_flag) {
            fprintf(stderr, "nlong_term_ref_pics_present_flag must be 0 (%d)\n", nb_st_rps);
            return -1;
        }
        sps_temporal_mvp_enabled_flag = get_bits(gb, 1);
        if (!sps_temporal_mvp_enabled_flag) {
            fprintf(stderr, "sps_temporal_mvp_enabled_flag must be set\n");
            return -1;
        }
        sps_strong_intra_smoothing_enable_flag = get_bits(gb, 1);
        vui_present = get_bits(gb, 1);
        if (vui_present) {
            int sar_present, sar_idx, overscan_info_present_flag;
            int video_signal_type_present_flag, chroma_loc_info_present_flag;
            int default_display_window_flag, vui_timing_info_present_flag;
            int vui_poc_proportional_to_timing_flag;
            int vui_hrd_parameters_present_flag, bitstream_restriction_flag;

            sar_present = get_bits(gb, 1);
            if (sar_present) {
                sar_idx = get_bits(gb, 8);
                if (sar_idx == 255) {
                    skip_bits(gb, 16); /* sar_num */ 
                    skip_bits(gb, 16); /* sar_den */ 
                }
            }
            
            overscan_info_present_flag = get_bits(gb, 1);
            if (overscan_info_present_flag) {
                skip_bits(gb, 1); /* overscan_appropriate_flag */
            }

            video_signal_type_present_flag = get_bits(gb, 1);
            if (video_signal_type_present_flag) {
                fprintf(stderr, "video_signal_type_present_flag must be 0\n");
                return -1;
            }
            chroma_loc_info_present_flag = get_bits(gb, 1);
            if (chroma_loc_info_present_flag) {
                get_ue_golomb(gb);
                get_ue_golomb(gb);
            }
            skip_bits(gb, 1); /* neutra_chroma_indication_flag */
            skip_bits(gb, 1);
            skip_bits(gb, 1);
            default_display_window_flag = get_bits(gb, 1);
            if (default_display_window_flag) {
                fprintf(stderr, "default_display_window_flag must be 0\n");
                return -1;
            }
            vui_timing_info_present_flag = get_bits(gb, 1);
            if (vui_timing_info_present_flag) {
                skip_bits(gb, 32);
                skip_bits(gb, 32);
                vui_poc_proportional_to_timing_flag = get_bits(gb, 1);
                if (vui_poc_proportional_to_timing_flag) {
                    get_ue_golomb(gb);
                }
                vui_hrd_parameters_present_flag = get_bits(gb, 1);
                if (vui_hrd_parameters_present_flag) {
                    fprintf(stderr, "vui_hrd_parameters_present_flag must be 0\n");
                    return -1;
                }
            }
            bitstream_restriction_flag = get_bits(gb, 1);
            if (bitstream_restriction_flag) {
                skip_bits(gb, 1);
                skip_bits(gb, 1);
                skip_bits(gb, 1);
                get_ue_golomb(gb);
                get_ue_golomb(gb);
                get_ue_golomb(gb);
                get_ue_golomb(gb);
                get_ue_golomb(gb);
            }
        }
        sps_extension_flag = get_bits(gb, 1);
        sps_range_extension_flag = 0;
        sps_range_extension_flags = 0;
        if (sps_extension_flag) {
            sps_range_extension_flag = get_bits(gb, 1);
            sps_extension_7bits = get_bits(gb, 7);
            if (sps_extension_7bits != 0) {
                fprintf(stderr, "sps_extension_7bits must be 0\n");
                return -1;
            }
            if (sps_range_extension_flag) {
                sps_range_extension_flags = get_bits(gb, 9);
                if (sps_range_extension_flags & ((1 << (8 - 3)) | 
                                                 (1 << (8 - 4)) | 
                                                 (1 << (8 - 6)) | 
                                                 (1 << (8 - 8)))) {
                    fprintf(stderr, "unsupported range extensions (0x%x)\n",
                            sps_range_extension_flags);
                    return -1;
                }
            }
        }

        /* build the modified SPS */
        msps_buf = malloc(nal_len + 32);
        memset(msps_buf, 0, nal_len + 16);
        
        init_put_bits(pb, msps_buf);
        put_ue_golomb(pb, log2_min_cb_size - 3);
        put_ue_golomb(pb, log2_diff_max_min_coding_block_size);
        put_ue_golomb(pb, log2_min_tb_size - 2);
        put_ue_golomb(pb, log2_diff_max_min_transform_block_size);
        put_ue_golomb(pb, max_transform_hierarchy_depth_intra);
        put_bits(pb, 1, sao_enabled);
        put_bits(pb, 1, pcm_enabled_flag);
        if (pcm_enabled_flag) {
            put_bits(pb, 4, pcm_sample_bit_depth_luma_minus1);
            put_bits(pb, 4, pcm_sample_bit_depth_chroma_minus1);
            put_ue_golomb(pb, log2_min_pcm_luma_coding_block_size_minus3);
            put_ue_golomb(pb, log2_diff_max_min_pcm_luma_coding_block_size);
            put_bits(pb, 1, pcm_loop_filter_disabled_flag);
        }
        put_bits(pb, 1, sps_strong_intra_smoothing_enable_flag);
        put_bits(pb, 1, sps_extension_flag);
        if (sps_extension_flag) {
            put_bits(pb, 1, sps_range_extension_flag);
            put_bits(pb, 7, 0);
            if (sps_range_extension_flag) {
                put_bits(pb, 9, sps_range_extension_flags);
            }
        }
        msps_buf_len = (pb->idx + 7) >> 3;

        out_buf_len_max = 5 + msps_buf_len;
        out_buf = malloc(out_buf_len_max);

        //        printf("msps_n_bits=%d\n", pb->idx);
        p = out_buf;
        put_ue(&p, msps_buf_len); /* header length */

        memcpy(p, msps_buf, msps_buf_len);
        p += msps_buf_len;
        
        out_buf_len = p - out_buf;
        free(msps_buf);
        free(nal_buf);
    }
    *pout_buf = out_buf;
    *pout_buf_len = out_buf_len;
    return idx;
}

static int add_frame_duration_sei(DynBuf *out_buf, uint16_t frame_ticks)
{
    uint8_t nal_buf[128], *q;
    int nut, nal_len;

    q = nal_buf;
    *q++ = 0x00;
    *q++ = 0x00;
    *q++ = 0x01;
    nut = 39; /* prefix SEI NUT */
    *q++ = (nut << 1);
    *q++ = 1;
    *q++ = 0xff;  /* payload_type = 257 */
    *q++ = 0x02;
    *q++ = 2; /* payload_size = 2 */
    *q++ = frame_ticks >> 8;
    *q++ = frame_ticks;
    *q++ = 0x80; /* extra '1' bit and align to byte */
    /* Note: the 0x00 0x00 b pattern with b <= 3 cannot happen, so no
       need to escape */
    nal_len = q - nal_buf;
    if (dyn_buf_resize(out_buf, out_buf->len + nal_len) < 0)
        return -1;
    memcpy(out_buf->buf + out_buf->len, nal_buf, nal_len);
    out_buf->len += nal_len;
    return 0;
}

static int build_modified_hevc(uint8_t **pout_buf, 
                               const uint8_t *cbuf, int cbuf_len,
                               const uint8_t *abuf, int abuf_len,
                               const uint16_t *frame_duration_tab,
                               int frame_count)
{
    DynBuf out_buf_s, *out_buf = &out_buf_s;
    uint8_t *msps;
    const uint8_t *nal_buf;
    int msps_len, cidx, aidx, is_alpha, nal_len, first_nal, start, l, frame_num;
    
    dyn_buf_init(out_buf);
    
    /* add alpha MSPS */
    aidx = 0; /* avoids warning */
    if (abuf) {
        aidx = build_modified_sps(&msps, &msps_len, abuf, abuf_len);
        if (aidx < 0)
            goto fail;
        if (dyn_buf_resize(out_buf, out_buf->len + msps_len) < 0)
            goto fail;
        memcpy(out_buf->buf + out_buf->len, msps, msps_len);
        out_buf->len += msps_len;
        free(msps);
    }
    
    /* add color MSPS */
    cidx = build_modified_sps(&msps, &msps_len, cbuf, cbuf_len);
    if (cidx < 0)
        goto fail;
    if (dyn_buf_resize(out_buf, out_buf->len + msps_len) < 0)
        goto fail;
    memcpy(out_buf->buf + out_buf->len, msps, msps_len);
    out_buf->len += msps_len;
    free(msps);

    /* add the remaining NALs, alternating between alpha (if present)
       and color. */
    is_alpha = (abuf != NULL);
    first_nal = 1;
    frame_num = 0;
    for(;;) {
        if (!is_alpha) {
            if (cidx >= cbuf_len) {
                if (abuf) {
                    fprintf(stderr, "Incorrect number of alpha NALs\n");
                    goto fail;
                }
                break;
            }
            nal_buf = cbuf + cidx;
            nal_len = find_nal_end(nal_buf, cbuf_len - cidx);
            //            printf("cidx=%d/%d nal_len=%d\n", cidx, cbuf_len, nal_len);
            if (nal_len < 0)
                goto fail;
            cidx += nal_len;
        } else {
            if (aidx >= abuf_len) 
                break;
            nal_buf = abuf + aidx;
            nal_len = find_nal_end(nal_buf, abuf_len - aidx);
            //            printf("aidx=%d/%d nal_len=%d\n", aidx, abuf_len, nal_len);
            if (nal_len < 0)
                goto fail;
            aidx += nal_len;
        }
        start = 3 + (nal_buf[2] == 0);
        if (!is_alpha) {
            int nut;
            /* add SEI NAL for the frame duration (animation case) */
            nut = (nal_buf[start] >> 1) & 0x3f;
            if ((nut <= 9 || (nut >= 16 && nut <= 21)) &&  
                start + 2 < nal_len && (nal_buf[start + 2] & 0x80)) {
                int frame_ticks;
                assert(frame_num < frame_count);
                frame_ticks = frame_duration_tab[frame_num];
                if (frame_ticks > 1) {
                    add_frame_duration_sei(out_buf, frame_ticks);
                }
                frame_num++;
            }
        }
        if (first_nal) {
            /* skip first start code */
            l = start;
        } else {
            l = 0;
        }
        if (dyn_buf_resize(out_buf, out_buf->len + nal_len - l) < 0)
            goto fail;
        //        printf("add nal len=%d\n", nal_len - l);
        memcpy(out_buf->buf + out_buf->len, nal_buf + l, nal_len - l);
        if (is_alpha) {
            /* set nul_layer_id of alpha to '1' */
            out_buf->buf[out_buf->len + (start - l) + 1] |= 1 << 3;
        }
        out_buf->len += nal_len - l;

        if (abuf) {
            is_alpha ^= 1;
        }
        first_nal = 0;
    }
    *pout_buf = out_buf->buf;
    return out_buf->len;
 fail:
    free(out_buf->buf);
    return -1;
}

typedef enum {
#if defined(USE_X265)
    HEVC_ENCODER_X265,
#endif
#if defined(USE_JCTVC)
    HEVC_ENCODER_JCTVC,
#endif

    HEVC_ENCODER_COUNT,
} HEVCEncoderEnum;

static char *hevc_encoder_name[HEVC_ENCODER_COUNT] = {
#if defined(USE_X265)
    "x265",
#endif
#if defined(USE_JCTVC)
    "jctvc",
#endif
};

static HEVCEncoder *hevc_encoder_tab[HEVC_ENCODER_COUNT] = {
#if defined(USE_X265)
    &x265_hevc_encoder,
#endif
#if defined(USE_JCTVC)
    &jctvc_encoder,
#endif
};

#define IMAGE_HEADER_MAGIC 0x425047fb

#define DEFAULT_OUTFILENAME "out.bpg"
#define DEFAULT_QP 29
#define DEFAULT_BIT_DEPTH 8

#ifdef RExt__HIGH_BIT_DEPTH_SUPPORT
#define BIT_DEPTH_MAX 14
#else
#define BIT_DEPTH_MAX 12
#endif
#define DEFAULT_COMPRESS_LEVEL 8


typedef struct BPGEncoderContext BPGEncoderContext;

typedef struct BPGEncoderParameters {
    int qp; /* 0 ... 51 */
    int alpha_qp; /* -1 ... 51. -1 means same as qp */
    int lossless; /* true if lossless compression (qp and alpha_qp are
                     ignored) */
    BPGImageFormatEnum preferred_chroma_format;
    int sei_decoded_picture_hash; /* 0, 1 */
    int compress_level; /* 1 ... 9 */
    int verbose;
    HEVCEncoderEnum encoder_type;
    int animated; /* 0 ... 1: if true, encode as animated image */
    uint16_t loop_count; /* animations: number of loops. 0=infinite */
    /* animations: the frame delay is a multiple of
       frame_delay_num/frame_delay_den seconds */
    uint16_t frame_delay_num;
    uint16_t frame_delay_den;
} BPGEncoderParameters;

typedef int BPGEncoderWriteFunc(void *opaque, const uint8_t *buf, int buf_len);

struct BPGEncoderContext {
    BPGEncoderParameters params;
    BPGMetaData *first_md;
    HEVCEncoder *encoder;
    int frame_count;
    HEVCEncoderContext *enc_ctx;
    HEVCEncoderContext *alpha_enc_ctx;
    int frame_ticks;
    uint16_t *frame_duration_tab;
    int frame_duration_tab_size;
};

void *mallocz(size_t size)
{
    void *ptr;
    ptr = malloc(size);
    if (!ptr)
        return NULL;
    memset(ptr, 0, size);
    return ptr;
}

BPGEncoderParameters *bpg_encoder_param_alloc(void)
{
    BPGEncoderParameters *p;
    p = mallocz(sizeof(BPGEncoderParameters));
    if (!p)
        return NULL;
    p->qp = DEFAULT_QP;
    p->alpha_qp = -1;
    p->preferred_chroma_format = BPG_FORMAT_420;
    p->compress_level = DEFAULT_COMPRESS_LEVEL;
    p->frame_delay_num = 1;
    p->frame_delay_den = 25;
    p->loop_count = 0;
    return p;
}

void bpg_encoder_param_free(BPGEncoderParameters *p)
{
    free(p);
}

BPGEncoderContext *bpg_encoder_open(BPGEncoderParameters *p)
{
    BPGEncoderContext *s;

    s = mallocz(sizeof(BPGEncoderContext));
    if (!s)
        return NULL;
    s->params = *p;
    s->encoder = hevc_encoder_tab[s->params.encoder_type];
    s->frame_ticks = 1;
    return s;
}

void bpg_encoder_set_extension_data(BPGEncoderContext *s, 
                                    BPGMetaData *md)
{
    s->first_md = md;
}

static int bpg_encoder_encode_trailer(BPGEncoderContext *s, 
                                      BPGEncoderWriteFunc *write_func,
                                      void *opaque)
{
    uint8_t *out_buf, *alpha_buf, *hevc_buf;
    int out_buf_len, alpha_buf_len, hevc_buf_len;

    out_buf_len = s->encoder->close(s->enc_ctx, &out_buf);
    if (out_buf_len < 0) {
        fprintf(stderr, "Error while encoding picture\n");
        exit(1);
    }
    s->enc_ctx = NULL;
    
    alpha_buf = NULL;
    alpha_buf_len = 0;
    if (s->alpha_enc_ctx) {
        alpha_buf_len = s->encoder->close(s->alpha_enc_ctx, &alpha_buf);
        if (alpha_buf_len < 0) {
            fprintf(stderr, "Error while encoding picture (alpha plane)\n");
            exit(1);
        }
        s->alpha_enc_ctx = NULL;
    }
    
    hevc_buf = NULL;
    hevc_buf_len = build_modified_hevc(&hevc_buf, out_buf, out_buf_len,
                                       alpha_buf, alpha_buf_len,
                                       s->frame_duration_tab, s->frame_count);
    if (hevc_buf_len < 0) {
        fprintf(stderr, "Error while creating HEVC data\n");
        exit(1);
    }
    free(out_buf);
    free(alpha_buf);
    
    if (write_func(opaque, hevc_buf, hevc_buf_len) != hevc_buf_len) {
        fprintf(stderr, "Error while writing HEVC data\n");
        exit(1);
    }
    free(hevc_buf);
    return 0;
}

int bpg_encoder_set_frame_duration(BPGEncoderContext *s, int frame_ticks)
{
    if (frame_ticks >= 1 && frame_ticks <= 65535) {
        s->frame_ticks = frame_ticks;
        return 0;
    } else {
        return -1;
    }
}

/* Warning: currently 'img' is modified. When encoding animations, img
   = NULL indicates the end of the stream. */
int bpg_encoder_encode(BPGEncoderContext *s, Image *img,
                       BPGEncoderWriteFunc *write_func,
                       void *opaque)
{
    const BPGEncoderParameters *p = &s->params;
    Image *img_alpha;
    HEVCEncodeParams ep_s, *ep = &ep_s;
    uint8_t *extension_buf;
    int extension_buf_len;
    int cb_size, width, height;

    if (p->animated && !img) {
        return bpg_encoder_encode_trailer(s, write_func, opaque);
    }

    /* extract the alpha plane */
    if (img->has_alpha) {
        int c_idx;

        img_alpha = malloc(sizeof(Image));
        memset(img_alpha, 0, sizeof(*img_alpha));
        if (img->format == BPG_FORMAT_GRAY)
            c_idx = 1;
        else
            c_idx = 3;

        img_alpha->w = img->w;
        img_alpha->h = img->h;
        img_alpha->format = BPG_FORMAT_GRAY;
        img_alpha->has_alpha = 0;
        img_alpha->color_space = BPG_CS_YCbCr;
        img_alpha->bit_depth = img->bit_depth;
        img_alpha->pixel_shift = img->pixel_shift;
        img_alpha->data[0] = img->data[c_idx];
        img_alpha->linesize[0] = img->linesize[c_idx];
        
        img->data[c_idx] = NULL;
        img->has_alpha = 0;
    } else {
        img_alpha = NULL;
    }

    if (img->format == BPG_FORMAT_444 && img->color_space != BPG_CS_RGB) {
        if (p->preferred_chroma_format == BPG_FORMAT_420 ||
            p->preferred_chroma_format == BPG_FORMAT_420_VIDEO) {
            int c_h_phase = (p->preferred_chroma_format == BPG_FORMAT_420);
            if (image_ycc444_to_ycc420(img, c_h_phase) != 0)
                goto error_convert;
        } else if (p->preferred_chroma_format == BPG_FORMAT_422 ||
                   p->preferred_chroma_format == BPG_FORMAT_422_VIDEO) {
            int c_h_phase = (p->preferred_chroma_format == BPG_FORMAT_422);
            if (image_ycc444_to_ycc422(img, c_h_phase) != 0)  {
            error_convert:
                fprintf(stderr, "Cannot convert image\n");
                exit(1);
            }
        }
    }

    cb_size = 8; /* XXX: should make it configurable. We assume the
                    HEVC encoder uses the same value */
    width = img->w;
    height = img->h;
    image_pad(img, cb_size);
    if (img_alpha)
        image_pad(img_alpha, cb_size);

    /* convert to the allocated pixel width to 8 bit if needed by the
       HEVC encoder */
    if (img->bit_depth == 8) {
        image_convert16to8(img);
        if (img_alpha)
            image_convert16to8(img_alpha);
    }
        
    if (s->frame_count == 0) {
        memset(ep, 0, sizeof(*ep));
        ep->qp = p->qp;
        ep->width = img->w;
        ep->height = img->h;
        ep->chroma_format = img->format;
        ep->bit_depth = img->bit_depth;
        ep->intra_only = !p->animated;
        ep->lossless = p->lossless;
        ep->sei_decoded_picture_hash = p->sei_decoded_picture_hash;
        ep->compress_level = p->compress_level;
        ep->verbose = p->verbose;

        s->enc_ctx = s->encoder->open(ep);
        if (!s->enc_ctx) {
            fprintf(stderr, "Error while opening encoder\n");
            exit(1);
        }

        if (img_alpha) {
            if (p->alpha_qp < 0)
                ep->qp = p->qp;
            else
                ep->qp = p->alpha_qp;
            ep->chroma_format = 0;
            
            s->alpha_enc_ctx = s->encoder->open(ep);
            if (!s->alpha_enc_ctx) {
                fprintf(stderr, "Error while opening alpha encoder\n");
                exit(1);
            }
        }

        /* prepare the extension data */
        if (p->animated) {
            BPGMetaData *md;
            uint8_t buf[15], *q;

            md = bpg_md_alloc(BPG_EXTENSION_TAG_ANIM_CONTROL);
            q = buf;
            put_ue(&q, p->loop_count);
            put_ue(&q, p->frame_delay_num);
            put_ue(&q, p->frame_delay_den);
            md->buf_len = q - buf;
            md->buf = malloc(md->buf_len);
            memcpy(md->buf, buf, md->buf_len);
            md->next = s->first_md;
            s->first_md = md;
        }

        extension_buf = NULL;
        extension_buf_len = 0;
        if (s->first_md) {
            BPGMetaData *md1;
            int max_len;
            uint8_t *q;
            
            max_len = 0;
            for(md1 = s->first_md; md1 != NULL; md1 = md1->next) {
                max_len += md1->buf_len + 5 * 2;
            }
            extension_buf = malloc(max_len);
            q = extension_buf;
            for(md1 = s->first_md; md1 != NULL; md1 = md1->next) {
                put_ue(&q, md1->tag);
                put_ue(&q, md1->buf_len);
                memcpy(q, md1->buf, md1->buf_len);
                q += md1->buf_len;
            }
            extension_buf_len = q - extension_buf;
            
            bpg_md_free(s->first_md);
            s->first_md = NULL;
        }
    
        {
            uint8_t img_header[128], *q;
            int v, has_alpha, has_extension, alpha2_flag, alpha1_flag, format;
            
            has_alpha = (img_alpha != NULL);
            has_extension = (extension_buf_len > 0);
            
            
            if (has_alpha) {
                if (img->has_w_plane) {
                    alpha1_flag = 0;
                    alpha2_flag = 1;
                } else {
                    alpha1_flag = 1;
                    alpha2_flag = img->premultiplied_alpha;
                }
            } else {
                alpha1_flag = 0;
                alpha2_flag = 0;
            }
            
            q = img_header;
            *q++ = (IMAGE_HEADER_MAGIC >> 24) & 0xff;
            *q++ = (IMAGE_HEADER_MAGIC >> 16) & 0xff;
            *q++ = (IMAGE_HEADER_MAGIC >> 8) & 0xff;
            *q++ = (IMAGE_HEADER_MAGIC >> 0) & 0xff;

            if (img->c_h_phase == 0 && img->format == BPG_FORMAT_420)
                format = BPG_FORMAT_420_VIDEO;
            else if (img->c_h_phase == 0 && img->format == BPG_FORMAT_422)
                format = BPG_FORMAT_422_VIDEO;
            else
                format = img->format;
            v = (format << 5) | (alpha1_flag << 4) | (img->bit_depth - 8);
            *q++ = v;
            v = (img->color_space << 4) | (has_extension << 3) |
                (alpha2_flag << 2) | (img->limited_range << 1) |
                p->animated;
            *q++ = v;
            put_ue(&q, width);
            put_ue(&q, height);
            
            put_ue(&q, 0); /* zero length means up to the end of the file */
            if (has_extension) {
                put_ue(&q, extension_buf_len); /* extension data length */
            }
            
            write_func(opaque, img_header, q - img_header);
            
            if (has_extension) {
                if (write_func(opaque, extension_buf, extension_buf_len) != extension_buf_len) {
                    fprintf(stderr, "Error while writing extension data\n");
                    exit(1);
                }
                free(extension_buf);
            }
        }
    }

    /* store the frame duration */
    if ((s->frame_count + 1) > s->frame_duration_tab_size) {
        s->frame_duration_tab_size = (s->frame_duration_tab_size * 3) / 2;
        if (s->frame_duration_tab_size < (s->frame_count + 1))
            s->frame_duration_tab_size = (s->frame_count + 1);
        s->frame_duration_tab = realloc(s->frame_duration_tab, 
                                        sizeof(s->frame_duration_tab) * s->frame_duration_tab_size);
    }
    s->frame_duration_tab[s->frame_count] = s->frame_ticks;

    s->encoder->encode(s->enc_ctx, img);
    
    if (img_alpha) {
        s->encoder->encode(s->alpha_enc_ctx, img_alpha);
        image_free(img_alpha);
    }
    
    s->frame_count++;

    if (!p->animated)
        bpg_encoder_encode_trailer(s, write_func, opaque);

    return 0;
}

void bpg_encoder_close(BPGEncoderContext *s)
{
    free(s->frame_duration_tab);
    bpg_md_free(s->first_md);
    free(s);
}

static int my_write_func(void *opaque, const uint8_t *buf, int buf_len)
{
    FILE *f = opaque;
    return fwrite(buf, 1, buf_len, f);
}

static int get_filename_num(char *buf, int buf_size, const char *str, int n)
{
    const char *p, *r;
    char *q;
    int l, c;

    q = buf;
    p = str;
    for(;;) {
        c = *p++;
        if (c == '\0')
            break;
        if (c == '%') {
            r = p - 1;
            l = 0;
            for(;;) {
                c = *p;
                if (c < '0' || c > '9')
                    break;
                l = l * 10 + (c - '0');
                p++;
            }
            c = *p++;
            if (c == '%') {
                goto add_char;
            } else if (c != 'd') {
                return -1;
            }
            snprintf(q, buf + buf_size - q, "%0*u", l, n);
            q += strlen(q);
            
        } else {
        add_char:
            if ((q - buf) < buf_size - 1)
                *q++ = c;
        }
    }
    *q = '\0';
    return 0;
}

void help(int is_full)
{
    char hevc_encoders[128];
    int i;

    hevc_encoders[0] = '\0';
    for(i = 0; i < HEVC_ENCODER_COUNT; i++) {
        if (i != 0)
            strcat(hevc_encoders, " ");
        strcat(hevc_encoders, hevc_encoder_name[i]);
    }
        
    printf("BPG Image Encoder version " CONFIG_BPG_VERSION "\n"
           "usage: bpgenc [options] infile.[jpg|png]\n"
           "\n"
           "Main options:\n"
           "-h                   show the full help (including the advanced options)\n"
           "-o outfile           set output filename (default = %s)\n"
           "-q qp                set quantizer parameter (smaller gives better quality,\n" 
           "                     range: 0-51, default = %d)\n"
           "-f cfmt              set the preferred chroma format (420, 422, 444,\n"
           "                     default=420)\n"
           "-c color_space       set the preferred color space (ycbcr, rgb, ycgco,\n"
           "                     ycbcr_bt709, ycbcr_bt2020, default=ycbcr)\n"
           "-b bit_depth         set the bit depth (8 to %d, default = %d)\n"
           "-lossless            enable lossless mode\n"
           "-e encoder           select the HEVC encoder (%s, default = %s)\n"
           "-m level             select the compression level (1=fast, 9=slow, default = %d)\n"
           "\n"
           "Animation options:\n"
           "-a                   generate animations from a sequence of images. Use %%d or\n"
           "                     %%Nd (N = number of digits) in the filename to specify the\n"
           "                     image index, starting from 0 or 1.\n"
           "-fps N               set the frame rate (default = 25)\n"
           "-loop N              set the number of times the animation is played. 0 means\n"
           "                     infinite (default = 0)\n"
           "-delayfile file      text file containing one number per image giving the\n"
           "                     display delay per image in centiseconds.\n"
           , DEFAULT_OUTFILENAME, DEFAULT_QP, BIT_DEPTH_MAX, DEFAULT_BIT_DEPTH,
           hevc_encoders, hevc_encoder_name[0], DEFAULT_COMPRESS_LEVEL);

    if (is_full) {
        printf("\nAdvanced options:\n"
           "-alphaq              set quantizer parameter for the alpha channel (default = same as -q value)\n"
           "-premul              store the color with premultiplied alpha\n"
           "-limitedrange        encode the color data with the limited range of video\n"
           "-hash                include MD5 hash in HEVC bitstream\n"
           "-keepmetadata        keep the metadata (from JPEG: EXIF, ICC profile, XMP, from PNG: ICC profile)\n"
           "-v                   show debug messages\n"
               );
    }

    exit(1);
}

struct option long_opts[] = {
    { "hash", no_argument },
    { "keepmetadata", no_argument },
    { "alphaq", required_argument },
    { "lossless", no_argument },
    { "limitedrange", no_argument },
    { "premul", no_argument },
    { "loop", required_argument },
    { "fps", required_argument },
    { "delayfile", required_argument },
    { NULL },
};

int main(int argc, char **argv)
{
    const char *infilename, *outfilename, *frame_delay_file;
    Image *img;
    FILE *f;
    int c, option_index;
    int keep_metadata;
    int bit_depth, i, limited_range, premultiplied_alpha;
    BPGColorSpaceEnum color_space;
    BPGMetaData *md;
    BPGEncoderContext *enc_ctx;
    BPGEncoderParameters *p;

    p = bpg_encoder_param_alloc();

    outfilename = DEFAULT_OUTFILENAME;
    color_space = BPG_CS_YCbCr;
    keep_metadata = 0;
    bit_depth = DEFAULT_BIT_DEPTH;
    limited_range = 0;
    premultiplied_alpha = 0;
    frame_delay_file = NULL;
    
    for(;;) {
        c = getopt_long_only(argc, argv, "q:o:hf:c:vm:b:e:a", long_opts, &option_index);
        if (c == -1)
            break;
        switch(c) {
        case 0:
            switch(option_index) {
            case 0:
                p->sei_decoded_picture_hash = 1;
                break;
            case 1:
                keep_metadata = 1;
                break;
            case 2:
                p->alpha_qp = atoi(optarg);
                if (p->alpha_qp < 0 || p->alpha_qp > 51) {
                    fprintf(stderr, "alpha_qp must be between 0 and 51\n");
                    exit(1);
                }
                break;
            case 3:
                p->lossless = 1;
                color_space = BPG_CS_RGB;
                p->preferred_chroma_format = BPG_FORMAT_444;
                bit_depth = 8;
                limited_range = 0;
                break;
            case 4:
                limited_range = 1;
                break;
            case 5:
                premultiplied_alpha = 1;
                break;
            case 6:
                p->loop_count = strtoul(optarg, NULL, 0);
                break;
            case 7:
                p->frame_delay_num = 1;
                p->frame_delay_den = strtoul(optarg, NULL, 0);
                if (p->frame_delay_den == 0) {
                    fprintf(stderr, "invalid frame rate\n");
                    exit(1);
                }
                break;
            case 8:
                frame_delay_file = optarg;
                break;
            default:
                goto show_help;
            }
            break;
        case 'h':
        show_help:
            help(1);
            break;
        case 'q':
            p->qp = atoi(optarg);
            if (p->qp < 0 || p->qp > 51) {
                fprintf(stderr, "qp must be between 0 and 51\n");
                exit(1);
            }
            break;
        case 'o':
            outfilename = optarg;
            break;
        case 'f':
            if (!strcmp(optarg, "420")) {
                p->preferred_chroma_format = BPG_FORMAT_420;
            } else if (!strcmp(optarg, "422")) {
                p->preferred_chroma_format = BPG_FORMAT_422;
            } else if (!strcmp(optarg, "444")) {
                p->preferred_chroma_format = BPG_FORMAT_444;
            } else if (!strcmp(optarg, "422_video")) {
                p->preferred_chroma_format = BPG_FORMAT_422_VIDEO;
            } else if (!strcmp(optarg, "420_video")) {
                p->preferred_chroma_format = BPG_FORMAT_420_VIDEO;
            } else {
                fprintf(stderr, "Invalid chroma format\n");
                exit(1);
            }
            break;
        case 'c':
            if (!strcmp(optarg, "ycbcr")) {
                color_space = BPG_CS_YCbCr;
            } else if (!strcmp(optarg, "rgb")) {
                color_space = BPG_CS_RGB;
                p->preferred_chroma_format = BPG_FORMAT_444;
            } else if (!strcmp(optarg, "ycgco")) {
                color_space = BPG_CS_YCgCo;
            } else if (!strcmp(optarg, "ycbcr_bt709")) {
                color_space = BPG_CS_YCbCr_BT709;
            } else if (!strcmp(optarg, "ycbcr_bt2020")) {
                color_space = BPG_CS_YCbCr_BT2020;
            } else {
                fprintf(stderr, "Invalid color space format\n");
                exit(1);
            }
            break;
        case 'm':
            p->compress_level = atoi(optarg);
            if (p->compress_level < 1)
                p->compress_level = 1;
            else if (p->compress_level > 9)
                p->compress_level = 9;
            break;
        case 'b':
            bit_depth = atoi(optarg);
            if (bit_depth < 8 || bit_depth > BIT_DEPTH_MAX) {
                fprintf(stderr, "Invalid bit depth (range: 8 to %d)\n",
                        BIT_DEPTH_MAX);
                exit(1);
            }
            break;
        case 'v':
            p->verbose++;
            break;
        case 'e':
            for(i = 0; i < HEVC_ENCODER_COUNT; i++) {
                if (!strcmp(optarg, hevc_encoder_name[i]))
                    break;
            }
            if (i == HEVC_ENCODER_COUNT) {
                fprintf(stderr, "Unsupported encoder. Available ones are:");
                for(i = 0; i < HEVC_ENCODER_COUNT; i++) {
                    fprintf(stderr, " %s", hevc_encoder_name[i]);
                }
                fprintf(stderr, "\n");
                exit(1);
            }
            p->encoder_type = i;
            break;
        case 'a':
            p->animated = 1;
            break;
        default:
            exit(1);
        }
    }

    if (optind >= argc) 
        help(0);
    infilename = argv[optind];

    f = fopen(outfilename, "wb");
    if (!f) {
        perror(outfilename);
        exit(1);
    }

    enc_ctx = bpg_encoder_open(p);
    if (!enc_ctx) {
        fprintf(stderr, "Could not open BPG encoder\n");
        exit(1);
    }

    if (p->animated) {
        int frame_num, first_frame, frame_ticks;
        char filename[1024];
        FILE *f1;

        if (frame_delay_file) {
            f1 = fopen(frame_delay_file, "r");
            if (!f1) {
                fprintf(stderr, "Could not open '%s'\n", frame_delay_file);
                exit(1);
            }
        } else {
            f1 = NULL;
        }

        first_frame = 1;
        for(frame_num = 0; ; frame_num++) {
            if (get_filename_num(filename, sizeof(filename), infilename, frame_num) < 0) {
                fprintf(stderr, "Invalid filename syntax: '%s'\n", infilename);
                exit(1);
            }
            img = load_image(&md, filename, color_space, bit_depth, limited_range,
                             premultiplied_alpha);
            if (!img) {
                if (frame_num == 0)
                    continue; /* accept to start at 0 or 1 */
                if (first_frame) {
                    fprintf(stderr, "Could not read '%s'\n", filename);
                    exit(1);
                } else {
                    break;
                }
            }
            frame_ticks = 1;
            if (f1) {
                float fdelay;
                if (fscanf(f1, "%f", &fdelay) == 1) {
                    frame_ticks = lrint(fdelay * p->frame_delay_den / (p->frame_delay_num * 100));
                    if (frame_ticks < 1)
                        frame_ticks = 1;
                }
            }
            
            if (p->verbose)
                printf("Encoding '%s' ticks=%d\n", filename, frame_ticks);
            
            if (keep_metadata && first_frame) {
                bpg_encoder_set_extension_data(enc_ctx, md);
            } else {
                bpg_md_free(md);
            }
            bpg_encoder_set_frame_duration(enc_ctx, frame_ticks);
            bpg_encoder_encode(enc_ctx, img, my_write_func, f);
            image_free(img);

            first_frame = 0;
        }
        if (f1)
            fclose(f1);
        /* end of stream */
        bpg_encoder_encode(enc_ctx, NULL, my_write_func, f);
    } else {
        img = load_image(&md, infilename, color_space, bit_depth, limited_range,
                         premultiplied_alpha);
        if (!img) {
            fprintf(stderr, "Could not read '%s'\n", infilename);
            exit(1);
        }
        
        if (!keep_metadata && md) {
            bpg_md_free(md);
            md = NULL;
        }
        
        bpg_encoder_set_extension_data(enc_ctx, md);
        
        bpg_encoder_encode(enc_ctx, img, my_write_func, f);
        image_free(img);
    }

    fclose(f);
    
    bpg_encoder_close(enc_ctx);
    
    bpg_encoder_param_free(p);

    return 0;
}
