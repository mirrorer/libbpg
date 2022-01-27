#include <stdlib.h>
#include <stdio.h>

#include "bpgenc_lite.h"

static int file_write_func(void *opaque, const uint8_t *buf, int buf_len)
{
    FILE *f = opaque;
    return fwrite(buf, 1, buf_len, f);
}


/* Warning: currently 'img' is modified. When encoding animations, img
   = NULL indicates the end of the stream. */
int bpg_encoder_encode(BPGEncoderContext *s, Image *img,
                       BPGEncoderWriteFunc *write_func,
                       void *opaque)
{
    const BPGEncoderParameters *p = &s->params;
    Image *img_alpha;
    HEVCEncodeParams ep_s, *ep = &ep_s;
    uint8_t *extension_buf;
    int extension_buf_len;
    int cb_size, width, height;

    if (p->animated && !img) {
        return bpg_encoder_encode_trailer(s, write_func, opaque);
    }

    /* extract the alpha plane */
    if (img->has_alpha) {
        int c_idx;

        img_alpha = malloc(sizeof(Image));
        memset(img_alpha, 0, sizeof(*img_alpha));
        if (img->format == BPG_FORMAT_GRAY)
            c_idx = 1;
        else
            c_idx = 3;

        img_alpha->w = img->w;
        img_alpha->h = img->h;
        img_alpha->format = BPG_FORMAT_GRAY;
        img_alpha->has_alpha = 0;
        img_alpha->color_space = BPG_CS_YCbCr;
        img_alpha->bit_depth = img->bit_depth;
        img_alpha->pixel_shift = img->pixel_shift;
        img_alpha->data[0] = img->data[c_idx];
        img_alpha->linesize[0] = img->linesize[c_idx];
        
        img->data[c_idx] = NULL;
        img->has_alpha = 0;
    } else {
        img_alpha = NULL;
    }

    if (img->format == BPG_FORMAT_444 && img->color_space != BPG_CS_RGB) {
        if (p->preferred_chroma_format == BPG_FORMAT_420 ||
            p->preferred_chroma_format == BPG_FORMAT_420_VIDEO) {
            int c_h_phase = (p->preferred_chroma_format == BPG_FORMAT_420);
            if (image_ycc444_to_ycc420(img, c_h_phase) != 0)
                goto error_convert;
        } else if (p->preferred_chroma_format == BPG_FORMAT_422 ||
                   p->preferred_chroma_format == BPG_FORMAT_422_VIDEO) {
            int c_h_phase = (p->preferred_chroma_format == BPG_FORMAT_422);
            if (image_ycc444_to_ycc422(img, c_h_phase) != 0)  {
            error_convert:
                fprintf(stderr, "Cannot convert image\n");
                exit(1);
            }
        }
    }

    cb_size = 8; /* XXX: should make it configurable. We assume the
                    HEVC encoder uses the same value */
    width = img->w;
    height = img->h;
    image_pad(img, cb_size);
    if (img_alpha)
        image_pad(img_alpha, cb_size);

    /* convert to the allocated pixel width to 8 bit if needed by the
       HEVC encoder */
    if (img->bit_depth == 8) {
        image_convert16to8(img);
        if (img_alpha)
            image_convert16to8(img_alpha);
    }
        
    if (s->frame_count == 0) {
        memset(ep, 0, sizeof(*ep));
        ep->qp = p->qp;
        ep->width = img->w;
        ep->height = img->h;
        ep->chroma_format = img->format;
        ep->bit_depth = img->bit_depth;
        ep->intra_only = !p->animated;
        ep->lossless = p->lossless;
        ep->sei_decoded_picture_hash = p->sei_decoded_picture_hash;
        ep->compress_level = p->compress_level;
        ep->verbose = p->verbose;

        s->enc_ctx = s->encoder->open(ep);
        if (!s->enc_ctx) {
            fprintf(stderr, "Error while opening encoder\n");
            exit(1);
        }

        if (img_alpha) {
            if (p->alpha_qp < 0)
                ep->qp = p->qp;
            else
                ep->qp = p->alpha_qp;
            ep->chroma_format = 0;
            
            s->alpha_enc_ctx = s->encoder->open(ep);
            if (!s->alpha_enc_ctx) {
                fprintf(stderr, "Error while opening alpha encoder\n");
                exit(1);
            }
        }

        /* prepare the extension data */
        if (p->animated) {
            BPGMetaData *md;
            uint8_t buf[15], *q;

            md = bpg_md_alloc(BPG_EXTENSION_TAG_ANIM_CONTROL);
            q = buf;
            put_ue(&q, p->loop_count);
            put_ue(&q, p->frame_delay_num);
            put_ue(&q, p->frame_delay_den);
            md->buf_len = q - buf;
            md->buf = malloc(md->buf_len);
            memcpy(md->buf, buf, md->buf_len);
            md->next = s->first_md;
            s->first_md = md;
        }

        extension_buf = NULL;
        extension_buf_len = 0;
        if (s->first_md) {
            BPGMetaData *md1;
            int max_len;
            uint8_t *q;
            
            max_len = 0;
            for(md1 = s->first_md; md1 != NULL; md1 = md1->next) {
                max_len += md1->buf_len + 5 * 2;
            }
            extension_buf = malloc(max_len);
            q = extension_buf;
            for(md1 = s->first_md; md1 != NULL; md1 = md1->next) {
                put_ue(&q, md1->tag);
                put_ue(&q, md1->buf_len);
                memcpy(q, md1->buf, md1->buf_len);
                q += md1->buf_len;
            }
            extension_buf_len = q - extension_buf;
            
            bpg_md_free(s->first_md);
            s->first_md = NULL;
        }
    
        {
            uint8_t img_header[128], *q;
            int v, has_alpha, has_extension, alpha2_flag, alpha1_flag, format;
            
            has_alpha = (img_alpha != NULL);
            has_extension = (extension_buf_len > 0);
            
            
            if (has_alpha) {
                if (img->has_w_plane) {
                    alpha1_flag = 0;
                    alpha2_flag = 1;
                } else {
                    alpha1_flag = 1;
                    alpha2_flag = img->premultiplied_alpha;
                }
            } else {
                alpha1_flag = 0;
                alpha2_flag = 0;
            }
            
            q = img_header;
            *q++ = (IMAGE_HEADER_MAGIC >> 24) & 0xff;
            *q++ = (IMAGE_HEADER_MAGIC >> 16) & 0xff;
            *q++ = (IMAGE_HEADER_MAGIC >> 8) & 0xff;
            *q++ = (IMAGE_HEADER_MAGIC >> 0) & 0xff;

            if (img->c_h_phase == 0 && img->format == BPG_FORMAT_420)
                format = BPG_FORMAT_420_VIDEO;
            else if (img->c_h_phase == 0 && img->format == BPG_FORMAT_422)
                format = BPG_FORMAT_422_VIDEO;
            else
                format = img->format;
            v = (format << 5) | (alpha1_flag << 4) | (img->bit_depth - 8);
            *q++ = v;
            v = (img->color_space << 4) | (has_extension << 3) |
                (alpha2_flag << 2) | (img->limited_range << 1) |
                p->animated;
            *q++ = v;
            put_ue(&q, width);
            put_ue(&q, height);
            
            put_ue(&q, 0); /* zero length means up to the end of the file */
            if (has_extension) {
                put_ue(&q, extension_buf_len); /* extension data length */
            }
            
            write_func(opaque, img_header, q - img_header);
            
            if (has_extension) {
                if (write_func(opaque, extension_buf, extension_buf_len) != extension_buf_len) {
                    fprintf(stderr, "Error while writing extension data\n");
                    exit(1);
                }
                free(extension_buf);
            }
        }
    }

    /* store the frame duration */
    if ((s->frame_count + 1) > s->frame_duration_tab_size) {
        s->frame_duration_tab_size = (s->frame_duration_tab_size * 3) / 2;
        if (s->frame_duration_tab_size < (s->frame_count + 1))
            s->frame_duration_tab_size = (s->frame_count + 1);
        s->frame_duration_tab = realloc(s->frame_duration_tab, 
                                        sizeof(s->frame_duration_tab) * s->frame_duration_tab_size);
    }
    s->frame_duration_tab[s->frame_count] = s->frame_ticks;

    s->encoder->encode(s->enc_ctx, img);
    
    if (img_alpha) {
        s->encoder->encode(s->alpha_enc_ctx, img_alpha);
        image_free(img_alpha);
    }
    
    s->frame_count++;

    if (!p->animated)
        bpg_encoder_encode_trailer(s, write_func, opaque);

    return 0;
}

void bpg_encoder_close(BPGEncoderContext *s)
{
    free(s->frame_duration_tab);
    bpg_md_free(s->first_md);
    free(s);
}