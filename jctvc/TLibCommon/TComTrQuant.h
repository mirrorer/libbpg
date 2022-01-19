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

/** \file     TComTrQuant.h
    \brief    transform and quantization class (header)
*/

#ifndef __TCOMTRQUANT__
#define __TCOMTRQUANT__

#include "CommonDef.h"
#include "TComYuv.h"
#include "TComDataCU.h"
#include "TComChromaFormat.h"
#include "ContextTables.h"

//! \ingroup TLibCommon
//! \{

// ====================================================================================================================
// Constants
// ====================================================================================================================

#define QP_BITS                 15

// ====================================================================================================================
// Type definition
// ====================================================================================================================

typedef struct
{
  Int significantCoeffGroupBits[NUM_SIG_CG_FLAG_CTX][2 /*Flag = [0|1]*/];
  Int significantBits[NUM_SIG_FLAG_CTX][2 /*Flag = [0|1]*/];
  Int lastXBits[MAX_NUM_CHANNEL_TYPE][LAST_SIGNIFICANT_GROUPS];
  Int lastYBits[MAX_NUM_CHANNEL_TYPE][LAST_SIGNIFICANT_GROUPS];
  Int m_greaterOneBits[NUM_ONE_FLAG_CTX][2 /*Flag = [0|1]*/];
  Int m_levelAbsBits[NUM_ABS_FLAG_CTX][2 /*Flag = [0|1]*/];

  Int blockCbpBits[NUM_QT_CBF_CTX_SETS * NUM_QT_CBF_CTX_PER_SET][2 /*Flag = [0|1]*/];
  Int blockRootCbpBits[4][2 /*Flag = [0|1]*/];

  Int golombRiceAdaptationStatistics[RExt__GOLOMB_RICE_ADAPTATION_STATISTICS_SETS];
} estBitsSbacStruct;

// ====================================================================================================================
// Class definition
// ====================================================================================================================

/// QP struct
struct QpParam
{
  Int Qp;
  Int per;
  Int rem;

  QpParam(const Int           qpy,
          const ChannelType   chType,
          const Int           qpBdOffset,
          const Int           chromaQPOffset,
          const ChromaFormat  chFmt );

  QpParam(const TComDataCU   &cu, const ComponentID compID);

}; // END STRUCT DEFINITION QpParam


/// transform and quantization class
class TComTrQuant
{
public:
  TComTrQuant();
  ~TComTrQuant();

  // initialize class
  Void init                 ( UInt  uiMaxTrSize,
                              Bool useRDOQ                = false,
                              Bool useRDOQTS              = false,
                              Bool bEnc                   = false,
                              Bool useTransformSkipFast   = false
#if ADAPTIVE_QP_SELECTION
                            , Bool bUseAdaptQpSelect      = false
#endif
                              );

  // transform & inverse transform functions
  Void transformNxN(       TComTU         & rTu,
                     const ComponentID      compID,
                           Pel           *  pcResidual,
                     const UInt             uiStride,
                           TCoeff        *  rpcCoeff,
#if ADAPTIVE_QP_SELECTION
                           TCoeff        * rpcArlCoeff,
#endif
                           TCoeff         & uiAbsSum,
                     const QpParam        & cQP
                    );


  Void invTransformNxN(      TComTU       & rTu,
                       const ComponentID    compID,
                             Pel         *pcResidual,
                       const UInt           uiStride,
                             TCoeff      *  pcCoeff,
                       const QpParam      & cQP
                             DEBUG_STRING_FN_DECLAREP(psDebug));

  Void invRecurTransformNxN ( const ComponentID compID, TComYuv *pResidual, TComTU &rTu );

  Void rdpcmNxN   ( TComTU& rTu, const ComponentID compID, Pel* pcResidual, const UInt uiStride, const QpParam& cQP, TCoeff* pcCoeff, TCoeff &uiAbsSum, RDPCMMode& rdpcmMode );
  Void invRdpcmNxN( TComTU& rTu, const ComponentID compID, Pel* pcResidual, const UInt uiStride );

  Void applyForwardRDPCM( TComTU& rTu, const ComponentID compID, Pel* pcResidual, const UInt uiStride, const QpParam& cQP, TCoeff* pcCoeff, TCoeff &uiAbsSum, const RDPCMMode mode );

  // Misc functions

#if RDOQ_CHROMA_LAMBDA
  Void setLambdas(const Double lambdas[MAX_NUM_COMPONENT]) { for (UInt component = 0; component < MAX_NUM_COMPONENT; component++) m_lambdas[component] = lambdas[component]; }
  Void selectLambda(const ComponentID compIdx) { m_dLambda = m_lambdas[compIdx]; }
#else
  Void setLambda(Double dLambda) { m_dLambda = dLambda;}
#endif
  Void setRDOQOffset( UInt uiRDOQOffset ) { m_uiRDOQOffset = uiRDOQOffset; }

