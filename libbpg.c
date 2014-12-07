/*
 * libbpg
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
#include <math.h>
#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavutil/common.h>

#ifndef EMSCRIPTEN
#define USE_RGB48
//#define DEBUG
#endif

#if !defined(DEBUG)
#define NDEBUG
#endif

#include <assert.h>
#include "libbpg.h"

#define BPG_HEADER_MAGIC 0x425047fb

#define ITAPS2 4 
#define ITAPS (2 * ITAPS2) /* number of taps of the interpolation filter */

typedef uint16_t PIXEL;

typedef struct {
    int c_shift;
    int c_rnd;
    int c_one;
    int c_r_cr, c_g_cb, c_g_cr, c_b_cb;
    int c_center;
    int bit_depth;
} ColorConvertState;

typedef void ColorConvertFunc(ColorConvertState *s, 
                              uint8_t *dst, const PIXEL *y_ptr,
                              const PIXEL *cb_ptr, const PIXEL *cr_ptr,
                              int n, int incr);

struct BPGDecoderContext {
    AVFrame *frame;
    AVFrame *alpha_frame;
    int w, h;
    BPGImageFormatEnum format;
    uint8_t has_alpha;
    uint8_t bit_depth;
    BPGColorSpaceEnum color_space;
    int keep_extension_data; /* true if the extension data must be
                                kept during parsing */
    BPGExtensionData *first_md;

    /* the following is used for format conversion */
    uint8_t is_rgba;
    uint8_t is_rgb48;
    int y; /* current line */
    int w2, h2;
    const uint8_t *y_buf, *cb_buf, *cr_buf, *a_buf;
    int y_linesize, cb_linesize, cr_linesize, a_linesize;
    PIXEL *cb_buf2, *cr_buf2, *cb_buf3[ITAPS], *cr_buf3[ITAPS];
    int16_t *c_buf4;
    ColorConvertState cvt;
    ColorConvertFunc *cvt_func;
};

/* ffmpeg utilities */
void av_log(void* avcl, int level, const char *fmt, ...)
{
#ifdef DEBUG
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
#endif
}

void avpriv_report_missing_feature(void *avc, const char *msg, ...)
{
#ifdef DEBUG
    va_list ap;

    va_start(ap, msg);
    vfprintf(stderr, msg, ap);
    va_end(ap);
#endif
}

/* return < 0 if error, otherwise the consumed length */
static int get_ue(uint32_t *pv, const uint8_t *buf, int len)
{
    const uint8_t *p;
    uint32_t v;
    int a;

    if (len <= 0) 
        return -1;
    p = buf;
    a = *p++;
    len--;
    if (a < 0x80) {
        *pv = a;
        return 1;
    } else if (a == 0x80) {
        /* we don't accept non canonical encodings */
        return -1;
    }
    v = a & 0x7f;
    for(;;) {
        if (len <= 0)
            return -1;
        a = *p++;
        len--;
        v = (v << 7) | (a & 0x7f);
        if (!(a & 0x80))
            break;
    }
    *pv = v;
    return p - buf;
}

static int decode_write_frame(AVCodecContext *avctx,
                              AVFrame *frame, int *frame_count, AVPacket *pkt, int last)
{
    int len, got_frame;

    len = avcodec_decode_video2(avctx, frame, &got_frame, pkt);
    if (len < 0) {
#ifdef DEBUG
        fprintf(stderr, "Error while decoding frame %d\n", *frame_count);
#endif
        return len;
    }
    if (got_frame) {
#ifdef DEBUG
        printf("Saving %sframe %3d\n", last ? "last " : "", *frame_count);
#endif
        (*frame_count)++;
    }
    if (pkt->data) {
        pkt->size -= len;
        pkt->data += len;
    }
    return 0;
}

extern AVCodec ff_hevc_decoder;

