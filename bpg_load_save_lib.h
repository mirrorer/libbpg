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

// DecodedImage struct defined in bpgenc
// typedef struct DecodedImage
// {
//     int w, h, has_alpha, is_grayscale;
//     int ** image_array;
// } DecodedImage;

DecodedImage load_bpg_image(char *filename);

// save functions defined in bpgenc
// int save_bpg_image(DecodedImage *decoded_image, char *outfilename, int qp, 
//                 int lossless, int compress_level, int preffered_chroma_format);

// int save_bpg_image_with_defaults(DecodedImage *decoded_image);

#endif