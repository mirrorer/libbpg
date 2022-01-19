/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2014, ITU/ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __TCOMTU__
#define __TCOMTU__

class TComTU; // forward declaration

#include "CommonDef.h"
#include "TComRectangle.h"
#include "TComChromaFormat.h"

class TComDataCU; // forward declaration

//----------------------------------------------------------------------------------------------------------------------


class TComTU
{
  public:
    typedef enum TU_SPLIT_MODE { DONT_SPLIT=0, VERTICAL_SPLIT=1, QUAD_SPLIT=2, NUMBER_OF_SPLIT_MODES=3 } SPLIT_MODE;

    static const UInt NUMBER_OF_SECTIONS[NUMBER_OF_SPLIT_MODES];

  protected:
    ChromaFormat  mChromaFormat;
    Bool          mbProcessLastOfLevel; // if true, then if size n/2 x n/2 is invalid, the nxn block for a channel is processed only for the last block, not the first.
    UInt          mCuDepth;
    UInt          mTrDepthRelCU[MAX_NUM_COMPONENT];
    UInt          mSection;
    TU_SPLIT_MODE mSplitMode;
    TComRectangle mRect[MAX_NUM_COMPONENT];
    Bool          mCodeAll[MAX_NUM_COMPONENT];
    UInt          mOrigWidth[MAX_NUM_COMPONENT];
    UInt          mOffsets[MAX_NUM_COMPONENT];
    UInt          mAbsPartIdxCU;
    UInt          mAbsPartIdxTURelCU;
    UInt          mAbsPartIdxStep;
    TComDataCU   *mpcCU;
    UInt          mLog2TrLumaSize;
    TComTU       *mpParent;

    TComTU(const TComTU &);           // not defined - do not use
    TComTU&operator=(const TComTU &); // not defined - do not use

  public:
    TComTU(      TComDataCU *pcCU,
           const UInt        absPartIdxCU,
           const UInt        cuDepth,
           const UInt        initTrDepthRelCU);

  protected:
    TComTU(      TComTU        &parentLevel,
           const Bool           bProcessLastOfLevel,
           const TU_SPLIT_MODE  splitMode                 = QUAD_SPLIT,
           const Bool           splitAtCurrentDepth       = false,
           const ComponentID    absPartIdxSourceComponent = COMPONENT_Y
           );

  public:
          TComTU *Parent()       { return mpParent; }
    const TComTU *Parent() const { return mpParent; }

    UInt getCoefficientOffset(const ComponentID compID)        const { return mOffsets[compID]; }

    const TComRectangle &getRect(const ComponentID compID)     const { return mRect[compID];    }

    Bool ProcessingAllQuadrants(const ComponentID compID)      const { return mCodeAll[compID]; }
    Bool ProcessComponentSection(const ComponentID compID)     const { return mRect[compID].width != 0; }
    Bool ProcessChannelSection(const ChannelType chType)       const { return mRect[chType].width != 0; }
    UInt GetSectionNumber()                                    const { return mSection; }

    UInt getCUDepth()                                          const { return mCuDepth; }

    UInt GetTransformDepthTotal()                              const { return mCuDepth+GetTransformDepthRel(); }
    UInt GetTransformDepthTotalAdj(const ComponentID compID)   const { return mCuDepth+GetTransformDepthRelAdj(compID); }

    UInt GetTransformDepthRel()                                const { return mTrDepthRelCU[COMPONENT_Y]; }
    UInt GetTransformDepthRelAdj(const ComponentID compID)     const { return mTrDepthRelCU[compID]; }
    UInt GetTransformDepthRelAdj(const ChannelType chType)     const
    {
      assert(isLuma(chType) || (mTrDepthRelCU[COMPONENT_Cb] == mTrDepthRelCU[COMPONENT_Cr]));
      return mTrDepthRelCU[isLuma(chType) ? COMPONENT_Y : COMPONENT_Cb];
    }

    UInt GetAbsPartIdxCU()                                     const { return mAbsPartIdxCU; }
    UInt GetRelPartIdxTU()                                     const { return mAbsPartIdxTURelCU; }
    UInt GetRelPartIdxTU(const ComponentID compID)             const { return ProcessingAllQuadrants(compID) ? mAbsPartIdxTURelCU : (mAbsPartIdxTURelCU & (~0x3)); }
    UInt GetAbsPartIdxTU()                                     const { return GetAbsPartIdxCU() + GetRelPartIdxTU(); }
    UInt GetAbsPartIdxTU(const ComponentID compID)             const { return GetAbsPartIdxCU() + GetRelPartIdxTU(compID); }
    UInt GetAbsPartIdxNumParts()                               const { return mAbsPartIdxStep; }
    UInt GetAbsPartIdxNumParts(const ComponentID compID)       const { return ProcessingAllQuadrants(compID) ? mAbsPartIdxStep : (mAbsPartIdxStep * NUMBER_OF_SECTIONS[mSplitMode]); }

    ChromaFormat GetChromaFormat()                             const { return mChromaFormat; }

    TComDataCU *getCU()                                              { return mpcCU; }
    const TComDataCU *getCU()                                  const { return mpcCU; }
    Bool IsLastSection() const { return mSection+1>=((1<<mSplitMode)); }

    UInt GetLog2LumaTrSize()                                   const { return mLog2TrLumaSize; }
    UInt GetEquivalentLog2TrSize(const ComponentID compID)     const;
    TU_SPLIT_MODE GetSplitMode()                               const { return mSplitMode; }

    Bool useDST(const ComponentID compID);
    Bool isNonTransformedResidualRotated(const ComponentID compID);

    UInt getGolombRiceStatisticsIndex(const ComponentID compID);
};



class TComTURecurse : public TComTU
{
  public:

    TComTURecurse(      TComDataCU *pcCU,
                  const UInt        absPartIdxCU,
                  const UInt        forcedDepthOfCU)
      : TComTU(pcCU, absPartIdxCU, forcedDepthOfCU, 0) { }

    TComTURecurse(      TComDataCU *pcCU,
                  const UInt        absPartIdxCU); // CU's depth is taken from CU->getDepth(idx)

    TComTURecurse(      TComTU        &parentLevel,                            //Parent TU from which recursion children are derived
                  const Bool           bProcessLastOfLevel,                    //If true (and the split results in a "step-up" for chroma), the chroma TU is colocated with the last luma TU instead of the first
                  const TU_SPLIT_MODE  splitMode                 = QUAD_SPLIT, //DONT_SPLIT = create one new TU as a copy of its parent, VERTICAL_SPLIT = split the TU into top and bottom halves, QUAD_SPLIT = split the TU into four equal quadrants
                  const Bool           splitAtCurrentDepth       = false,      //Set true to keep the current depth when applying a vertical or quad split
                  const ComponentID    absPartIdxSourceComponent = COMPONENT_Y //Specifies which component of the parent TU should be used to initialise the absPartIdx of the first child and the absPartIdx step (this is needed when splitting a "stepped-up" chroma TU)
                  )
                  : TComTU(parentLevel, bProcessLastOfLevel, splitMode, splitAtCurrentDepth, absPartIdxSourceComponent) { }

    Bool nextSection(const TComTU &parent); // returns true if there is another section to process, and prepares internal structures, else returns false
};

//----------------------------------------------------------------------------------------------------------------------

#endif
