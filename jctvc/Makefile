CC=gcc
CFLAGS=-m64 -Os -Wall -MMD -fno-asynchronous-unwind-tables -g -Wno-sign-compare -Wno-unused-but-set-variable
LDFLAGS= -m64 -g
CFLAGS+=-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE
PWD:=$(shell pwd)
CFLAGS+=-I$(PWD)

CXX=g++
CXXFLAGS=$(CFLAGS)

PROGS=TAppEncoder jsenc


all: $(PROGS)

ENC_OBJS=$(addprefix TLibEncoder/, SyntaxElementWriter.o TEncSbac.o \
TEncBinCoderCABACCounter.o TEncGOP.o\
TEncSampleAdaptiveOffset.o TEncBinCoderCABAC.o TEncAnalyze.o\
TEncEntropy.o TEncTop.o SEIwrite.o TEncPic.o TEncRateCtrl.o\
WeightPredAnalysis.o TEncSlice.o TEncCu.o NALwrite.o TEncCavlc.o\
TEncSearch.o TEncPreanalyzer.o)
ENC_OBJS+=TLibVideoIO/TVideoIOYuv.o
ENC_OBJS+=$(addprefix TLibCommon/, TComWeightPrediction.o TComLoopFilter.o\
TComBitStream.o TComMotionInfo.o TComSlice.o ContextModel3DBuffer.o\
TComPic.o TComRdCostWeightPrediction.o TComTU.o TComPicSym.o\
TComPicYuv.o TComYuv.o TComTrQuant.o TComInterpolationFilter.o\
ContextModel.o TComSampleAdaptiveOffset.o SEI.o TComPrediction.o\
TComDataCU.o TComChromaFormat.o Debug.o TComRom.o\
TComPicYuvMD5.o TComRdCost.o TComPattern.o TComCABACTables.o)
ENC_OBJS+=libmd5/libmd5.o
ENC_OBJS+=TAppEncCfg.o TAppEncTop.o program_options_lite.o 

TAppEncoder: $(ENC_OBJS) encmain.o
	$(CXX) -o $@ $(LDFLAGS) $^ -lm

jsenc: jsenc.o jctvc_glue.o $(ENC_OBJS)
	$(CXX) -o $@ $(LDFLAGS) $^ -lpng -lm

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.d *~ \
	TLibEncoder/*.o TLibEncoder/*.d TLibEncoder/*~ \
	TLibVideoIO/*.o TLibVideoIO/*.d TLibVideoIO/*~ \
        TLibCommon/*.o TLibCommon/*.d TLibCommon/*~

-include $(wildcard *.d)
-include $(wildcard TLibEncoder/*.d)
-include $(wildcard TLibVideoIO/*.d)
-include $(wildcard TLibCommon/*.d)
