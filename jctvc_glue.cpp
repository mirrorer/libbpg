#ifdef WIN32
#include <windows.h>
#endif
#include <unistd.h>
#include <iostream>
#include "TAppEncTop.h"
#include "TLibCommon/Debug.h"
#include "TLibEncoder/TEncAnalyze.h"

#include "bpgenc.h"

#define ARGV_MAX 128

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

/* return the encoded data in *pbuf and the size. Return < 0 if error */
int jctvc_encode_picture(uint8_t **pbuf, Image *img, 
                         const HEVCEncodeParams *params)
{
    TAppEncTop  cTAppEncTop;
    int argc;
    char *argv[ARGV_MAX + 1];
    char buf[1024], infilename[1024], outfilename[1024];
    const char *str;
    FILE *f;
    uint8_t *out_buf;
    int out_buf_len, i;
    
#ifdef WIN32
    if (GetTempPath(sizeof(buf), buf) > sizeof(buf) - 1) {
        fprintf(stderr, "Temporary path too long\n");
        return -1;
    }
#else
    strcpy(buf, "/tmp/");
#endif
    snprintf(infilename, sizeof(infilename), "%sout%d.yuv", buf, getpid());
    snprintf(outfilename, sizeof(outfilename), "%sout%d.bin", buf, getpid());

    save_yuv(img, infilename);

    m_gcAnalyzeAll.clear();
    m_gcAnalyzeI.clear();
    m_gcAnalyzeP.clear();
    m_gcAnalyzeB.clear();
    m_gcAnalyzeAll_in.clear();
    
    cTAppEncTop.create();

    argc = 0;
    add_opt(&argc, argv, "jctvc"); /* dummy executable name */

    snprintf(buf, sizeof(buf),"--InputFile=%s", infilename);
    add_opt(&argc, argv, buf);
    snprintf(buf, sizeof(buf),"--BitstreamFile=%s", outfilename);
    add_opt(&argc, argv, buf);

    snprintf(buf, sizeof(buf),"--SourceWidth=%d", img->w);
    add_opt(&argc, argv, buf);

    snprintf(buf, sizeof(buf),"--SourceWidth=%d", img->w);
    add_opt(&argc, argv, buf);
    snprintf(buf, sizeof(buf),"--SourceHeight=%d", img->h);
    add_opt(&argc, argv, buf);
    snprintf(buf, sizeof(buf),"--InputBitDepth=%d", img->bit_depth);
    add_opt(&argc, argv, buf);

    switch(img->format) {
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

    snprintf(buf, sizeof(buf),"--QP=%d", params->qp);
    add_opt(&argc, argv, buf);
    
    snprintf(buf, sizeof(buf),"--SEIDecodedPictureHash=%d", 
             params->sei_decoded_picture_hash);
    add_opt(&argc, argv, buf);
    
    if (!params->verbose)
      add_opt(&argc, argv, "--Verbose=0");
      
    /* single frame */
    add_opt(&argc, argv, "--FramesToBeEncoded=1");
    
    /* no padding necessary (it is done before) */
    add_opt(&argc, argv, "--ConformanceWindowMode=0");
    
    /* dummy frame rate */
    add_opt(&argc, argv, "--FrameRate=25");

    /* general config */
    add_opt(&argc, argv, "--Profile=main_444_16_intra");

    add_opt(&argc, argv, "--QuadtreeTULog2MaxSize=5");
    add_opt(&argc, argv, "--QuadtreeTUMaxDepthIntra=3");

    add_opt(&argc, argv, "--IntraPeriod=1");
    add_opt(&argc, argv, "--GOPSize=1");
    add_opt(&argc, argv, "--TransformSkip=1");
    add_opt(&argc, argv, "--TransformSkipFast=1");

    /* Note: Format Range extension */
    if (img->format == BPG_FORMAT_444) {
        add_opt(&argc, argv, "--CrossComponentPrediction=1");
    }

    if (params->lossless) {
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

    if (params->verbose >= 2) {
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
    unlink(infilename);

    /* read output bitstream */
    f = fopen(outfilename, "rb");
    if (!f) {
        fprintf(stderr, "Could not open '%s'\n", outfilename);
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
    unlink(outfilename);
    *pbuf = out_buf;
    return out_buf_len;
}
