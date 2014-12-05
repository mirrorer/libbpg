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

/** \file     TComWeightPrediction.h
    \brief    weighting prediction class (header)
*/

#ifndef __TCOMWEIGHTPREDICTION__
#define __TCOMWEIGHTPREDICTION__


// Include files
#include "TComPic.h"
#include "TComMotionInfo.h"
#include "TComPattern.h"
#include "TComTrQuant.h"
#include "TComInterpolationFilter.h"

// ====================================================================================================================
// Class definition
// ====================================================================================================================
/// weighting prediction class
class TComWeightPrediction
{
public:
  TComWeightPrediction();

  Void  getWpScaling(                 TComDataCU     *const pcCU,
                                const Int                   iRefIdx0,
                                const Int                   iRefIdx1,
                                      WPScalingParam      *&wp0,
                                      WPScalingParam      *&wp1);

  Void addWeightBi(             const TComYuv              *pcYuvSrc0,
                                const TComYuv              *pcYuvSrc1,
                                const UInt                  iPartUnitIdx,
                                const UInt                  uiWidth,
                                const UInt                  uiHeight,
                                const WPScalingParam *const wp0,
                                const WPScalingParam *const wp1,
                                      TComYuv        *const rpcYuvDst,
                                const Bool                  bRoundLuma=true );

  Void  addWeightUni(           const TComYuv        *const pcYuvSrc0,
                                const UInt                  iPartUnitIdx,
                                const UInt                  uiWidth,
                                const UInt                  uiHeight,
                                const WPScalingParam *const wp0,
                                      TComYuv        *const rpcYuvDst );

  Void  xWeightedPredictionUni(       TComDataCU     *const pcCU,
                                const TComYuv        *const pcYuvSrc,
                                const UInt                  uiPartAddr,
                                const Int                   iWidth,
                                const Int                   iHeight,
                                const RefPicList            eRefPicList,
                                      TComYuv              *pcYuvPred,
                                const Int                   iRefIdx=-1 );

  Void  xWeightedPredictionBi(        TComDataCU     *const pcCU,
                                const TComYuv        *const pcYuvSrc0,
                                const TComYuv        *const pcYuvSrc1,
                                const Int                   iRefIdx0,
                                const Int                   iRefIdx1,
                                const UInt                  uiPartIdx,
                                const Int                   iWidth,
                                const Int                   iHeight,
                                      TComYuv              *pcYuvDst );
};

#endif
