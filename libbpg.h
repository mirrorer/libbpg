/*
 * BPG decoder
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
#ifndef _LIBBPG_H
#define _LIBBPG_H

typedef struct BPGDecoderContext BPGDecoderContext;

typedef enum {
    BPG_FORMAT_GRAY,
    BPG_FORMAT_420,
    BPG_FORMAT_422,
    BPG_FORMAT_444,
} BPGImageFormatEnum;

typedef enum {
    BPG_CS_YCbCr,
    BPG_CS_RGB,
    BPG_CS_YCgCo,
    BPG_CS_YCbCrK,
    BPG_CS_CMYK,

    BPG_CS_COUNT,
} BPGColorSpaceEnum;

typedef struct {
    int width;
    int height;
    int format; /* see BPGImageFormatEnum */
    int has_alpha; /* TRUE if an alpha plane is present */
    int color_space; /* see BPGColorSpaceEnum */
    int bit_depth;
} BPGImageInfo;

typedef enum {
    BPG_EXTENSION_TAG_EXIF = 1,
} BPGImageExtensionTag;

typedef enum {
    BPG_OUTPUT_FORMAT_RGB24,
    BPG_OUTPUT_FORMAT_RGBA32,
    BPG_OUTPUT_FORMAT_RGB48,
    BPG_OUTPUT_FORMAT_RGBA64,
} BPGDecoderOutputFormat;

#define BPG_DECODER_INFO_BUF_SIZE 16

/* get information from the start of the image data in 'buf'. Return 0
   if OK, < 0 if unrecognized data. */
int bpg_decoder_get_info_from_buf(BPGImageInfo *p,
                                  const uint8_t *buf, int buf_len);

BPGDecoderContext *bpg_decoder_open(const uint8_t *buf, int buf_len);
void bpg_decoder_close(BPGDecoderContext *s);
int bpg_decoder_get_info(BPGDecoderContext *s, BPGImageInfo *p);
int bpg_decoder_start(BPGDecoderContext *s, BPGDecoderOutputFormat out_fmt);
int bpg_decoder_get_line(BPGDecoderContext *s, void *buf);

/* only useful for low level access to the image data */
uint8_t *bpg_decoder_get_data(BPGDecoderContext *s, int *pline_size, int plane);

#endif /* _LIBBPG_H */
