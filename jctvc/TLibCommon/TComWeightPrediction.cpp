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

// Include files
#include "TComSlice.h"
#include "TComWeightPrediction.h"
#include "TComInterpolationFilter.h"


static inline Pel weightBidir( Int w0, Pel P0, Int w1, Pel P1, Int round, Int shift, Int offset, Int clipBD)
{
  return ClipBD( ( (w0*(P0 + IF_INTERNAL_OFFS) + w1*(P1 + IF_INTERNAL_OFFS) + round + (offset << (shift-1))) >> shift ), clipBD );
}


static inline Pel weightUnidir( Int w0, Pel P0, Int round, Int shift, Int offset, Int clipBD)
{
  return ClipBD( ( (w0*(P0 + IF_INTERNAL_OFFS) + round) >> shift ) + offset, clipBD );
}

// ====================================================================================================================
// Class definition
// ====================================================================================================================

TComWeightPrediction::TComWeightPrediction()
{
}


/** weighted averaging for bi-pred
 * \param TComYuv* pcYuvSrc0
 * \param TComYuv* pcYuvSrc1
 * \param iPartUnitIdx
 * \param iWidth
 * \param iHeight
 * \param WPScalingParam *wp0
 * \param WPScalingParam *wp1
 * \param TComYuv* rpcYuvDst
 * \returns Void
 */
Void TComWeightPrediction::addWeightBi( const TComYuv              *pcYuvSrc0,
                                        const TComYuv              *pcYuvSrc1,
                                        const UInt                  iPartUnitIdx,
                                        const UInt                  uiWidth,
                                        const UInt                  uiHeight,
                                        const WPScalingParam *const wp0,
                                        const WPScalingParam *const wp1,
                                              TComYuv        *const rpcYuvDst,
                                        const Bool                  bRoundLuma )
{

  const Bool enableRounding[MAX_NUM_COMPONENT]={ bRoundLuma, true, true };

  const UInt numValidComponent = pcYuvSrc0->getNumberValidComponents();

  for(Int componentIndex=0; componentIndex<numValidComponent; componentIndex++)
  {
    const ComponentID compID=ComponentID(componentIndex);

    const Pel* pSrc0       = pcYuvSrc0->getAddr( compID,  iPartUnitIdx );
    const Pel* pSrc1       = pcYuvSrc1->getAddr( compID,  iPartUnitIdx );
          Pel* pDst        = rpcYuvDst->getAddr( compID,  iPartUnitIdx );

    // Luma : --------------------------------------------
    const Int  w0          = wp0[compID].w;
    const Int  offset      = wp0[compID].offset;
    const Int  clipBD      = g_bitDepth[toChannelType(compID)];
    const Int  shiftNum    = std::max<Int>(2, (IF_INTERNAL_PREC - clipBD));
    const Int  shift       = wp0[compID].shift + shiftNum;
    const Int  round       = (enableRounding[compID] && (shift > 0)) ? (1<<(shift-1)) : 0;
    const Int  w1          = wp1[compID].w;
    const UInt csx         = pcYuvSrc0->getComponentScaleX(compID);
    const UInt csy         = pcYuvSrc0->getComponentScaleY(compID);
    const Int  iHeight     = uiHeight>>csy;
    const Int  iWidth      = uiWidth>>csx;

    const UInt iSrc0Stride = pcYuvSrc0->getStride(compID);
    const UInt iSrc1Stride = pcYuvSrc1->getStride(compID);
    const UInt iDstStride  = rpcYuvDst->getStride(compID);

    for ( Int y = iHeight-1; y >= 0; y-- )
    {
      // do it in batches of 4 (partial unroll)
      Int x = iWidth-1;
      for ( ; x >= 3; )
      {
        pDst[x] = weightBidir(w0,pSrc0[x], w1,pSrc1[x], round, shift, offset, clipBD); x--;
        pDst[x] = weightBidir(w0,pSrc0[x], w1,pSrc1[x], round, shift, offset, clipBD); x--;
        pDst[x] = weightBidir(w0,pSrc0[x], w1,pSrc1[x], round, shift, offset, clipBD); x--;
        pDst[x] = weightBidir(w0,pSrc0[x], w1,pSrc1[x], round, shift, offset, clipBD); x--;
      }
      for( ; x >= 0; x-- )
      {
        pDst[x] = weightBidir(w0,pSrc0[x], w1,pSrc1[x], round, shift, offset, clipBD);
      }

      pSrc0 += iSrc0Stride;
      pSrc1 += iSrc1Stride;
      pDst  += iDstStride;
    } // y loop
  } // compID loop
}


/** weighted averaging for uni-pred
 * \param TComYuv* pcYuvSrc0
 * \param iPartUnitIdx
 * \param iWidth
 * \param iHeight
 * \param WPScalingParam *wp0
 * \param TComYuv* rpcYuvDst
 * \returns Void
 */
