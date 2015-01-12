/*
 * BPG viewer
 *
 * Copyright (c) 2014-2015 Fabrice Bellard
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
#include <math.h>
#include <getopt.h>
#include <inttypes.h>
#ifdef WIN32
#include <windows.h>
#endif
#include <SDL/SDL.h>
#include <SDL/SDL_image.h>

#include "libbpg.h"

typedef enum {
    BG_BLACK,
    BG_TILED,
} BackgroundTypeEnum;

typedef struct {
    SDL_Surface *img;
    int delay; /* in ms */
} Frame;

typedef struct {
    int screen_w, screen_h;
    int win_w, win_h;
    SDL_Surface *screen;

    int img_w, img_h;
    int frame_count;
    Frame *frames;
    int frame_index; /* index of the current frame */
    int loop_counter;
    int loop_count;
    SDL_TimerID frame_timer_id;

    int is_full_screen;
    int pos_x, pos_y;
    BackgroundTypeEnum background_type;
} DispContext;

static uint32_t timer_cb(uint32_t interval, void *param);

static inline int clamp_int(int val, int min_val, int max_val)
{
    if (val < min_val)
        return min_val;
    else if (val > max_val)
        return max_val;
    else
        return val;
}

Frame *bpg_load(FILE *f, int *pframe_count, int *ploop_count)
{
    BPGDecoderContext *s;
    BPGImageInfo bi_s, *bi = &bi_s;
    uint8_t *buf;
    int len, y;
    SDL_Surface *img;
    Frame *frames;
    uint32_t rmask, gmask, bmask, amask;
    int frame_count, i, delay_num, delay_den;

    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0)
        return NULL;
    buf = malloc(len);
    if (!buf)
        return NULL;
    if (fread(buf, 1, len, f) != len)
        return NULL;
    
    frames = NULL;
    frame_count = 0;

    s = bpg_decoder_open();
    if (bpg_decoder_decode(s, buf, len) < 0) 
        goto fail;
    bpg_decoder_get_info(s, bi);
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    rmask = 0xff000000;
    gmask = 0x00ff0000;
    bmask = 0x0000ff00;
    amask = 0x000000ff;
#else
    rmask = 0x000000ff;
    gmask = 0x0000ff00;
    bmask = 0x00ff0000;
    amask = 0xff000000;
#endif
    for(;;) {
        if (bpg_decoder_start(s, BPG_OUTPUT_FORMAT_RGBA32) < 0)
            break;
        bpg_decoder_get_frame_duration(s, &delay_num, &delay_den);
        frames = realloc(frames, sizeof(frames[0]) * (frame_count + 1));
        img = SDL_CreateRGBSurface(SDL_HWSURFACE, bi->width, bi->height, 32,
                                   rmask, gmask, bmask, amask);
        if (!img) 
            goto fail;
    
        SDL_LockSurface(img);
        for(y = 0; y < bi->height; y++) {
            bpg_decoder_get_line(s, (uint8_t *)img->pixels + y * img->pitch);
        }
        SDL_UnlockSurface(img);
        frames[frame_count].img = img;
        frames[frame_count].delay = (delay_num * 1000) / delay_den;
        frame_count++;
    }
    bpg_decoder_close(s);
    *pframe_count = frame_count;
    *ploop_count = bi->loop_count;
    return frames;
 fail:
    bpg_decoder_close(s);
    for(i = 0; i < frame_count; i++) {
        SDL_FreeSurface(frames[i].img);
    }
    free(frames);
    *pframe_count = 0;
    return NULL;
}

static void restart_frame_timer(DispContext *dc)
{
    if (dc->frame_timer_id) {
        /* XXX: the SDL timer API is not safe, so we remove the timer even if it already expired */
        SDL_RemoveTimer(dc->frame_timer_id);
        dc->frame_timer_id = 0;
    }
    dc->frame_timer_id = 
        SDL_AddTimer(dc->frames[dc->frame_index].delay, timer_cb, NULL);
}

int load_image(DispContext *dc, const char *filename)
{
    SDL_Surface *img;
    Frame *frames;
    FILE *f;
    uint8_t buf[BPG_DECODER_INFO_BUF_SIZE];
    int len, i, frame_count, loop_count;
    BPGImageInfo bi;

    f = fopen(filename, "rb");
    if (!f)
        goto fail;
    len = fread(buf, 1, sizeof(buf), f);
    if (bpg_decoder_get_info_from_buf(&bi, NULL, buf, len) >= 0) {
        fseek(f, 0, SEEK_SET);
        frames = bpg_load(f, &frame_count, &loop_count);
        if (!frames)
            goto fail;
        fclose(f);
    } else {
        /* use SDL image loader */
        img = IMG_Load(filename);
        if (!img) {
        fail:
            fprintf(stderr, "Could not load '%s'\n", filename);
            return -1;
        }
        frame_count = 1;
        frames = malloc(sizeof(dc->frames[0]) * frame_count);
        frames[0].img = img;
        frames[0].delay = 0;
        loop_count = 1;
    }

    for(i = 0; i < dc->frame_count; i++) {
        SDL_FreeSurface(dc->frames[i].img);
    }
    free(dc->frames);
    if (dc->frame_timer_id) {
        SDL_RemoveTimer(dc->frame_timer_id);
        dc->frame_timer_id = 0;
    }
    
    dc->frame_count = frame_count;
    dc->frames = frames;
    dc->frame_index = 0;
    dc->loop_counter = 0;
    dc->loop_count = loop_count;
    dc->img_w = dc->frames[0].img->w;
    dc->img_h = dc->frames[0].img->h;

    /* start the animation timer if needed */
    if (dc->frame_count > 1) {
        restart_frame_timer(dc);
    }
    return 0;
}