static AVFrame *hevc_decode(const uint8_t *input_data, int input_data_len,
                            int width, int height, int chroma_format_idc,
                            int bit_depth)
{
    AVCodec *codec;
    AVCodecContext *c= NULL;
    int frame_count, idx, msps_len, ret, buf_len, i;
    AVPacket avpkt;
    AVFrame *frame;
    uint32_t len;
    uint8_t *buf, *msps_buf;

    /* build the modified SPS header to please libavcodec */
    ret = get_ue(&len, input_data, input_data_len);
    if (ret < 0)
        return NULL;
    input_data += ret;
    input_data_len -= ret;
    
    if (len > input_data_len)
        return NULL;

    msps_len = 1 + 4 + 4 + 1 + len;
    msps_buf = av_malloc(msps_len);
    idx = 0;
    msps_buf[idx++] = chroma_format_idc;
    msps_buf[idx++] = (width >> 24);
    msps_buf[idx++] = (width >> 16);
    msps_buf[idx++] = (width >> 8);
    msps_buf[idx++] = (width >> 0);
    msps_buf[idx++] = (height >> 24);
    msps_buf[idx++] = (height >> 16);
    msps_buf[idx++] = (height >> 8);
    msps_buf[idx++] = (height >> 0);
    msps_buf[idx++] = bit_depth - 8;
    memcpy(msps_buf + idx, input_data, len);
    idx += len;
    assert(idx == msps_len);
    input_data += len;
    input_data_len -= len;
    
    buf_len = 4 + 2 + msps_len * 2 + 4 + (input_data_len - len);
    buf = av_malloc(buf_len);

    idx = 0;
    /* NAL header */
    buf[idx++] = 0x00;
    buf[idx++] = 0x00;
    buf[idx++] = 0x00;
    buf[idx++] = 0x01; 
    buf[idx++] = (48 << 1); /* application specific NAL unit type */
    buf[idx++] = 1;

    /* add the modified SPS with the correct escape codes */
    i = 0;
    while (i < msps_len) {
        if ((i + 1) < msps_len && msps_buf[i] == 0 && msps_buf[i + 1] == 0) {
            buf[idx++] = 0x00;
            buf[idx++] = 0x00;
            buf[idx++] = 0x03;
            i += 2;
        } else {
            buf[idx++] = msps_buf[i++];
        }
    }
    /* the last byte cannot be 0 */
    if (idx == 0 || buf[idx - 1] == 0x00)
        buf[idx++] = 0x80;
    
    av_free(msps_buf);

    /* NAL start code (Note: should be 3 bytes depending on exact NAL
       type, but it is not critical for libavcodec) */
    buf[idx++] = 0x00;
    buf[idx++] = 0x00;
    buf[idx++] = 0x00;
    buf[idx++] = 0x01; 

    memcpy(buf + idx, input_data, input_data_len);
    idx += input_data_len;
    
    assert(idx < buf_len);

    av_init_packet(&avpkt);

    codec = &ff_hevc_decoder;

    c = avcodec_alloc_context3(codec);
    if (!c) {
#ifdef DEBUG
        fprintf(stderr, "Could not allocate video codec context\n");
#endif
        exit(1);
    }

    if(codec->capabilities&CODEC_CAP_TRUNCATED)
        c->flags|= CODEC_FLAG_TRUNCATED; /* we do not send complete frames */

    /* for testing: use the MD5 or CRC in SEI to check the decoded bit
       stream. */
    c->err_recognition |= AV_EF_CRCCHECK; 

    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
#ifdef DEBUG
        fprintf(stderr, "Could not open codec\n");
#endif
        exit(1);
    }
    
    frame = av_frame_alloc();
    if (!frame) {
#ifdef DEBUG
        fprintf(stderr, "Could not allocate video frame\n");
#endif
        return NULL;
    }

    avpkt.size = idx;
    avpkt.data = buf;
    frame_count = 0;
    while (avpkt.size > 0) {
        if (decode_write_frame(c, frame, &frame_count, &avpkt, 0) < 0)
            exit(1);
    }

    avcodec_close(c);
    av_free(c);
    av_free(buf);

    if (frame_count == 0) {
        av_frame_free(&frame);
        return NULL;
    } else {
        return frame;
    }
}

uint8_t *bpg_decoder_get_data(BPGDecoderContext *img, int *pline_size, int plane)
{
    int c_count;
    if (img->format == BPG_FORMAT_GRAY) 
        c_count = 1;
    else
        c_count = 3;
    if (plane < c_count) {
        *pline_size = img->frame->linesize[plane];
        return img->frame->data[plane];
    } else if (img->has_alpha && plane == c_count) {
        *pline_size = img->alpha_frame->linesize[0];
        return img->alpha_frame->data[0];
    } else {
        *pline_size = 0;
        return NULL;
    }
}

int bpg_decoder_get_info(BPGDecoderContext *img, BPGImageInfo *p)
{
    if (!img->frame)
        return -1;
    p->width = img->w;
    p->height = img->h;
    p->format = img->format;
    if (img->color_space == BPG_CS_YCbCrK || 
        img->color_space == BPG_CS_CMYK)
        p->has_alpha = 0;
    else
        p->has_alpha = img->has_alpha;
    p->color_space = img->color_space;
    p->bit_depth = img->bit_depth;
    return 0;
}

static inline int clamp_pix(int a, int pixel_max)
{
    if (a < 0)
        return 0;
    else if (a > pixel_max)
        return pixel_max;
    else
        return a;
}

static inline int clamp8(int a)
{
    if (a < 0)
        return 0;
    else if (a > 255)
        return 255;
    else
        return a;
}

/* 7 tap Lanczos interpolator */
#define IC0 (-1)
#define IC1 4
#define IC2 (-10)
#define IC3 57
#define IC4 18
#define IC5 (-6)
#define IC6 2

/* interpolate by a factor of two assuming chroma is between the luma
   samples. */
static void interp2_simple(PIXEL *dst, const PIXEL *src, int n, int bit_depth)
{
    int pixel_max, a0, a1, a2, a3, a4, a5, a6;

    pixel_max = (1 << bit_depth) - 1;

    a1 = src[-3];
    a2 = src[-2];
    a3 = src[-1];
    a4 = src[0];
    a5 = src[1];
    a6 = src[2];

    while (n >= 2) {
        a0 = a1;
        a1 = a2;
        a2 = a3;
        a3 = a4;
        a4 = a5;
        a5 = a6;
        a6 = src[3];
        dst[0] = clamp_pix((a0 * IC6 + a1 * IC5 + a2 * IC4 + a3 * IC3 + 
                            a4 * IC2 + a5 * IC1 + a6 * IC0 + 32) >> 6, 
                           pixel_max);
        dst[1] = clamp_pix((a0 * IC0 + a1 * IC1 + a2 * IC2 + a3 * IC3 +
                            a4 * IC4 + a5 * IC5 + a6 * IC6 + 32) >> 6, 
                           pixel_max);
        dst += 2;
        src++;
        n -= 2;
    }
    if (n) {
        a0 = a1;
        a1 = a2;
        a2 = a3;
        a3 = a4;
        a4 = a5;
        a5 = a6;
        a6 = src[3];
        dst[0] = clamp_pix((a0 * IC6 + a1 * IC5 + a2 * IC4 + a3 * IC3 + 
                            a4 * IC2 + a5 * IC1 + a6 * IC0 + 32) >> 6, 
                           pixel_max);
    }
}

