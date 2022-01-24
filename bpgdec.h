#ifndef _BPGDEC_H
#define _BPGDEC_H

#include "libbpg.h"

typedef struct DecodedImage
{
    int w, h, has_alpha, is_grayscale;
    int ** image_array;
} DecodedImage;

DecodedImage get_array(char *filename);

#endif