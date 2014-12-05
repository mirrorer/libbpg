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

/** \file     WeightPredAnalysis.cpp
    \brief    weighted prediction encoder class
*/

#include "../TLibCommon/TypeDef.h"
#include "../TLibCommon/TComSlice.h"
#include "../TLibCommon/TComPic.h"
#include "../TLibCommon/TComPicYuv.h"
#include "WeightPredAnalysis.h"

#define ABS(a)    ((a) < 0 ? - (a) : (a))
#define DTHRESH (0.99)

WeightPredAnalysis::WeightPredAnalysis()
{
  m_weighted_pred_flag = false;
  m_weighted_bipred_flag = false;

  for ( UInt lst =0 ; lst<NUM_REF_PIC_LIST_01 ; lst++ )
  {
    for ( Int iRefIdx=0 ; iRefIdx<MAX_NUM_REF ; iRefIdx++ )
    {
      for ( Int comp=0 ; comp<MAX_NUM_COMPONENT ;comp++ )
      {
        WPScalingParam  *pwp   = &(m_wp[lst][iRefIdx][comp]);
        pwp->bPresentFlag      = false;
        pwp->uiLog2WeightDenom = 0;
        pwp->iWeight           = 1;
        pwp->iOffset           = 0;
      }
    }
  }
}


/** calculate AC and DC values for current original image
 * \param TComSlice *slice
 * \returns Void
 */
Void WeightPredAnalysis::xCalcACDCParamSlice(TComSlice *const slice)
{
  //===== calculate AC/DC value =====
  TComPicYuv*   pPic = slice->getPic()->getPicYuvOrg();

  WPACDCParam weightACDCParam[MAX_NUM_COMPONENT];

  for(Int componentIndex = 0; componentIndex < pPic->getNumberValidComponents(); componentIndex++)
  {
    const ComponentID compID = ComponentID(componentIndex);

    // calculate DC/AC value for channel

    const Int iStride = pPic->getStride(compID);
    const Int iWidth  = pPic->getWidth(compID);
    const Int iHeight = pPic->getHeight(compID);

    const Int iSample = iWidth*iHeight;

    Int64 iOrgDC = 0;
    {
      const Pel *pPel = pPic->getAddr(compID);

      for(Int y = 0; y < iHeight; y++, pPel+=iStride )
        for(Int x = 0; x < iWidth; x++ )
          iOrgDC += (Int)( pPel[x] );
    }

    const Int64 iOrgNormDC = ((iOrgDC+(iSample>>1)) / iSample);

    Int64 iOrgAC = 0;
    {
      const Pel *pPel = pPic->getAddr(compID);

      for(Int y = 0; y < iHeight; y++, pPel += iStride )
        for(Int x = 0; x < iWidth; x++ )
          iOrgAC += abs( (Int)pPel[x] - (Int)iOrgNormDC );
    }

    const Int fixedBitShift = (slice->getSPS()->getUseHighPrecisionPredictionWeighting())?RExt__PREDICTION_WEIGHTING_ANALYSIS_DC_PRECISION:0;
    weightACDCParam[compID].iDC = (((iOrgDC<<fixedBitShift)+(iSample>>1)) / iSample);
    weightACDCParam[compID].iAC = iOrgAC;
  }

  slice->setWpAcDcParam(weightACDCParam);
}


/** store weighted_pred_flag and weighted_bipred_idc values
 * \param weighted_pred_flag
 * \param weighted_bipred_idc
 * \returns Void
 */
Void  WeightPredAnalysis::xStoreWPparam(const Bool weighted_pred_flag, const Bool weighted_bipred_flag)
{
  m_weighted_pred_flag   = weighted_pred_flag;
  m_weighted_bipred_flag = weighted_bipred_flag;
}


/** restore weighted_pred_flag and weighted_bipred_idc values
 * \param TComSlice *slice
 * \returns Void
 */
Void  WeightPredAnalysis::xRestoreWPparam(TComSlice *const slice)
{
  slice->getPPS()->setUseWP   (m_weighted_pred_flag);
  slice->getPPS()->setWPBiPred(m_weighted_bipred_flag);
}


/** check weighted pred or non-weighted pred
 * \param TComSlice *slice
 * \returns Void
 */
Void  WeightPredAnalysis::xCheckWPEnable(TComSlice *const slice)
{
  const TComPicYuv *pPic = slice->getPic()->getPicYuvOrg();

  Int iPresentCnt = 0;
  for ( UInt lst=0 ; lst<NUM_REF_PIC_LIST_01 ; lst++ )
  {
    for ( Int iRefIdx=0 ; iRefIdx<MAX_NUM_REF ; iRefIdx++ )
    {
      for(Int componentIndex = 0; componentIndex < pPic->getNumberValidComponents(); componentIndex++)
      {
        WPScalingParam  *pwp = &(m_wp[lst][iRefIdx][componentIndex]);
        iPresentCnt += (Int)pwp->bPresentFlag;
      }
    }
  }

  if(iPresentCnt==0)
  {
    slice->getPPS()->setUseWP(false);
    slice->getPPS()->setWPBiPred(false);

    for ( UInt lst=0 ; lst<NUM_REF_PIC_LIST_01 ; lst++ )
    {
      for ( Int iRefIdx=0 ; iRefIdx<MAX_NUM_REF ; iRefIdx++ )
      {
        for(Int componentIndex = 0; componentIndex < pPic->getNumberValidComponents(); componentIndex++)
        {
          WPScalingParam  *pwp = &(m_wp[lst][iRefIdx][componentIndex]);

          pwp->bPresentFlag      = false;
          pwp->uiLog2WeightDenom = 0;
          pwp->iWeight           = 1;
          pwp->iOffset           = 0;
        }
      }
    }
    slice->setWpScaling( m_wp );
  }
}


