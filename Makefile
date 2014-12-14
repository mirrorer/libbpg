# libbpg Makefile
# 
# Compile options:
#
# Enable compilation of Javascript decoder with Emscripten
#USE_EMCC=y
# Enable x265 for the encoder (you must install it before)
#USE_X265=y
# Enable the JCTVC code (best quality but slow) for the encoder
USE_JCTVC=y
# Enable it to use bit depths > 12 (need more tests to validate encoder)
#USE_JCTVC_HIGH_BIT_DEPTH=y
# Enable the cross compilation for Windows
#CONFIG_WIN32=y
# Enable for compilation on MacOS X
#CONFIG_APPLE=y
# Installation prefix
prefix=/usr/local


#################################

ifdef CONFIG_WIN32
#CROSS_PREFIX:=x86_64-w64-mingw32-
CROSS_PREFIX=i686-w64-mingw32-
EXE:=.exe
else
CROSS_PREFIX:=
EXE:=
endif

CC=$(CROSS_PREFIX)gcc
CXX=$(CROSS_PREFIX)g++
AR=$(CROSS_PREFIX)ar
EMCC=emcc

PWD:=$(shell pwd)

CFLAGS:=-Os -Wall -MMD -fno-asynchronous-unwind-tables -fdata-sections -ffunction-sections -fno-math-errno -fno-signed-zeros -fno-tree-vectorize -fomit-frame-pointer
CFLAGS+=-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_REENTRANT
CFLAGS+=-I.
CFLAGS+=-DCONFIG_BPG_VERSION=\"$(shell cat VERSION)\"
ifdef USE_JCTVC_HIGH_BIT_DEPTH
CFLAGS+=-DRExt__HIGH_BIT_DEPTH_SUPPORT
endif

# Emscriptem config
EMLDFLAGS:=-s "EXPORTED_FUNCTIONS=['_bpg_decoder_open','_bpg_decoder_decode','_bpg_decoder_get_info','_bpg_decoder_start','_bpg_decoder_get_line','_bpg_decoder_close','_malloc','_free']"
EMLDFLAGS+=-s NO_FILESYSTEM=1 -s NO_BROWSER=1
#EMLDFLAGS+=-O1 --post-js post.js
EMLDFLAGS+=-O3 --memory-init-file 0 --closure 1 --post-js post.js
EMCFLAGS:=$(CFLAGS)

LDFLAGS=-g
ifdef CONFIG_APPLE
LDFLAGS+=-Wl,-dead_strip
else
LDFLAGS+=-Wl,--gc-sections
endif
CFLAGS+=-g
CXXFLAGS=$(CFLAGS)

PROGS=bpgdec$(EXE) bpgenc$(EXE)
ifdef USE_EMCC
PROGS+=bpgdec.js bpgdec8b.js
endif

all: $(PROGS)

LIBBPG_OBJS:=$(addprefix libavcodec/, \
hevc_cabac.o  hevc_filter.o  hevc.o         hevcpred.o  hevc_refs.o\
hevcdsp.o     hevc_mvs.o     hevc_ps.o   hevc_sei.o\
utils.o cabac.o golomb.o )
LIBBPG_OBJS+=$(addprefix libavutil/, mem.o buffer.o log2_tab.o frame.o pixdesc.o md5.o )
LIBBPG_OBJS+=libbpg.o

LIBBPG_JS_OBJS:=$(patsubst %.o, %.js.o, $(LIBBPG_OBJS)) tmalloc.js.o

LIBBPG_JS8_OBJS:=$(patsubst %.o, %.js8.o, $(LIBBPG_OBJS)) tmalloc.js8.o

$(LIBBPG_OBJS): CFLAGS+=-D_ISOC99_SOURCE -D_POSIX_C_SOURCE=200112 -D_XOPEN_SOURCE=600 -DHAVE_AV_CONFIG_H -std=c99 -D_GNU_SOURCE=1 -DUSE_VAR_BIT_DEPTH

$(LIBBPG_JS_OBJS): EMCFLAGS+=-D_ISOC99_SOURCE -D_POSIX_C_SOURCE=200112 -D_XOPEN_SOURCE=600 -DHAVE_AV_CONFIG_H -std=c99 -D_GNU_SOURCE=1 -DUSE_VAR_BIT_DEPTH

$(LIBBPG_JS8_OBJS): EMCFLAGS+=-D_ISOC99_SOURCE -D_POSIX_C_SOURCE=200112 -D_XOPEN_SOURCE=600 -DHAVE_AV_CONFIG_H -std=c99 -D_GNU_SOURCE=1

BPGENC_OBJS:=bpgenc.o
BPGENC_LIBS:=

ifdef USE_X265
BPGENC_OBJS+=x265_glue.o
BPGENC_LIBS+= -lx265
bpgenc.o: CFLAGS+=-DUSE_X265
endif # USE_X265

