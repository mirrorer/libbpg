#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <getopt.h>
#include <inttypes.h>

#include "bpg_load_save_lib.h"

int save_bpg_image_with_defaults(DecodedImage *decoded_image){
    save_bpg_image(decoded_image, DEFAULT_OUTFILENAME, DEFAULT_QP, 
        DEFAULT_LOSSLESS, DEFAULT_COMPRESS_LEVEL, DEFAULT_PREFFERED_CHROMA_FORMAT);
    
    return 0;
}

DecodedImage load_bpg_image(char *filename){
    DecodedImage decoded_image;
    FILE *f;
    BPGDecoderContext *img;
    uint8_t *buf;
    BPGImageInfo img_info_s, *img_info = &img_info_s;
    uint8_t *rgb_line;
    BPGDecoderOutputFormat out_fmt;
    int buf_len, bit_depth, c, show_info;
    const char *p;
    int * raw_data;
    int w, h, pixel_len, has_alpha, line_len, y;

    bit_depth = DEFAULT_BIT_DEPTH;

    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", filename);
        return decoded_image;
    }

    fseek(f, 0, SEEK_END);
    buf_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    buf = malloc(buf_len);
    if (fread(buf, 1, buf_len, f) != buf_len) {
        fprintf(stderr, "Error while reading file\n");
        return decoded_image;
    }
    
    fclose(f);

    img = bpg_decoder_open();

    if (bpg_decoder_decode(img, buf, buf_len) < 0) {
        fprintf(stderr, "Could not decode image\n");
        return decoded_image;
    }
    free(buf);
    
    bpg_decoder_get_info(img, img_info);
    w = img_info->width;
    h = img_info->height;
    has_alpha = img_info->has_alpha;

    pixel_len = 3;
    out_fmt = BPG_OUTPUT_FORMAT_RGB24;
    if (has_alpha){
        pixel_len = 4;
        out_fmt = BPG_OUTPUT_FORMAT_RGBA32;
    }

    line_len = pixel_len * w;
    raw_data = malloc(h * line_len * sizeof( int ));
    
    rgb_line = malloc(line_len);    

    bpg_decoder_start(img, out_fmt);
    for (y = 0; y < h; y++) {
        bpg_decoder_get_line(img, rgb_line);
        for(int i=0; i<pixel_len*w; i++){
            raw_data[y*line_len + i] = rgb_line[i];
        }
    }
    free(rgb_line);

    decoded_image.w = w;
    decoded_image.h = h;
    decoded_image.pixel_len = pixel_len;
    decoded_image.has_alpha = has_alpha;
    decoded_image.is_grayscale = 0;
    decoded_image.raw_data = raw_data;

    return decoded_image;
}