/** estimate wp tables for explicit wp
 * \param TComSlice *slice
 */
Void WeightPredAnalysis::xEstimateWPParamSlice(TComSlice *const slice)
{
  Int  iDenom         = 6;
  Bool validRangeFlag = false;

  if(slice->getNumRefIdx(REF_PIC_LIST_0)>3)
  {
    iDenom = 7;
  }

  do
  {
    validRangeFlag = xUpdatingWPParameters(slice, iDenom);
    if (!validRangeFlag)
    {
      iDenom--; // decrement to satisfy the range limitation
    }
  } while (validRangeFlag == false);

  // selecting whether WP is used, or not
  xSelectWP(slice, iDenom);

  slice->setWpScaling( m_wp );
}


/** update wp tables for explicit wp w.r.t ramge limitation
 * \param TComSlice *slice
 * \returns Bool
 */
Bool WeightPredAnalysis::xUpdatingWPParameters(TComSlice *const slice, const Int log2Denom)
{
  const Int  numComp                    = slice->getPic()->getPicYuvOrg()->getNumberValidComponents();
  const Bool bUseHighPrecisionWeighting = slice->getSPS()->getUseHighPrecisionPredictionWeighting();
  const Int numPredDir                  = slice->isInterP() ? 1 : 2;

  assert (numPredDir <= Int(NUM_REF_PIC_LIST_01));

  for ( Int refList = 0; refList < numPredDir; refList++ )
  {
    const RefPicList eRefPicList = ( refList ? REF_PIC_LIST_1 : REF_PIC_LIST_0 );

    for ( Int refIdxTemp = 0; refIdxTemp < slice->getNumRefIdx(eRefPicList); refIdxTemp++ )
    {
      WPACDCParam *currWeightACDCParam, *refWeightACDCParam;
      slice->getWpAcDcParam(currWeightACDCParam);
      slice->getRefPic(eRefPicList, refIdxTemp)->getSlice(0)->getWpAcDcParam(refWeightACDCParam);

      for ( Int comp = 0; comp < numComp; comp++ )
      {
        const ComponentID compID        = ComponentID(comp);
        const Int         range         = bUseHighPrecisionWeighting ? (1<<g_bitDepth[toChannelType(compID)])/2 : 128;
        const Int         realLog2Denom = log2Denom + (bUseHighPrecisionWeighting ? RExt__PREDICTION_WEIGHTING_ANALYSIS_DC_PRECISION : (g_bitDepth[toChannelType(compID)] - 8));
        const Int         realOffset    = ((Int)1<<(realLog2Denom-1));

        // current frame
        const Int64 currDC = currWeightACDCParam[comp].iDC;
        const Int64 currAC = currWeightACDCParam[comp].iAC;
        // reference frame
        const Int64 refDC  = refWeightACDCParam[comp].iDC;
        const Int64 refAC  = refWeightACDCParam[comp].iAC;

        // calculating iWeight and iOffset params
        const Double dWeight = (refAC==0) ? (Double)1.0 : Clip3( -16.0, 15.0, ((Double)currAC / (Double)refAC) );
        const Int weight     = (Int)( 0.5 + dWeight * (Double)(1<<log2Denom) );
        const Int offset     = (Int)( ((currDC<<log2Denom) - ((Int64)weight * refDC) + (Int64)realOffset) >> realLog2Denom );

        Int clippedOffset;
        if(isChroma(compID)) // Chroma offset range limination
        {
          const Int pred        = ( range - ( ( range*weight)>>(log2Denom) ) );
          const Int deltaOffset = Clip3( -4*range, 4*range-1, (offset - pred) ); // signed 10bit

          clippedOffset = Clip3( -range, range-1, (deltaOffset + pred) );  // signed 8bit
        }
        else // Luma offset range limitation
        {
          clippedOffset = Clip3( -range, range-1, offset);
        }

        // Weighting factor limitation
        const Int defaultWeight = (1<<log2Denom);
        const Int deltaWeight   = (defaultWeight - weight);

        if(deltaWeight >= range || deltaWeight < -range)
          return false;

        m_wp[refList][refIdxTemp][comp].bPresentFlag      = true;
        m_wp[refList][refIdxTemp][comp].iWeight           = weight;
        m_wp[refList][refIdxTemp][comp].iOffset           = clippedOffset;
        m_wp[refList][refIdxTemp][comp].uiLog2WeightDenom = log2Denom;
      }
    }
  }
  return true;
}