static void interp2_h(PIXEL *dst, const PIXEL *src, int n, int bit_depth)
{
    PIXEL *src1, v;
    int i, n2;

    /* add extra pixels and do the interpolation (XXX: could go faster) */
    n2 = (n + 1) / 2;
    src1 = av_malloc((n2 + ITAPS - 1) * sizeof(PIXEL));
    memcpy(src1 + ITAPS2 - 1, src, n2 * sizeof(PIXEL));

    v = src[0];
    for(i = 0; i < ITAPS2 - 1; i++)
        src1[i] = v;

    v = src[n2 - 1];
    for(i = 0; i < ITAPS2; i++)
        src1[ITAPS2 - 1 + n2 + i] = v;
    interp2_simple(dst, src1 + ITAPS2 - 1, n, bit_depth);
    av_free(src1);
}

static void interp2_simple2(PIXEL *dst, const int16_t *src, int n, 
                            int bit_depth)
{
    int shift, offset, pixel_max, a0, a1, a2, a3, a4, a5, a6;

    pixel_max = (1 << bit_depth) - 1;
    shift = 20 - bit_depth;
    offset = 1 << (shift - 1);

    a1 = src[-3];
    a2 = src[-2];
    a3 = src[-1];
    a4 = src[0];
    a5 = src[1];
    a6 = src[2];

    while (n >= 2) {
        a0 = a1;
        a1 = a2;
        a2 = a3;
        a3 = a4;
        a4 = a5;
        a5 = a6;
        a6 = src[3];
        dst[0] = clamp_pix((a0 * IC6 + a1 * IC5 + a2 * IC4 + a3 * IC3 +
                            a4 * IC2 + a5 * IC1 + a6 * IC0 + offset) >> shift,
                           pixel_max);
        dst[1] = clamp_pix((a0 * IC0 + a1 * IC1 + a2 * IC2 + a3 * IC3 +
                            a4 * IC4 + a5 * IC5 + a6 * IC6 + offset) >> shift,
                           pixel_max);
        dst += 2;
        src++;
        n -= 2;
    }
    if (n) {
        a0 = a1;
        a1 = a2;
        a2 = a3;
        a3 = a4;
        a4 = a5;
        a5 = a6;
        a6 = src[3];
        dst[0] = clamp_pix((a0 * IC6 + a1 * IC5 + a2 * IC4 + a3 * IC3 +
                            a4 * IC2 + a5 * IC1 + a6 * IC0 + offset) >> shift, 
                           pixel_max);
    }
}

/* y_pos is the position of the sample '0' in the 'src' circular
   buffer. tmp is a temporary buffer of length (n2 + 2 * ITAPS - 1) */
static void interp2_vh(PIXEL *dst, PIXEL **src, int n, int y_pos,
                       int16_t *tmp_buf, int bit_depth, int frac_pos)
{
    const PIXEL *src0, *src1, *src2, *src3, *src4, *src5, *src6;
    int i, n2, shift;
    PIXEL v;

    src0 = src[(y_pos - 3) & 7];
    src1 = src[(y_pos - 2) & 7];
    src2 = src[(y_pos - 1) & 7];
    src3 = src[(y_pos + 0) & 7];
    src4 = src[(y_pos + 1) & 7];
    src5 = src[(y_pos + 2) & 7];
    src6 = src[(y_pos + 3) & 7];

    /* vertical interpolation first */
    /* XXX: should round but not critical */
    shift = bit_depth - 8;
    n2 = (n + 1) / 2;
    if (frac_pos == 0) {
        for(i = 0; i < n2; i++) {
            tmp_buf[ITAPS2 - 1 + i] = 
                (src0[i] * IC6 + src1[i] * IC5 + 
                 src2[i] * IC4 + src3[i] * IC3 + 
                 src4[i] * IC2 + src5[i] * IC1 + 
                 src6[i] * IC0) >> shift;
        }
    } else {
        for(i = 0; i < n2; i++) {
            tmp_buf[ITAPS2 - 1 + i] = 
                (src0[i] * IC0 + src1[i] * IC1 + 
                 src2[i] * IC2 + src3[i] * IC3 + 
                 src4[i] * IC4 + src5[i] * IC5 + 
                 src6[i] * IC6) >> shift;
        }
    }

    /* then horizontal interpolation */
    v = tmp_buf[ITAPS2 - 1];
    for(i = 0; i < ITAPS2 - 1; i++)
        tmp_buf[i] = v;
    v = tmp_buf[ITAPS2 - 1 + n2 - 1];
    for(i = 0; i < ITAPS2; i++)
        tmp_buf[ITAPS2 - 1 + n2 + i] = v;
    interp2_simple2(dst, tmp_buf + ITAPS2 - 1, n, bit_depth);
}

static void ycc_to_rgb24(ColorConvertState *s, uint8_t *dst, const PIXEL *y_ptr,
                         const PIXEL *cb_ptr, const PIXEL *cr_ptr,
                         int n, int incr)
{
    uint8_t *q = dst;
    int y_val, cb_val, cr_val, x;
    int c_r_cr, c_g_cb, c_g_cr, c_b_cb, rnd, shift, center, c_one;

    c_r_cr = s->c_r_cr;
    c_g_cb = s->c_g_cb;
    c_g_cr = s->c_g_cr;
    c_b_cb = s->c_b_cb;
    c_one = s->c_one;
    rnd = s->c_rnd;
    shift = s->c_shift;
    center = s->c_center;
    for(x = 0; x < n; x++) {
        y_val = y_ptr[x] * c_one;
        cb_val = cb_ptr[x] - center;
        cr_val = cr_ptr[x] - center;
        q[0] = clamp8((y_val + c_r_cr * cr_val + rnd) >> shift);
        q[1] = clamp8((y_val - c_g_cb * cb_val - c_g_cr * cr_val + rnd) >> shift);
        q[2] = clamp8((y_val + c_b_cb * cb_val + rnd) >> shift);
        q += incr;
    }
}

