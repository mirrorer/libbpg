#ifndef _BPG_LOAD_SAVE_LIB_H
#define _BPG_LOAD_SAVE_LIB_H

#include "libbpg.h"
#include "bpgenc.h"

#define DEFAULT_OUTFILENAME "out.bpg"
#define DEFAULT_QP 29
#define DEFAULT_LOSSLESS 0
#define DEFAULT_COMPRESS_LEVEL 8
#define DEFAULT_PREFFERED_CHROMA_FORMAT 444
#define DEFAULT_BIT_DEPTH 8


typedef struct DecodedImage
{
    int w, h, has_alpha, is_grayscale;
    int ** image_array;
} DecodedImage;

DecodedImage load_bpg_image(char *filename);

int save_bpg_image(DecodedImage *decoded_image, char *outfilename, int qp, 
                int lossless, int compress_level, int preffered_chroma_format);

int save_bpg_image_with_defaults(DecodedImage *decoded_image);


// structs from bpgenc.c :
typedef struct BPGMetaData {
    uint32_t tag;
    uint8_t *buf;
    int buf_len;
    struct BPGMetaData *next;
} BPGMetaData;

typedef enum {
#if defined(USE_X265)
    HEVC_ENCODER_X265,
#endif
#if defined(USE_JCTVC)
    HEVC_ENCODER_JCTVC,
#endif

    HEVC_ENCODER_COUNT,
} HEVCEncoderEnum;

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

#endif