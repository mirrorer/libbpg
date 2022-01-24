#include <stdlib.h>
#include <stdio.h>

#include "bpg_load_save_lib.h"


static int write_func(void *opaque, const uint8_t *buf, int buf_len)
{
    FILE *f = opaque;
    return fwrite(buf, 1, buf_len, f);
}

int save_bpg_image(DecodedImage *decoded_image, char *outfilename, int qp, 
                int lossless, int compress_level, int preferred_chroma_format){  
    if (qp < 0 || qp > 51){
        fprintf(stderr, "qp must be between 0 and 51\n");
        exit(1);
    }
    if (lossless != 0 && lossless != 1){
        fprintf(stderr, "field lossless must be 0 or 1\n");
        exit(1);
    }
    if (compress_level < 1 || compress_level > 9){
        fprintf(stderr, "compress_level must be between 1 and 9\n");
        exit(1);
    }
    if (preferred_chroma_format != 420 && preferred_chroma_format != 422 && preferred_chroma_format != 444){
        fprintf(stderr, "preferred_chroma_format must be 420, 422 or 444\n");
        exit(1);
    }

    Image *img;
    FILE *f;
    int bit_depth, limited_range, premultiplied_alpha;
    BPGEncoderContext *enc_ctx;
    BPGEncoderParameters *p;

    // fixed settings:
    bit_depth = DEFAULT_BIT_DEPTH;

    p = bpg_encoder_param_alloc();
    p->qp = qp;
    p->compress_level = compress_level;

    if(lossless = 1){
        p->lossless = 1;
        p->preferred_chroma_format = BPG_FORMAT_444;
    }
    

    switch (preferred_chroma_format)
    {
    case 420:
        p->preferred_chroma_format = BPG_FORMAT_420;
        break;
    case 422:
        p->preferred_chroma_format = BPG_FORMAT_422;
        break;
    default:
        p->preferred_chroma_format = BPG_FORMAT_444;
        break;
    }

    p->qp; //0-51
    p->lossless; // wtedy qp jest ignorowane, recznie dodac preferred_chroma na 444
    p->compress_level; //1-9
    p->preferred_chroma_format; //444 422 420
    
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

    img = read_image_array(0, decoded_image);
    if (!img) {
        fprintf(stderr, "Could not read image'\n");
        exit(1);
    }
    bpg_encoder_encode(enc_ctx, img, write_func, f);
    image_free(img);

    fclose(f);
    bpg_encoder_close(enc_ctx);
    bpg_encoder_param_free(p);

    return 0;
}

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
    int ** image_array;
    int w, h, y, has_alpha;

    bit_depth = DEFAULT_BIT_DEPTH;

    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }

    fseek(f, 0, SEEK_END);
    buf_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    buf = malloc(buf_len);
    if (fread(buf, 1, buf_len, f) != buf_len) {
        fprintf(stderr, "Error while reading file\n");
        exit(1);
    }
    
    fclose(f);

    img = bpg_decoder_open();

    if (bpg_decoder_decode(img, buf, buf_len) < 0) {
        fprintf(stderr, "Could not decode image\n");
        exit(1);
    }
    free(buf);
    
    bpg_decoder_get_info(img, img_info);
    w = img_info->width;
    h = img_info->height;
    has_alpha = img_info->has_alpha;

    int pixel_len = 3;
    out_fmt = BPG_OUTPUT_FORMAT_RGB24;
    if (has_alpha){
        pixel_len = 4;
        out_fmt = BPG_OUTPUT_FORMAT_RGBA32;
    }

    image_array = malloc(h * sizeof( int * ));
    for (y = 0; y < h; y++) {
        image_array[y] = malloc(pixel_len * w * sizeof( int ));
    }
    
    rgb_line = malloc(pixel_len * w);    

    bpg_decoder_start(img, out_fmt);
    for (y = 0; y < h; y++) {
        bpg_decoder_get_line(img, rgb_line);
        for(int i=0; i<pixel_len*w; i++){
            image_array[y][i] = rgb_line[i];
        }
    }
    free(rgb_line);

    decoded_image.w = w;
    decoded_image.h = h;
    decoded_image.has_alpha = has_alpha;
    decoded_image.is_grayscale = 0;
    decoded_image.image_array = image_array;

    return decoded_image;
}