static void ycgco_to_rgb24(ColorConvertState *s, 
                           uint8_t *dst, const PIXEL *y_ptr,
                           const PIXEL *cb_ptr, const PIXEL *cr_ptr,
                           int n, int incr)
{
    uint8_t *q = dst;
    int y_val, cb_val, cr_val, x;
    int rnd, shift, center, c_one;

    c_one = s->c_one;
    rnd = s->c_rnd;
    shift = s->c_shift;
    center = s->c_center;
    for(x = 0; x < n; x++) {
        y_val = y_ptr[x];
        cb_val = cb_ptr[x] - center;
        cr_val = cr_ptr[x] - center;
        q[0] = clamp8(((y_val - cb_val + cr_val) * c_one + rnd) >> shift);
        q[1] = clamp8(((y_val + cb_val) * c_one + rnd) >> shift);
        q[2] = clamp8(((y_val - cb_val - cr_val) * c_one + rnd) >> shift);
        q += incr;
    }
}

/* c = c * alpha */
static void alpha_combine8(ColorConvertState *s, 
                           uint8_t *dst, const PIXEL *a_ptr, int n, int incr)
{
    uint8_t *q = dst;
    int x, a_val, shift, rnd;

    shift = s->bit_depth;
    rnd = 1 << (shift - 1);
    for(x = 0; x < n; x++) {
        a_val = a_ptr[x];
        /* XXX: not accurate enough */
        q[0] = (q[0] * a_val + rnd) >> shift;
        q[1] = (q[1] * a_val + rnd) >> shift;
        q[2] = (q[2] * a_val + rnd) >> shift;
        q += incr;
    }
}

static void gray_to_rgb24(ColorConvertState *s, 
                          uint8_t *dst, const PIXEL *y_ptr,
                          int n, int incr)
{
    uint8_t *q = dst;
    int x, y_val, c, rnd, shift;

    if (s->bit_depth == 8) {
        for(x = 0; x < n; x++) {
            y_val = y_ptr[x];
            q[0] = y_val;
            q[1] = y_val;
            q[2] = y_val;
            q += incr;
        }
    } else {
        c = s->c_one;
        rnd = s->c_rnd;
        shift = s->c_shift;
        for(x = 0; x < n; x++) {
            y_val = (y_ptr[x] * c + rnd) >> shift;
            q[0] = y_val;
            q[1] = y_val;
            q[2] = y_val;
            q += incr;
        }
    }
}

static void rgb_to_rgb24(ColorConvertState *s, uint8_t *dst, const PIXEL *y_ptr,
                         const PIXEL *cb_ptr, const PIXEL *cr_ptr,
                         int n, int incr)
{
    uint8_t *q = dst;
    int x, c, rnd, shift;

    if (s->bit_depth == 8) {
        for(x = 0; x < n; x++) {
            q[0] = cr_ptr[x];
            q[1] = y_ptr[x];
            q[2] = cb_ptr[x];
            q += incr;
        }
    } else {
        c = s->c_one;
        rnd = s->c_rnd;
        shift = s->c_shift;
        for(x = 0; x < n; x++) {
            q[0] = (cr_ptr[x] * c + rnd) >> shift;
            q[1] = (y_ptr[x] * c + rnd) >> shift;
            q[2] = (cb_ptr[x] * c + rnd) >> shift;
            q += incr;
        }
    }
}

static void put_dummy_gray8(uint8_t *dst, int n, int incr)
{
    int x;
    for(x = 0; x < n; x++) {
        dst[0] = 0xff;
        dst += incr;
    }
}

static void gray_to_gray8(ColorConvertState *s, 
                          uint8_t *dst, const PIXEL *y_ptr,
                          int n, int incr)
{
    uint8_t *q = dst;
    int x, y_val, c, rnd, shift;

    if (s->bit_depth == 8) {
        for(x = 0; x < n; x++) {
            y_val = y_ptr[x];
            q[0] = y_val;
            q += incr;
        }
    } else {
        c = s->c_one;
        rnd = s->c_rnd;
        shift = s->c_shift;
        for(x = 0; x < n; x++) {
            y_val = (y_ptr[x] * c + rnd) >> shift;
            q[0] = y_val;
            q += incr;
        }
    }
}

static ColorConvertFunc *cs_to_rgb24[BPG_CS_COUNT] = {
    ycc_to_rgb24,
    rgb_to_rgb24,
    ycgco_to_rgb24,
    ycc_to_rgb24,
    rgb_to_rgb24,
};

#ifdef USE_RGB48

/* 16 bit output */

static inline int clamp16(int a)
{
    if (a < 0)
        return 0;
    else if (a > 65535)
        return 65535;
    else
        return a;
}

static void ycc_to_rgb48(ColorConvertState *s, uint8_t *dst, const PIXEL *y_ptr,
                         const PIXEL *cb_ptr, const PIXEL *cr_ptr,
                         int n, int incr)
{
    uint16_t *q = (uint16_t *)dst;
    int y_val, cb_val, cr_val, x;
    int c_r_cr, c_g_cb, c_g_cr, c_b_cb, rnd, shift, center, c_one;

    c_r_cr = s->c_r_cr;
    c_g_cb = s->c_g_cb;
    c_g_cr = s->c_g_cr;
    c_b_cb = s->c_b_cb;
    c_one = s->c_one;
    rnd = s->c_rnd;
    shift = s->c_shift;
    center = s->c_center;
    for(x = 0; x < n; x++) {
        y_val = y_ptr[x] * c_one;
        cb_val = cb_ptr[x] - center;
        cr_val = cr_ptr[x] - center;
        q[0] = clamp16((y_val + c_r_cr * cr_val + rnd) >> shift);
        q[1] = clamp16((y_val - c_g_cb * cb_val - c_g_cr * cr_val + rnd) >> shift);
        q[2] = clamp16((y_val + c_b_cb * cb_val + rnd) >> shift);
        q += incr;
    }
}

