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

#define IC0 40
#define IC1 (-11)
#define IC2 4
#define IC3 (-1)

/* interpolate by a factor of two */
static void interp2_simple(PIXEL *dst, const PIXEL *src, int n, int bit_depth)
{
    int pixel_max;

    pixel_max = (1 << bit_depth) - 1;
    while (n >= 2) {
        dst[0] = src[0];
        dst[1] = clamp_pix(((src[-3] + src[4]) * IC3 + 
                            (src[-2] + src[3]) * IC2 + 
                            (src[-1] + src[2]) * IC1 + 
                            (src[0] + src[1]) * IC0 + 32) >> 6, pixel_max);
        dst += 2;
        src++;
        n -= 2;
    }
    if (n) {
        dst[0] = src[0];
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
    int shift1, offset1, shift0, offset0, pixel_max;

    pixel_max = (1 << bit_depth) - 1;
    shift0 = 14 - bit_depth;
    offset0 = (1 << shift0) >> 1;
    shift1 = 20 - bit_depth;
    offset1 = 1 << (shift1 - 1);

    while (n >= 2) {
        dst[0] = clamp_pix((src[0] + offset0) >> shift0, pixel_max);
        dst[1] = clamp_pix(((src[-3] + src[4]) * IC3 + 
                            (src[-2] + src[3]) * IC2 + 
                            (src[-1] + src[2]) * IC1 + 
                            (src[0] + src[1]) * IC0 + offset1) >> shift1,
                           pixel_max);
        dst += 2;
        src++;
        n -= 2;
    }
    if (n) {
        dst[0] = clamp_pix((src[0] + offset0) >> shift0, pixel_max);
    }
}

/* y_pos is the position of the sample '0' in the 'src' circular
   buffer. tmp is a temporary buffer of length (n2 + 2 * ITAPS - 1) */
static void interp2_vh(PIXEL *dst, PIXEL **src, int n, int y_pos,
                       int16_t *tmp_buf, int bit_depth)
{
    const PIXEL *src0, *src1, *src2, *src3, *src4, *src5, *src6, *src7;
    int i, n2, shift;
    PIXEL v;

    src0 = src[(y_pos - 3) & 7]; 
    src1 = src[(y_pos - 2) & 7]; 
    src2 = src[(y_pos - 1) & 7]; 
    src3 = src[(y_pos + 0) & 7]; 
    src4 = src[(y_pos + 1) & 7]; 
    src5 = src[(y_pos + 2) & 7]; 
    src6 = src[(y_pos + 3) & 7]; 
    src7 = src[(y_pos + 4) & 7]; 

    /* vertical interpolation first */
    /* XXX: should round */
    shift = bit_depth - 8;
    n2 = (n + 1) / 2;
    for(i = 0; i < n2; i++) {
        tmp_buf[ITAPS2 - 1 + i] = 
            ((src0[i] + src7[i]) * IC3 + 
             (src1[i] + src6[i]) * IC2 + 
             (src2[i] + src5[i]) * IC1 + 
             (src3[i] + src4[i]) * IC0) >> shift;
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
            interp2_h(s->cb_buf2, s->cb_buf3[pos], w, s->bit_depth);
            interp2_h(s->cr_buf2, s->cr_buf3[pos], w, s->bit_depth);
        } else {
            interp2_vh(s->cb_buf2, s->cb_buf3, w, pos, s->c_buf4,
                       s->bit_depth);
            interp2_vh(s->cr_buf2, s->cr_buf3, w, pos, s->c_buf4,
                       s->bit_depth);

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

    if (s->y >= 0)
        return -1;
    ret = bpg_decoder_output_init(s, out_fmt);
    if (ret)
        return ret;
    s->y = 0;
    return 0;
}

BPGDecoderContext *bpg_decoder_open(const uint8_t *buf, int buf_len)
{
    int idx, ret, has_alpha, format, bit_depth, chroma_format_idc, color_space;
    uint32_t width, height, flags1, flags2, ycc_data_len, alpha_data_len;
    uint32_t extension_data_len;
    int has_extension;
    BPGDecoderContext *img;
    
    if (buf_len < 6)
        return NULL;
    /* check magic */
    if (buf[0] != ((BPG_HEADER_MAGIC >> 24) & 0xff) ||
        buf[1] != ((BPG_HEADER_MAGIC >> 16) & 0xff) ||
        buf[2] != ((BPG_HEADER_MAGIC >> 8) & 0xff) ||
        buf[3] != ((BPG_HEADER_MAGIC >> 0) & 0xff))
        return NULL;
    idx = 4;
    flags1 = buf[idx++];
    format = flags1 >> 5;
    if (format > 3)
        return NULL;
    has_alpha = (flags1 >> 4) & 1;
    bit_depth = (flags1 & 0xf) + 8;
    if (bit_depth > 14)
        return NULL;
    flags2 = buf[idx++];
    color_space = (flags2 >> 4) & 0xf;
    has_extension = (flags2 >> 3) & 1;
    if (color_space >= BPG_CS_COUNT || 
        (format == BPG_FORMAT_GRAY && color_space != 0) ||
        ((color_space == BPG_CS_YCbCrK || color_space == BPG_CS_CMYK) &&
         !has_alpha))
        return NULL;
    ret = get_ue(&width, buf + idx, buf_len - idx);
    if (ret < 0)
        return NULL;
    idx += ret;
    ret = get_ue(&height, buf + idx, buf_len - idx);
    if (ret < 0)
        return NULL;
    idx += ret;

    ret = get_ue(&ycc_data_len, buf + idx, buf_len - idx);
    if (ret < 0)
        return NULL;
    idx += ret;
           
    extension_data_len = 0;
    if (has_extension) {
        ret = get_ue(&extension_data_len, buf + idx, buf_len - idx);
        if (ret < 0)
            return NULL;
        idx += ret;
    }

    alpha_data_len = 0;
    if (has_alpha) {
        ret = get_ue(&alpha_data_len, buf + idx, buf_len - idx);
        if (ret < 0)
            return NULL;
        idx += ret;
    }


    img = av_mallocz(sizeof(BPGDecoderContext));
    if (!img)
        return NULL;
    img->w = width;
    img->h = height;
    img->format = format;
    img->has_alpha = has_alpha;
    img->color_space = color_space;
    img->bit_depth = bit_depth;

    if (has_extension) {
        if (idx + extension_data_len > buf_len)
            goto fail;
        idx += extension_data_len;
    }

    if (idx + ycc_data_len > buf_len)
        goto fail;
    chroma_format_idc = format;
    img->frame = hevc_decode(buf + idx, ycc_data_len,
                             width, height, chroma_format_idc, bit_depth);
    if (!img->frame)
        goto fail;
    idx += ycc_data_len;

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
        if (idx + alpha_data_len > buf_len)
            goto fail;
        img->alpha_frame = hevc_decode(buf + idx, alpha_data_len,
                                       width, height, 0, bit_depth);
        if (!img->alpha_frame)
            goto fail;
        idx += alpha_data_len;
    }

    img->y = -1;
    return img;

 fail:
    bpg_decoder_close(img);
    return NULL;
}

void bpg_decoder_close(BPGDecoderContext *s)
{
    bpg_decoder_output_end(s);
    if (s->frame)
        av_frame_free(&s->frame);
    if (s->alpha_frame)
        av_frame_free(&s->alpha_frame);
    av_free(s);
}

#ifndef EMSCRIPTEN
int bpg_decoder_get_info_from_buf(BPGImageInfo *p,
                                  const uint8_t *buf, int buf_len)
{
    int flags1, flags2, idx, format, color_space, has_alpha, bit_depth;
    int ret, width, height;

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
    format = flags1 >> 5;
    if (format > 3)
        return -1;
    has_alpha = (flags1 >> 4) & 1;
    bit_depth = (flags1 & 0xf) + 8;
    if (bit_depth > 14)
        return -1;
    flags2 = buf[idx++];
    color_space = (flags2 >> 4) & 0xf;
    if (color_space >= BPG_CS_COUNT || 
        (format == BPG_FORMAT_GRAY && color_space != 0) ||
        ((color_space == BPG_CS_YCbCrK || color_space == BPG_CS_CMYK) &&
         !has_alpha))
        return -1;
    ret = get_ue(&width, buf + idx, buf_len - idx);
    if (ret < 0)
        return -1;
    idx += ret;
    ret = get_ue(&height, buf + idx, buf_len - idx);
    if (ret < 0)
        return -1;
    p->width = width;
    p->height = height;
    p->format = format;
    if (color_space == BPG_CS_YCbCrK || 
        color_space == BPG_CS_CMYK)
        p->has_alpha = 0;
    else
        p->has_alpha = has_alpha;
    p->color_space = color_space;
    p->bit_depth = bit_depth;
    return 0;
}
#endif

