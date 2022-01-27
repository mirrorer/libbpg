#ifndef _BPG_LOAD_SAVE_LIB_H
#define _BPG_LOAD_SAVE_LIB_H

#include <inttypes.h>
#include "libbpg.h"

#define IMAGE_HEADER_MAGIC 0x425047fb

typedef enum {
    HEVC_ENCODER_JCTVC,
    HEVC_ENCODER_COUNT,
} HEVCEncoderEnum;

typedef struct BPGMetaData {
    uint32_t tag;
    uint8_t *buf;
    int buf_len;
    struct BPGMetaData *next;
} BPGMetaData;

typedef struct {
    int w, h;
    BPGImageFormatEnum format; /* x_VIDEO values are forbidden here */
    uint8_t c_h_phase; /* 4:2:2 or 4:2:0 : give the horizontal chroma
                          position. 0=MPEG2, 1=JPEG. */
    uint8_t has_alpha;
    uint8_t has_w_plane;
    uint8_t limited_range;
    uint8_t premultiplied_alpha;
    BPGColorSpaceEnum color_space;
    uint8_t bit_depth;
    uint8_t pixel_shift; /* (1 << pixel_shift) bytes per pixel */
    uint8_t *data[4];
    int linesize[4];
} Image;

typedef struct {
    int width;
    int height;
    int chroma_format; /* 0-3 */
    int bit_depth; /* 8-14 */
    int intra_only; /* 0-1 */

    int qp; /* quantizer 0-51 */
    int lossless; /* 0-1 lossless mode */
    int sei_decoded_picture_hash; /* 0=no hash, 1=MD5 hash */
    int compress_level; /* 1-9 */
    int verbose;
} HEVCEncodeParams;

typedef struct BPGEncoderContext BPGEncoderContext;

typedef struct {
    HEVCEncoderContext *(*open)(const HEVCEncodeParams *params);
    int (*encode)(HEVCEncoderContext *s, Image *img);
    int (*close)(HEVCEncoderContext *s, uint8_t **pbuf);
} HEVCEncoder;

extern HEVCEncoder jctvc_encoder;

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

static char *hevc_encoder_name[HEVC_ENCODER_COUNT] = {
    "jctvc",
};

static HEVCEncoder *hevc_encoder_tab[HEVC_ENCODER_COUNT] = {
    &jctvc_encoder,
};

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


#endif