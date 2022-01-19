#ifdef WIN32
#include <windows.h>
#endif
#include <unistd.h>
#include <iostream>
#include "TAppEncTop.h"
#include "TLibCommon/Debug.h"
#include "TLibEncoder/TEncAnalyze.h"

#include "bpgenc.h"

struct HEVCEncoderContext {
    HEVCEncodeParams params;
    char infilename[1024];
    char outfilename[1024];
    FILE *yuv_file;
    int frame_count;
};

#define ARGV_MAX 256

static void add_opt(int *pargc, char **argv,
                    const char *str)
{
    int argc;
    argc = *pargc;
    if (argc >= ARGV_MAX)
        abort();
    argv[argc++] = strdup(str);
    *pargc = argc;
}

static HEVCEncoderContext *jctvc_open(const HEVCEncodeParams *params)
{
    HEVCEncoderContext *s;
    char buf[1024];
    static int tmp_idx = 1;

    s = (HEVCEncoderContext *)malloc(sizeof(HEVCEncoderContext));
    memset(s, 0, sizeof(*s));

    s->params = *params;
#ifdef WIN32
    if (GetTempPath(sizeof(buf), buf) > sizeof(buf) - 1) {
        fprintf(stderr, "Temporary path too long\n");
        free(s);
        return NULL;
    }
#else
    strcpy(buf, "/tmp/");
#endif
    snprintf(s->infilename, sizeof(s->infilename), "%sout%d-%d.yuv", buf, getpid(), tmp_idx);
    snprintf(s->outfilename, sizeof(s->outfilename), "%sout%d-%d.bin", buf, getpid(), tmp_idx);
    tmp_idx++;

    s->yuv_file = fopen(s->infilename, "wb");
    if (!s->yuv_file) {
        fprintf(stderr, "Could not open '%s'\n", s->infilename);
        free(s);
        return NULL;
    }
    return s;
}

static int jctvc_encode(HEVCEncoderContext *s, Image *img)
{
    save_yuv1(img, s->yuv_file);
    s->frame_count++;
    return 0;
}