static void ycgco_to_rgb48(ColorConvertState *s, 
                           uint8_t *dst, const PIXEL *y_ptr,
                           const PIXEL *cb_ptr, const PIXEL *cr_ptr,
                           int n, int incr)
{
    uint16_t *q = (uint16_t *)dst;
    int y_val, cb_val, cr_val, x;
    int rnd, shift, center, c_one;

    c_one = s->c_one;
    rnd = s->c_rnd;
    shift = s->c_shift;
    center = s->c_center;
    for(x = 0; x < n; x++) {
        y_val = y_ptr[x];
        cb_val = cb_ptr[x] - center;
        cr_val = cr_ptr[x] - center;
        q[0] = clamp16(((y_val - cb_val + cr_val) * c_one + rnd) >> shift);
        q[1] = clamp16(((y_val + cb_val) * c_one + rnd) >> shift);
        q[2] = clamp16(((y_val - cb_val - cr_val) * c_one + rnd) >> shift);
        q += incr;
    }
}

static void gray_to_rgb48(ColorConvertState *s, 
                          uint16_t *dst, const PIXEL *y_ptr,
                          int n, int incr)
{
    uint16_t *q = dst;
    int x, y_val, c, rnd, shift;

    c = s->c_one;
    rnd = s->c_rnd;
    shift = s->c_shift;
    for(x = 0; x < n; x++) {
        y_val = (y_ptr[x] * c + rnd) >> shift;
        q[0] = y_val;
        q[1] = y_val;
        q[2] = y_val;
        q += incr;
    }
}

static void gray_to_gray16(ColorConvertState *s, 
                           uint16_t *dst, const PIXEL *y_ptr,
                           int n, int incr)
{
    uint16_t *q = dst;
    int x, y_val, c, rnd, shift;

    c = s->c_one;
    rnd = s->c_rnd;
    shift = s->c_shift;
    for(x = 0; x < n; x++) {
        y_val = (y_ptr[x] * c + rnd) >> shift;
        q[0] = y_val;
        q += incr;
    }
}

static void rgb_to_rgb48(ColorConvertState *s, 
                         uint8_t *dst, const PIXEL *y_ptr,
                         const PIXEL *cb_ptr, const PIXEL *cr_ptr,
                         int n, int incr)
{
    gray_to_gray16(s, (uint16_t *)dst + 1, y_ptr, n, incr);
    gray_to_gray16(s, (uint16_t *)dst + 2, cb_ptr, n, incr);
    gray_to_gray16(s, (uint16_t *)dst + 0, cr_ptr, n, incr);
}

static void put_dummy_gray16(uint16_t *dst, int n, int incr)
{
    int x;
    for(x = 0; x < n; x++) {
        dst[0] = 0xffff;
        dst += incr;
    }
}

/* c = c * alpha */
static void alpha_combine16(ColorConvertState *s, 
                            uint16_t *dst, const PIXEL *a_ptr, int n, int incr)
{
    uint16_t *q = dst;
    int x, a_val, shift, rnd;

    shift = s->bit_depth;
    rnd = 1 << (shift - 1);
    for(x = 0; x < n; x++) {
        a_val = a_ptr[x];
        /* XXX: not accurate enough */
        q[0] = (q[0] * a_val + rnd) >> shift;
        q[1] = (q[1] * a_val + rnd) >> shift;
        q[2] = (q[2] * a_val + rnd) >> shift;
        q += incr;
    }
}

static ColorConvertFunc *cs_to_rgb48[BPG_CS_COUNT] = {
    ycc_to_rgb48,
    rgb_to_rgb48,
    ycgco_to_rgb48,
    ycc_to_rgb48,
    rgb_to_rgb48,
};
#endif

static void convert_init(ColorConvertState *s, 
                         int in_bit_depth, int out_bit_depth)
{
    int c_shift, in_pixel_max, out_pixel_max;
    double mult;

    c_shift = 30 - out_bit_depth;
    in_pixel_max = (1 << in_bit_depth) - 1;
    out_pixel_max = (1 << out_bit_depth) - 1;
    mult = (double)out_pixel_max * (1 << c_shift) / (double)in_pixel_max;

    s->c_r_cr = lrint(1.40200 * mult); /* 2*(1-k_r) */
    s->c_g_cb = lrint(0.3441362862 * mult); /* -2*k_b*(1-k_b)/(1-k_b-k_r) */
    s->c_g_cr = lrint(0.7141362862 * mult); /* -2*k_r*(1-k_r)/(1-k_b-k_r) */
    s->c_b_cb = lrint(1.77200 * mult); /* 2*(1-k_b) */
    s->c_one = lrint(mult);
    s->c_shift = c_shift;
    s->c_rnd = (1 << (c_shift - 1));
    s->c_center = 1 << (in_bit_depth - 1);
    s->bit_depth = in_bit_depth;
}

