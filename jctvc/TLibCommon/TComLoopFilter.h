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

/** \file     TComLoopFilter.h
    \brief    deblocking filter (header)
*/

#ifndef __TCOMLOOPFILTER__
#define __TCOMLOOPFILTER__

#include "CommonDef.h"
#include "TComPic.h"

//! \ingroup TLibCommon
//! \{

#define DEBLOCK_SMALLEST_BLOCK  8

// ====================================================================================================================
// Class definition
// ====================================================================================================================

/// deblocking filter class
class TComLoopFilter
{
private:

  UInt      m_uiNumPartitions;
  UChar*    m_aapucBS[NUM_EDGE_DIR];         ///< Bs for [Ver/Hor][Y/U/V][Blk_Idx]
  Bool*     m_aapbEdgeFilter[NUM_EDGE_DIR];
  LFCUParam m_stLFCUParam;                   ///< status structure

  Bool      m_bLFCrossTileBoundary;

protected:
  /// CU-level deblocking function
  Void xDeblockCU                 ( TComDataCU* pcCU, UInt uiAbsZorderIdx, UInt uiDepth, DeblockEdgeDir edgeDir );

  // set / get functions
  Void xSetLoopfilterParam        ( TComDataCU* pcCU, UInt uiAbsZorderIdx );
  // filtering functions
  Void xSetEdgefilterTU           ( TComTU &rTu );
  Void xSetEdgefilterPU           ( TComDataCU* pcCU, UInt uiAbsZorderIdx );
  Void xGetBoundaryStrengthSingle ( TComDataCU* pCtu, DeblockEdgeDir edgeDir, UInt uiPartIdx );
  UInt xCalcBsIdx                 ( TComDataCU* pcCU, UInt absZIdxInCtu, DeblockEdgeDir edgeDir, Int iEdgeIdx, Int iBaseUnitIdx, const struct TComRectangle *rect=NULL )
  {
    TComPic* const pcPic = pcCU->getPic();
    const UInt ctuWidthInBaseUnits = pcPic->getNumPartInCtuWidth();
    Int rasterOffsetTU=0;
    if (rect != NULL)
    {
      const UInt minCuWidth =pcPic->getMinCUWidth();
      const UInt minCuHeight=pcPic->getMinCUHeight();
      rasterOffsetTU = rect->x0/minCuWidth + (rect->y0/minCuHeight)*ctuWidthInBaseUnits;
    }
    if( edgeDir == EDGE_VER )
    {
      return g_auiRasterToZscan[g_auiZscanToRaster[absZIdxInCtu] + iBaseUnitIdx * ctuWidthInBaseUnits + iEdgeIdx + rasterOffsetTU ];
    }
    else
    {
      return g_auiRasterToZscan[g_auiZscanToRaster[absZIdxInCtu] + iEdgeIdx * ctuWidthInBaseUnits + iBaseUnitIdx + rasterOffsetTU ];
    }
  }

  Void xSetEdgefilterMultiple( TComDataCU* pcCU,
                               UInt uiAbsZorderIdx,
                               UInt uiDepth,
                               DeblockEdgeDir edgeDir,
                               Int iEdgeIdx,
                               Bool bValue,
                               UInt uiWidthInBaseUnits = 0,
                               UInt uiHeightInBaseUnits = 0,
                               const TComRectangle *rect = 0
                               );

  Void xEdgeFilterLuma            ( TComDataCU* pcCU, UInt uiAbsZorderIdx, UInt uiDepth, DeblockEdgeDir edgeDir, Int iEdge );
  Void xEdgeFilterChroma          ( TComDataCU* pcCU, UInt uiAbsZorderIdx, UInt uiDepth, DeblockEdgeDir edgeDir, Int iEdge );

  __inline Void xPelFilterLuma( Pel* piSrc, Int iOffset, Int tc, Bool sw, Bool bPartPNoFilter, Bool bPartQNoFilter, Int iThrCut, Bool bFilterSecondP, Bool bFilterSecondQ);
  __inline Void xPelFilterChroma( Pel* piSrc, Int iOffset, Int tc, Bool bPartPNoFilter, Bool bPartQNoFilter);


  __inline Bool xUseStrongFiltering( Int offset, Int d, Int beta, Int tc, Pel* piSrc);
  __inline Int xCalcDP( Pel* piSrc, Int iOffset);
  __inline Int xCalcDQ( Pel* piSrc, Int iOffset);

  static const UChar sm_tcTable[54];
  static const UChar sm_betaTable[52];

public:
  TComLoopFilter();
  virtual ~TComLoopFilter();

  Void  create                    ( UInt uiMaxCUDepth );
  Void  destroy                   ();

  /// set configuration
  Void setCfg( Bool bLFCrossTileBoundary );

  /// picture-level deblocking filter
  Void loopFilterPic( TComPic* pcPic );

  static Int getBeta( Int qp )
  {
    Int indexB = Clip3( 0, MAX_QP, qp );
    return sm_betaTable[ indexB ];
  }
};

//! \}

#endif