Void TComWeightPrediction::addWeightUni( const TComYuv        *const pcYuvSrc0,
                                         const UInt                  iPartUnitIdx,
                                         const UInt                  uiWidth,
                                         const UInt                  uiHeight,
                                         const WPScalingParam *const wp0,
                                               TComYuv        *const pcYuvDst )
{
  const UInt numValidComponent = pcYuvSrc0->getNumberValidComponents();

  for(Int componentIndex=0; componentIndex<numValidComponent; componentIndex++)
  {
    const ComponentID compID=ComponentID(componentIndex);

    const Pel* pSrc0       = pcYuvSrc0->getAddr( compID,  iPartUnitIdx );
          Pel* pDst        = pcYuvDst->getAddr( compID,  iPartUnitIdx );

    // Luma : --------------------------------------------
    const Int  w0          = wp0[compID].w;
    const Int  offset      = wp0[compID].offset;
    const Int  clipBD      = g_bitDepth[toChannelType(compID)];
    const Int  shiftNum    = std::max<Int>(2, (IF_INTERNAL_PREC - clipBD));
    const Int  shift       = wp0[compID].shift + shiftNum;
    const Int  round       = (shift > 0) ? (1<<(shift-1)) : 0;
    const UInt iSrc0Stride = pcYuvSrc0->getStride(compID);
    const UInt iDstStride  = pcYuvDst->getStride(compID);
    const UInt csx         = pcYuvSrc0->getComponentScaleX(compID);
    const UInt csy         = pcYuvSrc0->getComponentScaleY(compID);
    const Int  iHeight     = uiHeight>>csy;
    const Int  iWidth      = uiWidth>>csx;

    for (Int y = iHeight-1; y >= 0; y-- )
    {
      Int x = iWidth-1;
      for ( ; x >= 3; )
      {
        pDst[x] = weightUnidir(w0, pSrc0[x], round, shift, offset, clipBD); x--;
        pDst[x] = weightUnidir(w0, pSrc0[x], round, shift, offset, clipBD); x--;
        pDst[x] = weightUnidir(w0, pSrc0[x], round, shift, offset, clipBD); x--;
        pDst[x] = weightUnidir(w0, pSrc0[x], round, shift, offset, clipBD); x--;
      }
      for( ; x >= 0; x--)
      {
        pDst[x] = weightUnidir(w0, pSrc0[x], round, shift, offset, clipBD);
      }
      pSrc0 += iSrc0Stride;
      pDst  += iDstStride;
    }
  }
}


//=======================================================
//  getWpScaling()
//=======================================================
/** derivation of wp tables
 * \param TComDataCU* pcCU
 * \param iRefIdx0
 * \param iRefIdx1
 * \param WPScalingParam *&wp0
 * \param WPScalingParam *&wp1
 * \param ibdi
 * \returns Void
 */
Void TComWeightPrediction::getWpScaling(       TComDataCU *const pcCU,
                                         const Int               iRefIdx0,
                                         const Int               iRefIdx1,
                                               WPScalingParam  *&wp0,
                                               WPScalingParam  *&wp1)
{
  assert(iRefIdx0 >= 0 || iRefIdx1 >= 0);

        TComSlice *const pcSlice  = pcCU->getSlice();
  const Bool             wpBiPred = pcCU->getSlice()->getPPS()->getWPBiPred();
  const Bool             bBiDir   = (iRefIdx0>=0 && iRefIdx1>=0);
  const Bool             bUniDir  = !bBiDir;

  if ( bUniDir || wpBiPred )
  { // explicit --------------------
    if ( iRefIdx0 >= 0 )
    {
      pcSlice->getWpScaling(REF_PIC_LIST_0, iRefIdx0, wp0);
    }
    if ( iRefIdx1 >= 0 )
    {
      pcSlice->getWpScaling(REF_PIC_LIST_1, iRefIdx1, wp1);
    }
  }
  else
  {
    assert(0);
  }

  if ( iRefIdx0 < 0 )
  {
    wp0 = NULL;
  }
  if ( iRefIdx1 < 0 )
  {
    wp1 = NULL;
  }

  const UInt numValidComponent                    = pcCU->getPic()->getNumberValidComponents();
  const Bool bUseHighPrecisionPredictionWeighting = pcSlice->getSPS()->getUseHighPrecisionPredictionWeighting();

  if ( bBiDir )
  { // Bi-Dir case
    for ( Int yuv=0 ; yuv<numValidComponent ; yuv++ )
    {
      const Int bitDepth            = g_bitDepth[toChannelType(ComponentID(yuv))];
      const Int offsetScalingFactor = bUseHighPrecisionPredictionWeighting ? 1 : (1 << (bitDepth-8));

      wp0[yuv].w      = wp0[yuv].iWeight;
      wp1[yuv].w      = wp1[yuv].iWeight;
      wp0[yuv].o      = wp0[yuv].iOffset * offsetScalingFactor;
      wp1[yuv].o      = wp1[yuv].iOffset * offsetScalingFactor;
      wp0[yuv].offset = wp0[yuv].o + wp1[yuv].o;
      wp0[yuv].shift  = wp0[yuv].uiLog2WeightDenom + 1;
      wp0[yuv].round  = (1 << wp0[yuv].uiLog2WeightDenom);
      wp1[yuv].offset = wp0[yuv].offset;
      wp1[yuv].shift  = wp0[yuv].shift;
      wp1[yuv].round  = wp0[yuv].round;
    }
  }
  else
  {  // Unidir
    WPScalingParam *const pwp = (iRefIdx0>=0) ? wp0 : wp1 ;

    for ( Int yuv=0 ; yuv<numValidComponent ; yuv++ )
    {
      const Int bitDepth            = g_bitDepth[toChannelType(ComponentID(yuv))];
      const Int offsetScalingFactor = bUseHighPrecisionPredictionWeighting ? 1 : (1 << (bitDepth-8));

      pwp[yuv].w      = pwp[yuv].iWeight;
      pwp[yuv].offset = pwp[yuv].iOffset * offsetScalingFactor;
      pwp[yuv].shift  = pwp[yuv].uiLog2WeightDenom;
      pwp[yuv].round  = (pwp[yuv].uiLog2WeightDenom>=1) ? (1 << (pwp[yuv].uiLog2WeightDenom-1)) : (0);
    }
  }
}