static int bpg_decoder_output_init(BPGDecoderContext *s,
                                   BPGDecoderOutputFormat out_fmt)
{
    int i, y1, c_idx;
    PIXEL *cb_ptr, *cr_ptr;

#ifdef USE_RGB48
    if ((unsigned)out_fmt > BPG_OUTPUT_FORMAT_RGBA64)
        return -1;
#else
    if ((unsigned)out_fmt > BPG_OUTPUT_FORMAT_RGBA32)
        return -1;
#endif
    s->is_rgba = (out_fmt == BPG_OUTPUT_FORMAT_RGBA32 ||
                    out_fmt == BPG_OUTPUT_FORMAT_RGBA64);
    s->is_rgb48 = (out_fmt == BPG_OUTPUT_FORMAT_RGB48 ||
                     out_fmt == BPG_OUTPUT_FORMAT_RGBA64);

    s->y_buf = bpg_decoder_get_data(s, &s->y_linesize, 0);
    if (s->format != BPG_FORMAT_GRAY) {
        s->cb_buf = bpg_decoder_get_data(s, &s->cb_linesize, 1);
        s->cr_buf = bpg_decoder_get_data(s, &s->cr_linesize, 2);
        c_idx = 3;
    } else {
        c_idx = 1;
    }
    if (s->has_alpha)
        s->a_buf = bpg_decoder_get_data(s, &s->a_linesize, c_idx);
    else
        s->a_buf = NULL;

    if (s->format == BPG_FORMAT_420 || s->format == BPG_FORMAT_422) {
        s->w2 = (s->w + 1) / 2;
        s->h2 = (s->h + 1) / 2;
        s->cb_buf2 = av_malloc(s->w * sizeof(PIXEL));
        s->cr_buf2 = av_malloc(s->w * sizeof(PIXEL));

        if (s->format == BPG_FORMAT_420) {
            for(i = 0; i < ITAPS; i++) {
                s->cb_buf3[i] = av_malloc(s->w2 * sizeof(PIXEL));
                s->cr_buf3[i] = av_malloc(s->w2 * sizeof(PIXEL));
            }
            s->c_buf4 = av_malloc((s->w2 + 2 * ITAPS2 - 1) * sizeof(int16_t));
            
            /* init the vertical interpolation buffer */
            for(i = 0; i < ITAPS; i++) {
                y1 = i;
                if (y1 > ITAPS2)
                    y1 -= ITAPS;
                if (y1 < 0)
                    y1 = 0;
                else if (y1 >= s->h2)
                    y1 = s->h2 - 1;
                cb_ptr = (PIXEL *)(s->cb_buf + y1 * s->cb_linesize);
                cr_ptr = (PIXEL *)(s->cr_buf + y1 * s->cr_linesize);
                memcpy(s->cb_buf3[i], cb_ptr, s->w2 * sizeof(PIXEL));
                memcpy(s->cr_buf3[i], cr_ptr, s->w2 * sizeof(PIXEL));
            }
        }
    }
    convert_init(&s->cvt, s->bit_depth, s->is_rgb48 ? 16 : 8);

    if (s->format == BPG_FORMAT_GRAY) {
        s->cvt_func = NULL;
    } else {
#ifdef USE_RGB48
        if (s->is_rgb48) {
            s->cvt_func = cs_to_rgb48[s->color_space];
        } else
#endif
        {
            s->cvt_func = cs_to_rgb24[s->color_space];
        }
    }
    return 0;
}

static void bpg_decoder_output_end(BPGDecoderContext *s)
{
    int i;

    av_free(s->cb_buf2);
    av_free(s->cr_buf2);
    for(i = 0; i < ITAPS; i++) {
        av_free(s->cb_buf3[i]);
        av_free(s->cr_buf3[i]);
    }
    av_free(s->c_buf4);
}