/* return the encoded data in *pbuf and the size. Return < 0 if error */
static int jctvc_close(HEVCEncoderContext *s, uint8_t **pbuf)
{
    TAppEncTop cTAppEncTop;
    int argc;
    char *argv[ARGV_MAX + 1];
    char buf[1024];
    const char *str;
    FILE *f;
    uint8_t *out_buf;
    int out_buf_len, i;
    
    fclose(s->yuv_file);
    s->yuv_file = NULL;

    m_gcAnalyzeAll.clear();
    m_gcAnalyzeI.clear();
    m_gcAnalyzeP.clear();
    m_gcAnalyzeB.clear();
    m_gcAnalyzeAll_in.clear();
    
    cTAppEncTop.create();

    argc = 0;
    add_opt(&argc, argv, "jctvc"); /* dummy executable name */

    snprintf(buf, sizeof(buf),"--InputFile=%s", s->infilename);
    add_opt(&argc, argv, buf);
    snprintf(buf, sizeof(buf),"--BitstreamFile=%s", s->outfilename);
    add_opt(&argc, argv, buf);

    snprintf(buf, sizeof(buf),"--SourceWidth=%d", s->params.width);
    add_opt(&argc, argv, buf);
    snprintf(buf, sizeof(buf),"--SourceHeight=%d", s->params.height);
    add_opt(&argc, argv, buf);
    snprintf(buf, sizeof(buf),"--InputBitDepth=%d", s->params.bit_depth);
    add_opt(&argc, argv, buf);

    switch(s->params.chroma_format) {
    case BPG_FORMAT_GRAY:
        str = "400";
        break;
    case BPG_FORMAT_420:
        str = "420";
        break;
    case BPG_FORMAT_422:
        str = "422";
        break;
    case BPG_FORMAT_444:
        str = "444";
        break;
    default:
        abort();
    }
    snprintf(buf, sizeof(buf),"--InputChromaFormat=%s", str);
    add_opt(&argc, argv, buf);

    snprintf(buf, sizeof(buf),"--QP=%d", s->params.qp);
    add_opt(&argc, argv, buf);
    
    snprintf(buf, sizeof(buf),"--SEIDecodedPictureHash=%d", 
             s->params.sei_decoded_picture_hash);
    add_opt(&argc, argv, buf);
    
    if (!s->params.verbose)
      add_opt(&argc, argv, "--Verbose=0");
      
    /* single frame */
    snprintf(buf, sizeof(buf),"--FramesToBeEncoded=%d", s->frame_count);
    add_opt(&argc, argv, buf);
    
    /* no padding necessary (it is done before) */
    add_opt(&argc, argv, "--ConformanceWindowMode=0");
    
    /* dummy frame rate */
    add_opt(&argc, argv, "--FrameRate=25");

    /* general config */
    add_opt(&argc, argv, "--QuadtreeTULog2MaxSize=5");
    if (s->params.compress_level == 9) {
        add_opt(&argc, argv, "--QuadtreeTUMaxDepthIntra=4");
        add_opt(&argc, argv, "--QuadtreeTUMaxDepthInter=4");
    } else {
        add_opt(&argc, argv, "--QuadtreeTUMaxDepthIntra=3");
        add_opt(&argc, argv, "--QuadtreeTUMaxDepthInter=3");
    }

    if (s->params.intra_only) {
        add_opt(&argc, argv, "--Profile=main_444_16_intra");

        add_opt(&argc, argv, "--IntraPeriod=1");
        add_opt(&argc, argv, "--GOPSize=1");
    } else {
        int gop_size;

        add_opt(&argc, argv, "--Profile=main_444_16");
        add_opt(&argc, argv, "--IntraPeriod=250");
        gop_size = 1;
        snprintf(buf, sizeof(buf), "--GOPSize=%d", gop_size);
        add_opt(&argc, argv, buf);

        for(i = 0; i < gop_size; i++) {
            snprintf(buf, sizeof(buf), "--Frame%d=P 1 3 0.4624 0 0 0 1 1 -1 0", i + 1);
            add_opt(&argc, argv, buf);
        }
    }
    add_opt(&argc, argv, "--TransformSkip=1");
    add_opt(&argc, argv, "--TransformSkipFast=1");

    /* Note: Format Range extension */
    if (s->params.chroma_format == BPG_FORMAT_444) {
        add_opt(&argc, argv, "--CrossComponentPrediction=1");
    }

    if (s->params.lossless) {
        add_opt(&argc, argv, "--CostMode=lossless");
        add_opt(&argc, argv, "--SAO=0");
        add_opt(&argc, argv, "--LoopFilterDisable");
        add_opt(&argc, argv, "--TransquantBypassEnableFlag");
        add_opt(&argc, argv, "--CUTransquantBypassFlagForce");
        add_opt(&argc, argv, "--ImplicitResidualDPCM");
        add_opt(&argc, argv, "--GolombRiceParameterAdaptation");
        add_opt(&argc, argv, "--HadamardME=0");
    }

#if 0
    /* TEST with several slices */
    add_opt(&argc, argv, "--SliceMode=2");
    add_opt(&argc, argv, "--SliceArgument=5");
#endif

    /* trailing NULL */
    argv[argc] = NULL;

    if (s->params.verbose >= 2) {
        int i;
        printf("Encode options:");
        for(i = 0; i < argc; i++) {
            printf(" %s", argv[i]);
        }
        printf("\n");
    }
    
    if(!cTAppEncTop.parseCfg( argc, argv )) {
        fprintf(stderr, "Error while parsing options\n");
        cTAppEncTop.destroy();
        return -1;
    }
    
    cTAppEncTop.encode();
    
    cTAppEncTop.destroy();
    
    for(i = 0; i < argc; i++)
        free(argv[i]);
    unlink(s->infilename);

    /* read output bitstream */
    f = fopen(s->outfilename, "rb");
    if (!f) {
        fprintf(stderr, "Could not open '%s'\n", s->outfilename);
        return -1;
    }
    
    fseek(f, 0, SEEK_END);
    out_buf_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    out_buf = (uint8_t *)malloc(out_buf_len);
    if (fread(out_buf, 1, out_buf_len, f) != out_buf_len) {
        fprintf(stderr, "read error\n");
        fclose(f);
        free(out_buf);
        return -1;
    }
    fclose(f);
    unlink(s->outfilename);
    *pbuf = out_buf;
    free(s);
    return out_buf_len;
}

HEVCEncoder jctvc_encoder = {
  .open = jctvc_open,
  .encode = jctvc_encode,
  .close = jctvc_close,
};