/** weighted prediction for bi-pred
 * \param TComDataCU* pcCU
 * \param TComYuv* pcYuvSrc0
 * \param TComYuv* pcYuvSrc1
 * \param iRefIdx0
 * \param iRefIdx1
 * \param uiPartIdx
 * \param iWidth
 * \param iHeight
 * \param TComYuv* rpcYuvDst
 * \returns Void
 */
Void TComWeightPrediction::xWeightedPredictionBi(       TComDataCU *const pcCU,
                                                  const TComYuv    *const pcYuvSrc0,
                                                  const TComYuv    *const pcYuvSrc1,
                                                  const Int               iRefIdx0,
                                                  const Int               iRefIdx1,
                                                  const UInt              uiPartIdx,
                                                  const Int               iWidth,
                                                  const Int               iHeight,
                                                        TComYuv          *rpcYuvDst )
{
  WPScalingParam  *pwp0;
  WPScalingParam  *pwp1;

  assert(pcCU->getSlice()->getPPS()->getWPBiPred());

  getWpScaling(pcCU, iRefIdx0, iRefIdx1, pwp0, pwp1);

  if( iRefIdx0 >= 0 && iRefIdx1 >= 0 )
  {
    addWeightBi(pcYuvSrc0, pcYuvSrc1, uiPartIdx, iWidth, iHeight, pwp0, pwp1, rpcYuvDst );
  }
  else if ( iRefIdx0 >= 0 && iRefIdx1 <  0 )
  {
    addWeightUni( pcYuvSrc0, uiPartIdx, iWidth, iHeight, pwp0, rpcYuvDst );
  }
  else if ( iRefIdx0 <  0 && iRefIdx1 >= 0 )
  {
    addWeightUni( pcYuvSrc1, uiPartIdx, iWidth, iHeight, pwp1, rpcYuvDst );
  }
  else
  {
    assert (0);
  }
}


/** weighted prediction for uni-pred
 * \param TComDataCU* pcCU
 * \param TComYuv* pcYuvSrc
 * \param uiPartAddr
 * \param iWidth
 * \param iHeight
 * \param eRefPicList
 * \param TComYuv* pcYuvPred
 * \param iPartIdx
 * \param iRefIdx
 * \returns Void
 */
Void TComWeightPrediction::xWeightedPredictionUni(       TComDataCU *const pcCU,
                                                   const TComYuv    *const pcYuvSrc,
                                                   const UInt              uiPartAddr,
                                                   const Int               iWidth,
                                                   const Int               iHeight,
                                                   const RefPicList        eRefPicList,
                                                         TComYuv          *pcYuvPred,
                                                   const Int               iRefIdx_input)
{
  WPScalingParam  *pwp, *pwpTmp;

  Int iRefIdx=iRefIdx_input;
  if ( iRefIdx < 0 )
  {
    iRefIdx   = pcCU->getCUMvField( eRefPicList )->getRefIdx( uiPartAddr );
  }
  assert (iRefIdx >= 0);

  if ( eRefPicList == REF_PIC_LIST_0 )
  {
    getWpScaling(pcCU, iRefIdx, -1, pwp, pwpTmp);
  }
  else
  {
    getWpScaling(pcCU, -1, iRefIdx, pwpTmp, pwp);
  }
  addWeightUni( pcYuvSrc, uiPartAddr, iWidth, iHeight, pwp, pcYuvPred );
}