ifdef USE_JCTVC
JCTVC_OBJS=$(addprefix jctvc/TLibEncoder/, SyntaxElementWriter.o TEncSbac.o \
TEncBinCoderCABACCounter.o TEncGOP.o\
TEncSampleAdaptiveOffset.o TEncBinCoderCABAC.o TEncAnalyze.o\
TEncEntropy.o TEncTop.o SEIwrite.o TEncPic.o TEncRateCtrl.o\
WeightPredAnalysis.o TEncSlice.o TEncCu.o NALwrite.o TEncCavlc.o\
TEncSearch.o TEncPreanalyzer.o)
JCTVC_OBJS+=jctvc/TLibVideoIO/TVideoIOYuv.o
JCTVC_OBJS+=$(addprefix jctvc/TLibCommon/, TComWeightPrediction.o TComLoopFilter.o\
TComBitStream.o TComMotionInfo.o TComSlice.o ContextModel3DBuffer.o\
TComPic.o TComRdCostWeightPrediction.o TComTU.o TComPicSym.o\
TComPicYuv.o TComYuv.o TComTrQuant.o TComInterpolationFilter.o\
ContextModel.o TComSampleAdaptiveOffset.o SEI.o TComPrediction.o\
TComDataCU.o TComChromaFormat.o Debug.o TComRom.o\
TComPicYuvMD5.o TComRdCost.o TComPattern.o TComCABACTables.o)
JCTVC_OBJS+=jctvc/libmd5/libmd5.o
JCTVC_OBJS+=jctvc/TAppEncCfg.o jctvc/TAppEncTop.o jctvc/program_options_lite.o 

$(JCTVC_OBJS) jctvc_glue.o: CFLAGS+=-I$(PWD)/jctvc -Wno-sign-compare

jctvc/libjctvc.a: $(JCTVC_OBJS)
	$(AR) rcs $@ $^

BPGENC_OBJS+=jctvc_glue.o jctvc/libjctvc.a

bpgenc.o: CFLAGS+=-DUSE_JCTVC
endif # USE_JCTVC

ifdef CONFIG_WIN32
LIBS:=-lz
LDFLAGS+=-static
else
ifdef CONFIG_APPLE
LIBS:=
else
LIBS:=-lrt
endif # !CONFIG_APPLE 
LIBS+=-lm -lpthread
endif # !CONFIG_WIN32

BPGENC_LIBS+=-lpng -ljpeg $(LIBS)

bpgenc.o: CFLAGS+=-Wno-unused-but-set-variable

libbpg.a: $(LIBBPG_OBJS) 
	$(AR) rcs $@ $^

bpgdec$(EXE): bpgdec.o libbpg.a
	$(CC) $(LDFLAGS) -o $@ $^ -lpng $(LIBS)

bpgenc$(EXE): $(BPGENC_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(BPGENC_LIBS)

bpgdec.js: $(LIBBPG_JS_OBJS) post.js
	$(EMCC) $(EMLDFLAGS) -s TOTAL_MEMORY=33554432 -o $@ $(LIBBPG_JS_OBJS)

bpgdec8b.js: $(LIBBPG_JS8_OBJS) post.js
	$(EMCC) $(EMLDFLAGS) -s TOTAL_MEMORY=16777216 -o $@ $(LIBBPG_JS8_OBJS)

size:
	strip bpgdec
	size bpgdec libbpg.o libavcodec/*.o libavutil/*.o | sort -n
	gzip < bpgdec | wc

install: bpgenc bpgdec
	install -s -m 755 $^ $(prefix)/bin

CLEAN_DIRS=doc html libavcodec libavutil \
     jctvc jctvc/TLibEncoder jctvc/TLibVideoIO jctvc/TLibCommon jctvc/libmd5

clean:
	rm -f $(PROGS) *.o *.a *.d *~ $(addsuffix /*.o, $(CLEAN_DIRS)) \
          $(addsuffix /*.d, $(CLEAN_DIRS)) $(addsuffix /*~, $(CLEAN_DIRS)) \
          $(addsuffix /*.a, $(CLEAN_DIRS))

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.js.o: %.c
	$(EMCC) $(EMCFLAGS) -c -o $@ $<

%.js8.o: %.c
	$(EMCC) $(EMCFLAGS) -c -o $@ $<

-include $(wildcard *.d)
-include $(wildcard libavcodec/*.d)
-include $(wildcard libavutil/*.d)
-include $(wildcard jctvc/*.d)
-include $(wildcard jctvc/TLibEncoder/*.d)
-include $(wildcard jctvc/TLibVideoIO/*.d)
-include $(wildcard jctvc/TLibCommon/*.d)
-include $(wildcard jctvc/libmd5/*.d)