  estBitsSbacStruct* m_pcEstBitsSbac;

  static Int      calcPatternSigCtx( const UInt* sigCoeffGroupFlag, UInt uiCGPosX, UInt uiCGPosY, UInt widthInGroups, UInt heightInGroups );

  static Int      getSigCtxInc     ( Int                              patternSigCtx,
                                     const TUEntropyCodingParameters &codingParameters,
                                     const Int                        scanPosition,
                                     const Int                        log2BlockWidth,
                                     const Int                        log2BlockHeight,
                                     const ChannelType                chanType
                                    );

  static UInt getSigCoeffGroupCtxInc  (const UInt*  uiSigCoeffGroupFlag,
                                       const UInt   uiCGPosX,
                                       const UInt   uiCGPosY,
                                       const UInt   widthInGroups,
                                       const UInt   heightInGroups);

  Void initScalingList                      ();
  Void destroyScalingList                   ();
  Void setErrScaleCoeff    ( UInt list, UInt size, Int qp );
  Double* getErrScaleCoeff              ( UInt list, UInt size, Int qp ) { return m_errScale             [size][list][qp]; };  //!< get Error Scale Coefficent
  Double& getErrScaleCoeffNoScalingList ( UInt list, UInt size, Int qp ) { return m_errScaleNoScalingList[size][list][qp]; };  //!< get Error Scale Coefficent
  Int* getQuantCoeff                    ( UInt list, Int qp, UInt size ) { return m_quantCoef            [size][list][qp]; };  //!< get Quant Coefficent
  Int* getDequantCoeff                  ( UInt list, Int qp, UInt size ) { return m_dequantCoef          [size][list][qp]; };  //!< get DeQuant Coefficent
  Void setUseScalingList   ( Bool bUseScalingList){ m_scalingListEnabledFlag = bUseScalingList; };
  Bool getUseScalingList   (const UInt width, const UInt height, const Bool isTransformSkip){ return m_scalingListEnabledFlag && (!isTransformSkip || ((width == 4) && (height == 4))); };
  Void setFlatScalingList  (const ChromaFormat format);
  Void xsetFlatScalingList ( UInt list, UInt size, Int qp, const ChromaFormat format);
  Void xSetScalingListEnc  ( TComScalingList *scalingList, UInt list, UInt size, Int qp, const ChromaFormat format);
  Void xSetScalingListDec  ( TComScalingList *scalingList, UInt list, UInt size, Int qp, const ChromaFormat format);
  Void setScalingList      ( TComScalingList *scalingList, const ChromaFormat format);
  Void setScalingListDec   ( TComScalingList *scalingList, const ChromaFormat format);
  Void processScalingListEnc( Int *coeff, Int *quantcoeff, Int quantScales, UInt height, UInt width, UInt ratio, Int sizuNum, UInt dc);
  Void processScalingListDec( Int *coeff, Int *dequantcoeff, Int invQuantScales, UInt height, UInt width, UInt ratio, Int sizuNum, UInt dc);
#if ADAPTIVE_QP_SELECTION
  Void    initSliceQpDelta() ;
  Void    storeSliceQpNext(TComSlice* pcSlice);
  Void    clearSliceARLCnt();
  Int     getQpDelta(Int qp) { return m_qpDelta[qp]; }
  Int*    getSliceNSamples(){ return m_sliceNsamples ;}
  Double* getSliceSumC()    { return m_sliceSumC; }
#endif
  Void transformSkipQuantOneSample(TComTU &rTu, const ComponentID compID, const Pel resiDiff, TCoeff* pcCoeff, const UInt uiPos, const QpParam &cQP, const Bool bUseHalfRoundingPoint);
  Void invTrSkipDeQuantOneSample(TComTU &rTu, ComponentID compID, TCoeff pcCoeff, Pel &reconSample, const QpParam &cQP, UInt uiPos );

protected:
#if ADAPTIVE_QP_SELECTION
  Int     m_qpDelta[MAX_QP+1];
  Int     m_sliceNsamples[LEVEL_RANGE+1];
  Double  m_sliceSumC[LEVEL_RANGE+1] ;
#endif
  TCoeff* m_plTempCoeff;

//  QpParam  m_cQP; - removed - placed on the stack.
#if RDOQ_CHROMA_LAMBDA
  Double   m_lambdas[MAX_NUM_COMPONENT];
#endif
  Double   m_dLambda;
  UInt     m_uiRDOQOffset;
  UInt     m_uiMaxTrSize;
  Bool     m_bEnc;
  Bool     m_useRDOQ;
  Bool     m_useRDOQTS;
#if ADAPTIVE_QP_SELECTION
  Bool     m_bUseAdaptQpSelect;
#endif
  Bool     m_useTransformSkipFast;

  Bool     m_scalingListEnabledFlag;