int bpg_decoder_get_line(BPGDecoderContext *s, void *rgb_line1)
{
    uint8_t *rgb_line = rgb_line1;
    int w, h, y, pos, y2, y1, incr;
    PIXEL *y_ptr, *cb_ptr, *cr_ptr, *a_ptr;

    w = s->w;
    h = s->h;
    y = s->y;
    
    if ((unsigned)y >= h)
        return -1;
    
    y_ptr = (PIXEL *)(s->y_buf + y * s->y_linesize);
    incr = 3 + s->is_rgba;
    switch(s->format) {
    case BPG_FORMAT_GRAY:
#ifdef USE_RGB48
        if (s->is_rgb48) {
            gray_to_rgb48(&s->cvt, (uint16_t *)rgb_line, y_ptr, w, incr);
        } else 
#endif
        {
            gray_to_rgb24(&s->cvt, rgb_line, y_ptr, w, incr);
        }
        break;
    case BPG_FORMAT_420:
        y2 = y >> 1;
        pos = y2 % ITAPS;
        if ((y & 1) == 0) {
            interp2_vh(s->cb_buf2, s->cb_buf3, w, pos, s->c_buf4,
                       s->bit_depth, 0);
            interp2_vh(s->cr_buf2, s->cr_buf3, w, pos, s->c_buf4,
                       s->bit_depth, 0);
        } else {
            interp2_vh(s->cb_buf2, s->cb_buf3, w, pos, s->c_buf4,
                       s->bit_depth, 1);
            interp2_vh(s->cr_buf2, s->cr_buf3, w, pos, s->c_buf4,
                       s->bit_depth, 1);

            /* add a new line in the circular buffer */
            pos = (pos + ITAPS2 + 1) % ITAPS;
            y1 = y2 + ITAPS2 + 1;
            if (y1 >= s->h2)
                y1 = s->h2 - 1;
            cb_ptr = (PIXEL *)(s->cb_buf + y1 * s->cb_linesize);
            cr_ptr = (PIXEL *)(s->cr_buf + y1 * s->cr_linesize);
            memcpy(s->cb_buf3[pos], cb_ptr, s->w2 * sizeof(PIXEL));
            memcpy(s->cr_buf3[pos], cr_ptr, s->w2 * sizeof(PIXEL));
        }
        s->cvt_func(&s->cvt, rgb_line, y_ptr, s->cb_buf2, s->cr_buf2, w, incr);
        break;
    case BPG_FORMAT_422:
        cb_ptr = (PIXEL *)(s->cb_buf + y * s->cb_linesize);
        cr_ptr = (PIXEL *)(s->cr_buf + y * s->cr_linesize);
        interp2_h(s->cb_buf2, cb_ptr, w, s->bit_depth);
        interp2_h(s->cr_buf2, cr_ptr, w, s->bit_depth);
        s->cvt_func(&s->cvt, rgb_line, y_ptr, s->cb_buf2, s->cr_buf2, w, incr);
        break;
    case BPG_FORMAT_444:
        cb_ptr = (PIXEL *)(s->cb_buf + y * s->cb_linesize);
        cr_ptr = (PIXEL *)(s->cr_buf + y * s->cr_linesize);
        s->cvt_func(&s->cvt, rgb_line, y_ptr, cb_ptr, cr_ptr, w, incr);
        break;
    default:
        return -1;
    }

    /* alpha output or CMYK handling */
    if (s->color_space == BPG_CS_YCbCrK || 
        s->color_space == BPG_CS_CMYK) {
        a_ptr = (PIXEL *)(s->a_buf + y * s->a_linesize);
#ifdef USE_RGB48
        if (s->is_rgb48) {
            alpha_combine16(&s->cvt, (uint16_t *)rgb_line, a_ptr, w, incr);
            if (s->is_rgba)
                put_dummy_gray16((uint16_t *)rgb_line + 3, w, 4);
        } else
#endif
        {
            alpha_combine8(&s->cvt, rgb_line, a_ptr, w, incr);
            if (s->is_rgba)
                put_dummy_gray8(rgb_line + 3, w, 4);
        }
    } else {
        if (s->is_rgba) {
#ifdef USE_RGB48
            if (s->is_rgb48) {
                if (s->has_alpha) {
                    a_ptr = (PIXEL *)(s->a_buf + y * s->a_linesize);
                    gray_to_gray16(&s->cvt, 
                                   (uint16_t *)rgb_line + 3, a_ptr, w, 4);
                } else {
                    put_dummy_gray16((uint16_t *)rgb_line + 3, w, 4);
                }
            } else
#endif
            {
                if (s->has_alpha) {
                    a_ptr = (PIXEL *)(s->a_buf + y * s->a_linesize);
                    gray_to_gray8(&s->cvt, rgb_line + 3, a_ptr, w, 4);
                } else {
                    put_dummy_gray8(rgb_line + 3, w, 4);
                }
            }
        }
    }

    /* go to next line */
    s->y++;
    return 0;
}

int bpg_decoder_start(BPGDecoderContext *s, BPGDecoderOutputFormat out_fmt)
{
    int ret;

    if (!s->frame || s->y >= 0)
        return -1;
    ret = bpg_decoder_output_init(s, out_fmt);
    if (ret)
        return ret;
    s->y = 0;
    return 0;
}

BPGDecoderContext *bpg_decoder_open(void)
{
    BPGDecoderContext *s;

    s = av_mallocz(sizeof(BPGDecoderContext));
    if (!s)
        return NULL;
    return s;
}

typedef struct {
    int width, height;
    BPGImageFormatEnum format;
    uint8_t has_alpha;
    uint8_t bit_depth;
    BPGColorSpaceEnum color_space;
    uint32_t ycc_data_len;
    uint32_t alpha_data_len;
    BPGExtensionData *first_md;
} BPGHeaderData;

static int bpg_decode_header(BPGHeaderData *h,
                             const uint8_t *buf, int buf_len,
                             int header_only, int load_extensions)
{
    int idx, flags1, flags2, has_extension, ret;
    uint32_t extension_data_len;

    if (buf_len < 6)
        return -1;
    /* check magic */
    if (buf[0] != ((BPG_HEADER_MAGIC >> 24) & 0xff) ||
        buf[1] != ((BPG_HEADER_MAGIC >> 16) & 0xff) ||
        buf[2] != ((BPG_HEADER_MAGIC >> 8) & 0xff) ||
        buf[3] != ((BPG_HEADER_MAGIC >> 0) & 0xff))
        return -1;
    idx = 4;
    flags1 = buf[idx++];
    h->format = flags1 >> 5;
    if (h->format > 3)
        return -1;
    h->has_alpha = (flags1 >> 4) & 1;
    h->bit_depth = (flags1 & 0xf) + 8;
    if (h->bit_depth > 14)
        return -1;
    flags2 = buf[idx++];
    h->color_space = (flags2 >> 4) & 0xf;
    has_extension = (flags2 >> 3) & 1;
    if (h->color_space >= BPG_CS_COUNT || 
        (h->format == BPG_FORMAT_GRAY && h->color_space != 0) ||
        ((h->color_space == BPG_CS_YCbCrK || h->color_space == BPG_CS_CMYK) &&
         !h->has_alpha))
        return -1;
    ret = get_ue(&h->width, buf + idx, buf_len - idx);
    if (ret < 0)
        return -1;
    idx += ret;
    ret = get_ue(&h->height, buf + idx, buf_len - idx);
    if (ret < 0)
        return -1;
    idx += ret;
    if (h->width == 0 || h->height == 0)
        return -1;
    if (header_only)
        return idx;

    ret = get_ue(&h->ycc_data_len, buf + idx, buf_len - idx);
    if (ret < 0)
        return -1;
    idx += ret;
           
    extension_data_len = 0;
    if (has_extension) {
        ret = get_ue(&extension_data_len, buf + idx, buf_len - idx);
        if (ret < 0)
            return -1;
        idx += ret;
    }

    h->alpha_data_len = 0;
    if (h->has_alpha) {
        ret = get_ue(&h->alpha_data_len, buf + idx, buf_len - idx);
        if (ret < 0)
            return -1;
        idx += ret;
    }

    h->first_md = NULL;
    if (has_extension) {
        int ext_end;

        ext_end = idx + extension_data_len;
        if (ext_end > buf_len)
            return -1;
#ifndef EMSCRIPTEN
        if (load_extensions) {
            BPGExtensionData *md, **plast_md;

            plast_md = &h->first_md;
            while (idx < ext_end) {
                md = av_malloc(sizeof(BPGExtensionData));
                *plast_md = md;
                plast_md = &md->next;

                ret = get_ue(&md->tag, buf + idx, ext_end - idx);
                if (ret < 0) 
                    goto fail;
                idx += ret;

                ret = get_ue(&md->buf_len, buf + idx, ext_end - idx);
                if (ret < 0) 
                    goto fail;
                idx += ret;
                
                if (idx + md->buf_len > ext_end) {
                fail:
                    bpg_decoder_free_extension_data(h->first_md);
                    return -1;
                }
                md->buf = av_malloc(md->buf_len);
                memcpy(md->buf, buf + idx, md->buf_len);
                idx += md->buf_len;
            }
        } else
#endif
        {
            /* skip extension data */
            idx += extension_data_len;
        }
    }
    return idx;
}

