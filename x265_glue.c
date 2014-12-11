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

int x265_encode_picture(uint8_t **pbuf, Image *img, 
                        const HEVCEncodeParams *params)
{
    x265_encoder *enc;
    x265_param *p;
    x265_picture *pic, *pic_in;
    x265_nal *p_nal;
    int buf_len, idx, c_count, i, ret, pic_count;
    uint32_t nal_count;
    uint8_t *buf;
    int preset_index;
    const char *preset;

    if (img->bit_depth != x265_max_bit_depth) {
        fprintf(stderr, "x265 is compiled to support only %d bit depth. Use the '-b %d' option to force the bit depth.\n",
                x265_max_bit_depth, x265_max_bit_depth);
        return -1;
    }
    if (img->format == BPG_FORMAT_GRAY) {
        fprintf(stderr, "x265 does not support monochrome (or alpha) data yet. Plase use the jctvc encoder.\n");
        return -1;
    }

    p = x265_param_alloc();

    preset_index = params->compress_level; /* 9 is placebo */

    preset = x265_preset_names[preset_index];
    if (params->verbose)
        printf("Using x265 preset: %s\n", preset);
    
    x265_param_default_preset(p, preset, "ssim");

    p->bRepeatHeaders = 1;
    p->decodedPictureHashSEI = params->sei_decoded_picture_hash;
    p->sourceWidth = img->w;
    p->sourceHeight = img->h;
    switch(img->format) {
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
    p->keyframeMax = 1; /* only I frames */
    p->internalBitDepth = img->bit_depth;
    p->bEmitInfoSEI = 0;
    if (params->verbose)
        p->logLevel = X265_LOG_INFO;
    else
        p->logLevel = X265_LOG_NONE;
        
    /* dummy frame rate */
    p->fpsNum = 25;
    p->fpsDenom = 1;
    p->totalFrames = 1;

    p->rc.rateControlMode = X265_RC_CQP;
    /* XXX: why do we need this offset to match the JCTVC quality ? */
    if (img->bit_depth == 10)
        p->rc.qp = params->qp + 7;
    else
        p->rc.qp = params->qp + 1;
    p->bLossless = params->lossless;

    enc = x265_encoder_open(p);

    pic = x265_picture_alloc();
    x265_picture_init(p, pic);

    if (img->format == BPG_FORMAT_GRAY)
        c_count = 1;
    else
        c_count = 3;
    for(i = 0; i < c_count; i++) {
        pic->planes[i] = img->data[i];
        pic->stride[i] = img->linesize[i];
    }
    pic->bitDepth = img->bit_depth;
    pic->colorSpace = p->internalCsp;

    pic_count = 0;
    for(;;) {
        if (pic_count == 0)
            pic_in = pic;
        else
            pic_in = NULL;
        ret = x265_encoder_encode(enc, &p_nal, &nal_count, pic_in, NULL);
        if (ret < 0)
            goto fail;
        if (ret == 1)
            break;
        pic_count++;
    }

    buf_len = 0;
    for(i = 0; i < nal_count; i++) {
        buf_len += p_nal[i].sizeBytes;
    }
    //    printf("nal_count=%d buf_len=%d\n", nal_count, buf_len);
    
    buf = malloc(buf_len);
    idx = 0;
    for(i = 0; i < nal_count; i++) {
        memcpy(buf + idx, p_nal[i].payload, p_nal[i].sizeBytes);
        idx += p_nal[i].sizeBytes;
    }
    
    x265_encoder_close(enc);

    x265_param_free(p);
    
    x265_picture_free(pic);

    *pbuf = buf;
    return buf_len;
 fail:
    x265_encoder_close(enc);
    
    x265_param_free(p);
    x265_picture_free(pic);
    *pbuf = NULL;
    return -1;
}
