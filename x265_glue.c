/*
 * x265 encoder front-end  
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
#include <unistd.h>

#include "bpgenc.h"

#include "x265.h"

struct HEVCEncoderContext {
    const x265_api *api;
    x265_encoder *enc;
    x265_picture *pic;
    uint8_t *buf;
    int buf_len, buf_size;
};

static HEVCEncoderContext *x265_open(const HEVCEncodeParams *params)
{
    HEVCEncoderContext *s;
    x265_param *p;
    int preset_index;
    const char *preset;
    
    s = malloc(sizeof(HEVCEncoderContext));
    memset(s, 0, sizeof(*s));

    s->api = x265_api_get(params->bit_depth);
    if (!s->api) {
        fprintf(stderr, "x265 supports bit depths of 8, 10 or 12.\n");
        return NULL;
    }
#if 0
    /* Note: the x265 library included in libbpg supported gray encoding */
    if (params->chroma_format == BPG_FORMAT_GRAY) {
        fprintf(stderr, "x265 does not support monochrome (or alpha) data yet. Plase use the jctvc encoder.\n");
        return NULL;
    }
#endif
    
    p = s->api->param_alloc();

    preset_index = params->compress_level; /* 9 is placebo */

    preset = x265_preset_names[preset_index];
    if (params->verbose)
        printf("Using x265 preset: %s\n", preset);
    
    s->api->param_default_preset(p, preset, "ssim");

    p->bRepeatHeaders = 1;
    p->decodedPictureHashSEI = params->sei_decoded_picture_hash;
    p->sourceWidth = params->width;
    p->sourceHeight = params->height;
    switch(params->chroma_format) {
    case BPG_FORMAT_GRAY:
        p->internalCsp = X265_CSP_I400;
        break;
    case BPG_FORMAT_420:
        p->internalCsp = X265_CSP_I420;
        break;
    case BPG_FORMAT_422:
        p->internalCsp = X265_CSP_I422;
        break;
    case BPG_FORMAT_444:
        p->internalCsp = X265_CSP_I444;
        break;
    default:
        abort();
    }
    if (params->intra_only) {
        p->keyframeMax = 1; /* only I frames */
        p->totalFrames = 1;
    } else {
        p->keyframeMax = 250;
        p->totalFrames = 0;
        p->maxNumReferences = 1;
        p->bframes = 0;
    }
    p->bEnableRectInter = 1;
    p->bEnableAMP = 1; /* cannot use 0 due to header restriction */
    p->internalBitDepth = params->bit_depth;
    p->bEmitInfoSEI = 0;
    if (params->verbose)
        p->logLevel = X265_LOG_INFO;
    else
        p->logLevel = X265_LOG_NONE;
        
    /* dummy frame rate */
    p->fpsNum = 25;
    p->fpsDenom = 1;

    p->rc.rateControlMode = X265_RC_CQP;
    p->rc.qp = params->qp;
    p->bLossless = params->lossless;

    s->enc = s->api->encoder_open(p);

    s->pic = s->api->picture_alloc();
    s->api->picture_init(p, s->pic);

    s->pic->colorSpace = p->internalCsp;

    s->api->param_free(p);

    return s;
}

static void add_nal(HEVCEncoderContext *s, const uint8_t *data, int data_len)
{
    int new_size, size;

    size = s->buf_len + data_len;
    if (size > s->buf_size) {
        new_size = (s->buf_size * 3) / 2;
        if (new_size < size)
            new_size = size;
        s->buf = realloc(s->buf, new_size);
        s->buf_size = new_size;
    }
    memcpy(s->buf + s->buf_len, data, data_len);
    s->buf_len += data_len;
}

static int x265_encode(HEVCEncoderContext *s, Image *img)
{
    int c_count, i, ret;
    x265_picture *pic;
    uint32_t nal_count;
    x265_nal *p_nal;
    
    pic = s->pic;

    if (img->format == BPG_FORMAT_GRAY)
        c_count = 1;
    else
        c_count = 3;
    for(i = 0; i < c_count; i++) {
        pic->planes[i] = img->data[i];
        pic->stride[i] = img->linesize[i];
    }
    pic->bitDepth = img->bit_depth;

    ret = s->api->encoder_encode(s->enc, &p_nal, &nal_count, pic, NULL);
    if (ret > 0) {
        for(i = 0; i < nal_count; i++) {
            add_nal(s, p_nal[i].payload, p_nal[i].sizeBytes);
        }
    }
    return 0;
}

static int x265_close(HEVCEncoderContext *s, uint8_t **pbuf)
{
    int buf_len, ret, i;
    uint32_t nal_count;
    x265_nal *p_nal;
    
    /* get last compressed pictures */
    for(;;) {
        ret = s->api->encoder_encode(s->enc, &p_nal, &nal_count, NULL, NULL);
        if (ret <= 0)
            break;
        for(i = 0; i < nal_count; i++) {
            add_nal(s, p_nal[i].payload, p_nal[i].sizeBytes);
        }
    }

    if (s->buf_len < s->buf_size) {
        s->buf = realloc(s->buf, s->buf_len);
    }

    *pbuf = s->buf;
    buf_len = s->buf_len;

    s->api->encoder_close(s->enc);
    s->api->picture_free(s->pic);
    free(s);
    return buf_len;
}

HEVCEncoder x265_hevc_encoder = {
  .open = x265_open,
  .encode = x265_encode,
  .close = x265_close,
};