void center_image(DispContext *dc)
{
    dc->pos_x = clamp_int((dc->screen->w - dc->img_w) / 2, -32767, 32768);
    dc->pos_y = clamp_int((dc->screen->h - dc->img_h) / 2, -32767, 32768);
}

void draw_image(DispContext *dc)
{
    SDL_Rect r;

    r.x = 0;
    r.y = 0;
    r.w = dc->screen->w;
    r.h = dc->screen->h;
    SDL_FillRect(dc->screen, &r, SDL_MapRGB(dc->screen->format, 0x00, 0x00, 0x00));

    if (dc->background_type == BG_TILED) {
        int x, y, tw, w, h, x2, y2, w1, h1, x1, y1;
        uint32_t bgcolors[2];

        tw = 16;
        w = dc->img_w;
        h = dc->img_h;
        w1 = (w + tw - 1) / tw;
        h1 = (h + tw - 1) / tw;
        bgcolors[0] = SDL_MapRGB(dc->screen->format, 100, 100, 100);
        bgcolors[1] = SDL_MapRGB(dc->screen->format, 150, 150, 150);
        for(y = 0; y < h1; y++) {
            for(x = 0; x < w1; x++) {
                x1 = x * tw;
                y1 = y * tw;
                x2 = x1 + tw;
                y2 = y1 + tw;
                if (x2 > w)
                    x2 = w;
                if (y2 > h)
                    y2 = h;
                r.x = x1 + dc->pos_x;
                r.y = y1 + dc->pos_y;
                r.w = x2 - x1;
                r.h = y2 - y1;
                SDL_FillRect(dc->screen, &r, bgcolors[(x ^ y) & 1]);
            }
        }
    }

    r.x = dc->pos_x;
    r.y = dc->pos_y;
    r.w = 0;
    r.h = 0;
    SDL_BlitSurface (dc->frames[dc->frame_index].img, NULL, dc->screen, &r);

    SDL_Flip(dc->screen);
}

void pan_image(DispContext *dc, int dx, int dy)
{
    int dw, dh;

    dw = dc->img_w - dc->screen->w;
    dh = dc->img_h - dc->screen->h;
    if (dw > 0) {
        dc->pos_x += dx;
        if (dc->pos_x < -dw)
            dc->pos_x = -dw;
        else if (dc->pos_x > 0)
            dc->pos_x = 0;
    }
    if (dh > 0) {
        dc->pos_y += dy;
        if (dc->pos_y < -dh)
            dc->pos_y = -dh;
        else if (dc->pos_y > 0)
            dc->pos_y = 0;
    }
    draw_image(dc);
}

static void set_caption(DispContext *dc, char **argv,
                        int image_index, int image_count)
{
    char buf[1024];
    const char *filename;
    filename = argv[image_index];
    snprintf(buf, sizeof(buf), "bpgview [%d of %d] - %s",
             image_index + 1, image_count, filename);
    SDL_WM_SetCaption(buf, buf);
}

static void open_window(DispContext *dc, int w, int h, int is_full_screen)
{
    int flags;

    flags = SDL_DOUBLEBUF | SDL_HWSURFACE | SDL_HWACCEL;
    if (is_full_screen)
        flags |= SDL_FULLSCREEN;
    else
        flags |= SDL_RESIZABLE;

    dc->screen = SDL_SetVideoMode(w, h, 32, flags);
    if (!dc->screen) {
        fprintf(stderr, "Could not init screen\n");
        exit(1);
    }
}

static uint32_t timer_cb(uint32_t interval, void *param)
{
    SDL_Event event;
    SDL_UserEvent userevent;

    userevent.type = SDL_USEREVENT;
    userevent.code = 0;
    userevent.data1 = NULL;
    userevent.data2 = NULL;

    event.type = SDL_USEREVENT;
    event.user = userevent;

    SDL_PushEvent(&event);
    return 0;
}

#define DEFAULT_W 640
#define DEFAULT_H 480