  Int      *m_quantCoef            [SCALING_LIST_SIZE_NUM][SCALING_LIST_NUM][SCALING_LIST_REM_NUM]; ///< array of quantization matrix coefficient 4x4
  Int      *m_dequantCoef          [SCALING_LIST_SIZE_NUM][SCALING_LIST_NUM][SCALING_LIST_REM_NUM]; ///< array of dequantization matrix coefficient 4x4
  Double   *m_errScale             [SCALING_LIST_SIZE_NUM][SCALING_LIST_NUM][SCALING_LIST_REM_NUM]; ///< array of quantization matrix coefficient 4x4
  Double    m_errScaleNoScalingList[SCALING_LIST_SIZE_NUM][SCALING_LIST_NUM][SCALING_LIST_REM_NUM]; ///< array of quantization matrix coefficient 4x4

private:
  // forward Transform
  Void xT   ( const ComponentID compID, Bool useDST, Pel* piBlkResi, UInt uiStride, TCoeff* psCoeff, Int iWidth, Int iHeight );

  // skipping Transform
  Void xTransformSkip ( Pel* piBlkResi, UInt uiStride, TCoeff* psCoeff, TComTU &rTu, const ComponentID component );

  Void signBitHidingHDQ( const ComponentID compID, TCoeff* pQCoef, TCoeff* pCoef, TCoeff* deltaU, const TUEntropyCodingParameters &codingParameters );

  // quantization
  Void xQuant(       TComTU       &rTu,
                     TCoeff      * pSrc,
                     TCoeff      * pDes,
#if ADAPTIVE_QP_SELECTION
                     TCoeff      *pArlDes,
#endif
                     TCoeff       &uiAbsSum,
               const ComponentID   compID,
               const QpParam      &cQP );

  // RDOQ functions

  Void           xRateDistOptQuant (       TComTU       &rTu,
                                           TCoeff      * plSrcCoeff,
                                           TCoeff      * piDstCoeff,
#if ADAPTIVE_QP_SELECTION
                                           TCoeff      *piArlDstCoeff,
#endif
                                           TCoeff       &uiAbsSum,
                                     const ComponentID   compID,
                                     const QpParam      &cQP );

__inline UInt              xGetCodedLevel  ( Double&          rd64CodedCost,
                                             Double&          rd64CodedCost0,
                                             Double&          rd64CodedCostSig,
                                             Intermediate_Int lLevelDouble,
                                             UInt             uiMaxAbsLevel,
                                             UShort           ui16CtxNumSig,
                                             UShort           ui16CtxNumOne,
                                             UShort           ui16CtxNumAbs,
                                             UShort           ui16AbsGoRice,
                                             UInt             c1Idx,
                                             UInt             c2Idx,
                                             Int              iQBits,
                                             Double           errorScale,
                                             Bool             bLast,
                                             Bool             useLimitedPrefixLength,
                                             ChannelType      channelType
                                             ) const;


  __inline Int xGetICRate  ( UInt   uiAbsLevel,
                             UShort ui16CtxNumOne,
                             UShort ui16CtxNumAbs,
                             UShort ui16AbsGoRice,
                             UInt   c1Idx,
                             UInt   c2Idx,
                             Bool   useLimitedPrefixLength,
                             ChannelType channelType
                           ) const;

  __inline Double xGetRateLast         ( const UInt uiPosX, const UInt uiPosY, const ComponentID component ) const;
  __inline Double xGetRateSigCoeffGroup( UShort uiSignificanceCoeffGroup, UShort ui16CtxNumSig             ) const;
  __inline Double xGetRateSigCoef      ( UShort uiSignificance,           UShort ui16CtxNumSig             ) const;
  __inline Double xGetICost            ( Double dRate                                                      ) const;
  __inline Double xGetIEPRate          (                                                                   ) const;


  // dequantization
  Void xDeQuant(       TComTU       &rTu,
                 const TCoeff      * pSrc,
                       TCoeff      * pDes,
                 const ComponentID   compID,
                 const QpParam      &cQP );

  // inverse transform
  Void xIT    ( const ComponentID compID, Bool useDST, TCoeff* plCoef, Pel* pResidual, UInt uiStride, Int iWidth, Int iHeight );

  // inverse skipping transform
  Void xITransformSkip ( TCoeff* plCoef, Pel* pResidual, UInt uiStride, TComTU &rTu, const ComponentID component );

public:
  static Void crossComponentPrediction(      TComTU      &rTu,
                                       const ComponentID  compID,
                                       const Pel         *piResiL,
                                       const Pel         *piResiC,
                                             Pel         *piResiT,
                                       const Int          width,
                                       const Int          height,
                                       const Int          strideL,
                                       const Int          strideC,
                                       const Int          strideT,
                                       const Bool         reverse);

};// END CLASS DEFINITION TComTrQuant

//! \}

#endif // __TCOMTRQUANT__