/** select whether weighted pred enables or not.
 * \param TComSlice *slice
 * \param log2Denom
 * \returns Bool
 */
Bool WeightPredAnalysis::xSelectWP(TComSlice *const slice, const Int log2Denom)
{
        TComPicYuv *const pPic                                = slice->getPic()->getPicYuvOrg();
  const Int               iDefaultWeight                      = ((Int)1<<log2Denom);
  const Int               iNumPredDir                         = slice->isInterP() ? 1 : 2;
  const Bool              useHighPrecisionPredictionWeighting = slice->getSPS()->getUseHighPrecisionPredictionWeighting();

  assert (iNumPredDir <= Int(NUM_REF_PIC_LIST_01));

  for ( Int iRefList = 0; iRefList < iNumPredDir; iRefList++ )
  {
    const RefPicList eRefPicList = ( iRefList ? REF_PIC_LIST_1 : REF_PIC_LIST_0 );

    for ( Int iRefIdxTemp = 0; iRefIdxTemp < slice->getNumRefIdx(eRefPicList); iRefIdxTemp++ )
    {
      Int64 iSADWP = 0, iSADnoWP = 0;

      for(Int comp=0; comp<pPic->getNumberValidComponents(); comp++)
      {
        const ComponentID  compID     = ComponentID(comp);
              Pel         *pOrg       = pPic->getAddr(compID);
              Pel         *pRef       = slice->getRefPic(eRefPicList, iRefIdxTemp)->getPicYuvRec()->getAddr(compID);
        const Int          iOrgStride = pPic->getStride(compID);
        const Int          iRefStride = slice->getRefPic(eRefPicList, iRefIdxTemp)->getPicYuvRec()->getStride(compID);
        const Int          iWidth     = pPic->getWidth(compID);
        const Int          iHeight    = pPic->getHeight(compID);
        const Int          bitDepth   = g_bitDepth[toChannelType(compID)];

        // calculate SAD costs with/without wp for luma
        iSADWP   += xCalcSADvalueWP(bitDepth, pOrg, pRef, iWidth, iHeight, iOrgStride, iRefStride, log2Denom, m_wp[iRefList][iRefIdxTemp][compID].iWeight, m_wp[iRefList][iRefIdxTemp][compID].iOffset, useHighPrecisionPredictionWeighting);
        iSADnoWP += xCalcSADvalueWP(bitDepth, pOrg, pRef, iWidth, iHeight, iOrgStride, iRefStride, log2Denom, iDefaultWeight, 0, useHighPrecisionPredictionWeighting);
      }

      const Double dRatio = ((Double)iSADWP / (Double)iSADnoWP);
      if(dRatio >= (Double)DTHRESH)
      {
        for(Int comp=0; comp<pPic->getNumberValidComponents(); comp++)
        {
          m_wp[iRefList][iRefIdxTemp][comp].bPresentFlag      = false;
          m_wp[iRefList][iRefIdxTemp][comp].iOffset           = 0;
          m_wp[iRefList][iRefIdxTemp][comp].iWeight           = iDefaultWeight;
          m_wp[iRefList][iRefIdxTemp][comp].uiLog2WeightDenom = log2Denom;
        }
      }
    }
  }

  return true;
}


/** calculate SAD values for both WP version and non-WP version.
 * \param Pel *pOrgPel
 * \param Pel *pRefPel
 * \param Int iWidth
 * \param Int iHeight
 * \param Int iOrgStride
 * \param Int iRefStride
 * \param Int iLog2Denom
 * \param Int iWeight
 * \param Int iOffset
 * \returns Int64
 */
Int64 WeightPredAnalysis::xCalcSADvalueWP(const Int   bitDepth,
                                          const Pel  *pOrgPel,
                                          const Pel  *pRefPel,
                                          const Int   iWidth,
                                          const Int   iHeight,
                                          const Int   iOrgStride,
                                          const Int   iRefStride,
                                          const Int   iLog2Denom,
                                          const Int   iWeight,
                                          const Int   iOffset,
                                          const Bool  useHighPrecisionPredictionWeighting)
{
  const Int64 iSize          = iWidth*iHeight;
  const Int64 iRealLog2Denom = useHighPrecisionPredictionWeighting ? iLog2Denom : (iLog2Denom + (bitDepth - 8));

  Int64 iSAD = 0;
  for( Int y = 0; y < iHeight; y++ )
  {
    for( Int x = 0; x < iWidth; x++ )
    {
      iSAD += ABS(( ((Int64)pOrgPel[x]<<(Int64)iLog2Denom) - ( (Int64)pRefPel[x] * (Int64)iWeight + ((Int64)iOffset<<iRealLog2Denom) ) ) );
    }
    pOrgPel += iOrgStride;
    pRefPel += iRefStride;
  }

  return (iSAD/iSize);
}