static void help(void)
{
    const char *str;
    str = "BPG Image Viewer version " CONFIG_BPG_VERSION "\n"
           "usage: bpgview infile...\n"
           "\n"
           "Keys:\n"
           "q, ESC         quit\n"
           "n, SPACE       next image\n"
           "p              previous image\n"
           "arrows         pan\n"
           "c              center\n"
           "b              toggle background type\n";
#ifdef WIN32
    MessageBox(NULL, str, "Error", MB_ICONERROR | MB_OK);
    exit(1);
#else
    printf("%s", str);
    exit(1);
#endif
}

int main(int argc, char **argv)
{
    int c, image_index, image_count, incr, i;
    SDL_Event event;
    DispContext dc_s, *dc = &dc_s;
    const SDL_VideoInfo *vi;

    for(;;) {
        c = getopt(argc, argv, "h");
        if (c == -1)
            break;
        switch(c) {
        case 'h':
        show_help:
            help();
            break;
        default:
            exit(1);
        }
    }

    if (optind >= argc)
        goto show_help;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "Could not init SDL\n");
        exit(1);
    }
    memset(dc, 0, sizeof(*dc));

    vi = SDL_GetVideoInfo();
    dc->screen_w = vi->current_w;
    dc->screen_h = vi->current_h;
    dc->is_full_screen = 0;

    image_count = argc - optind;
    image_index = 0;
    if (load_image(dc, argv[optind + image_index]) < 0)
        exit(1);
    dc->background_type = BG_TILED;

    {
        int w, h;

        if (image_count > 1 || (dc->img_w < 256 || dc->img_h < 256)) {
            w = DEFAULT_W;
            h = DEFAULT_H;
        } else {
            w = clamp_int(dc->img_w, 32, dc->screen_w);
            h = clamp_int(dc->img_h, 32, dc->screen_h);
        }
        open_window(dc, w, h, 0);
        set_caption(dc, argv + optind, image_index, image_count);
    }

    center_image(dc);
    draw_image(dc);

    SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

    for(;;) {
        if (!SDL_WaitEvent(&event))
            continue;
        switch(event.type) {
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
            case SDLK_ESCAPE:
            case SDLK_q:
                goto done;
            case SDLK_SPACE: /* next image */
            case SDLK_n:
                incr = 1;
                goto prev_next;
            case SDLK_p: /* previous image */
                incr = -1;
            prev_next:
                if (image_count > 1) {
                    for(i = 0; i < image_count; i++) {
                        image_index += incr;
                        if (image_index < 0)
                            image_index = image_count - 1;
                        else if (image_index >= image_count)
                            image_index = 0;
                        if (load_image(dc, argv[optind + image_index]) == 0)
                            break;
                    }
                    if (i == image_count)
                        exit(1);
                    set_caption(dc, argv + optind, image_index, image_count);
                    center_image(dc);
                    draw_image(dc);
                }
                break;
            case SDLK_LEFT:
                pan_image(dc, 32, 0);
                break;
            case SDLK_RIGHT:
                pan_image(dc, -32, 0);
                break;
            case SDLK_UP:
                pan_image(dc, 0, 32);
                break;
            case SDLK_DOWN:
                pan_image(dc, 0, -32);
                break;
            case SDLK_c:
                center_image(dc);
                draw_image(dc);
                break;
            case SDLK_b:
                dc->background_type ^= 1;
                draw_image(dc);
                break;
            case SDLK_f:
                dc->is_full_screen ^= 1;
                if (dc->is_full_screen) {
                    /* save old windows size */
                    dc->win_w = dc->screen->w;
                    dc->win_h = dc->screen->h;
                    open_window(dc, dc->screen_w, dc->screen_h, 1);
                } else {
                    open_window(dc, dc->win_w, dc->win_h, 0);
                }
                center_image(dc);
                draw_image(dc);
                break;
            default:
                break;
            }
            break;
        case SDL_VIDEORESIZE:
            {
                open_window(dc, event.resize.w, event.resize.h, 0);
                center_image(dc);
                draw_image(dc);
            }
            break;
        case SDL_QUIT:
            goto done;
        case SDL_MOUSEMOTION:
            if (event.motion.state) {
                pan_image(dc, event.motion.xrel, event.motion.yrel);
            }
            break;
        case SDL_USEREVENT:
            if (dc->frame_count > 1) {
                /* show next frame */
                if (dc->frame_index == (dc->frame_count - 1)) {
                    if (dc->loop_count == 0 ||
                        dc->loop_counter < (dc->loop_count - 1)) {
                        dc->frame_index = 0;
                        dc->loop_counter++;
                    } else {
                        break;
                    }
                } else {
                    dc->frame_index++;
                }
                draw_image(dc);
                restart_frame_timer(dc);
            }
            break;
        default:
            break;
        }
    }
 done: 

    SDL_FreeSurface(dc->screen);
    return 0;
}