int bpg_decoder_decode(BPGDecoderContext *img, const uint8_t *buf, int buf_len)
{
    int idx, has_alpha, format, bit_depth, chroma_format_idc, color_space;
    uint32_t width, height;
    BPGHeaderData h_s, *h = &h_s;

    idx = bpg_decode_header(h, buf, buf_len, 0, img->keep_extension_data);
    if (idx < 0)
        return idx;
    width = h->width;
    height = h->height;
    format = h->format;
    has_alpha = h->has_alpha;
    color_space = h->color_space;
    bit_depth = h->bit_depth;
    
    img->w = width;
    img->h = height;
    img->format = format;
    img->has_alpha = has_alpha;
    img->color_space = color_space;
    img->bit_depth = bit_depth;
    img->first_md = h->first_md;

    if (idx + h->ycc_data_len > buf_len)
        goto fail;
    chroma_format_idc = format;
    img->frame = hevc_decode(buf + idx, h->ycc_data_len,
                             width, height, chroma_format_idc, bit_depth);
    if (!img->frame)
        goto fail;
    idx += h->ycc_data_len;

    if (img->frame->width < img->w || img->frame->height < img->h)
        goto fail;
    
    switch(img->frame->format) {
    case AV_PIX_FMT_YUV420P16:
    case AV_PIX_FMT_YUV420P:
        if (format != BPG_FORMAT_420)
            goto fail;
        break;
    case AV_PIX_FMT_YUV422P16:
    case AV_PIX_FMT_YUV422P:
        if (format != BPG_FORMAT_422)
            goto fail;
        break;
    case AV_PIX_FMT_YUV444P16:
    case AV_PIX_FMT_YUV444P:
        if (format != BPG_FORMAT_444)
            goto fail;
        break;
    case AV_PIX_FMT_GRAY16:
    case AV_PIX_FMT_GRAY8:
        if (format != BPG_FORMAT_GRAY)
            goto fail;
        break;
    default:
        goto fail;
    }
    
    if (has_alpha) {
        if (idx + h->alpha_data_len > buf_len)
            goto fail;
        img->alpha_frame = hevc_decode(buf + idx, h->alpha_data_len,
                                       width, height, 0, bit_depth);
        if (!img->alpha_frame)
            goto fail;
        idx += h->alpha_data_len;
    }

    img->y = -1;
    return 0;

 fail:
    if (img->frame)
        av_frame_free(&img->frame);
    if (img->alpha_frame)
        av_frame_free(&img->alpha_frame);
    bpg_decoder_free_extension_data(img->first_md);
    img->first_md = NULL;
    return -1;
}

void bpg_decoder_close(BPGDecoderContext *s)
{
    bpg_decoder_output_end(s);
    if (s->frame)
        av_frame_free(&s->frame);
    if (s->alpha_frame)
        av_frame_free(&s->alpha_frame);
    bpg_decoder_free_extension_data(s->first_md);
    av_free(s);
}

void bpg_decoder_free_extension_data(BPGExtensionData *first_md)
{
#ifndef EMSCRIPTEN
    BPGExtensionData *md, *md_next;
    
    for(md = first_md; md != NULL; md = md_next) {
        md_next = md->next;
        av_free(md->buf);
        av_free(md);
    }
#endif
}


#ifndef EMSCRIPTEN
void bpg_decoder_keep_extension_data(BPGDecoderContext *s, int enable)
{
    s->keep_extension_data = enable;
}

BPGExtensionData *bpg_decoder_get_extension_data(BPGDecoderContext *s)
{
    return s->first_md;
}

int bpg_decoder_get_info_from_buf(BPGImageInfo *p, 
                                  BPGExtensionData **pfirst_md,
                                  const uint8_t *buf, int buf_len)
{
    BPGHeaderData h_s, *h = &h_s;
    int parse_extension;

    parse_extension = (pfirst_md != NULL);
    if (bpg_decode_header(h, buf, buf_len, 
                          !parse_extension, parse_extension) < 0)
        return -1;
    p->width = h->width;
    p->height = h->height;
    p->format = h->format;
    if (h->color_space == BPG_CS_YCbCrK || 
        h->color_space == BPG_CS_CMYK)
        p->has_alpha = 0;
    else
        p->has_alpha = h->has_alpha;
    p->color_space = h->color_space;
    p->bit_depth = h->bit_depth;
    if (pfirst_md)
        *pfirst_md = h->first_md;
    return 0;
}
#endif
