#ifndef _DECODED_IMAGE_STRUCT_H
#define _DECODED_IMAGE_STRUCT_H

typedef struct DecodedImage
{
    int w, h, has_alpha, is_grayscale;
    int ** image_array;
} DecodedImage;

#endif