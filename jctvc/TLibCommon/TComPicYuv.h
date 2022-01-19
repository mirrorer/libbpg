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

/** \file     TComPicYuv.h
    \brief    picture YUV buffer class (header)
*/

#ifndef __TCOMPICYUV__
#define __TCOMPICYUV__

#include <stdio.h>
#include "CommonDef.h"
#include "TComRom.h"
#include "TComChromaFormat.h"
#include "SEI.h"

//! \ingroup TLibCommon
//! \{

// ====================================================================================================================
// Class definition
// ====================================================================================================================

/// picture YUV buffer class
class TComPicYuv
{
private:

  // ------------------------------------------------------------------------------------------------
  //  YUV buffer
  // ------------------------------------------------------------------------------------------------

  Pel*  m_apiPicBuf[MAX_NUM_COMPONENT];             ///< Buffer (including margin)

  Pel*  m_piPicOrg[MAX_NUM_COMPONENT];              ///< m_apiPicBufY + m_iMarginLuma*getStride() + m_iMarginLuma

  // ------------------------------------------------------------------------------------------------
  //  Parameter for general YUV buffer usage
  // ------------------------------------------------------------------------------------------------

  Int   m_iPicWidth;                                ///< Width of picture in pixels
  Int   m_iPicHeight;                               ///< Height of picture in pixels
  ChromaFormat m_chromaFormatIDC;                   ///< Chroma Format

  Int*  m_ctuOffsetInBuffer[MAX_NUM_CHANNEL_TYPE];  ///< Gives an offset in the buffer for a given CTU (and channel)
  Int*  m_subCuOffsetInBuffer[MAX_NUM_CHANNEL_TYPE];///< Gives an offset in the buffer for a given sub-CU (and channel), relative to start of CTU

  Int   m_iMarginX;                                 ///< margin of Luma channel (chroma's may be smaller, depending on ratio)
  Int   m_iMarginY;                                 ///< margin of Luma channel (chroma's may be smaller, depending on ratio)

  Bool  m_bIsBorderExtended;

public:
               TComPicYuv         ();
  virtual     ~TComPicYuv         ();

  // ------------------------------------------------------------------------------------------------
  //  Memory management
  // ------------------------------------------------------------------------------------------------

  Void          create            (const Int iPicWidth,
                                   const Int iPicHeight,
                                   const ChromaFormat chromaFormatIDC,
                                   const UInt uiMaxCUWidth,
                                   const UInt uiMaxCUHeight,
                                   const UInt uiMaxCUDepth );

  Void          destroy           ();

  // The following have been removed - Use CHROMA_400 in the above function call.
  //Void  createLuma  ( Int iPicWidth, Int iPicHeight, UInt uiMaxCUWidth, UInt uiMaxCUHeight, UInt uhMaxCUDepth );
  //Void  destroyLuma ();

  // ------------------------------------------------------------------------------------------------
  //  Get information of picture
  // ------------------------------------------------------------------------------------------------

  Int           getWidth          (const ComponentID id) const { return  m_iPicWidth >> getComponentScaleX(id);   }
  Int           getHeight         (const ComponentID id) const { return  m_iPicHeight >> getComponentScaleY(id);  }
  ChromaFormat  getChromaFormat   ()                     const { return m_chromaFormatIDC; }
  UInt          getNumberValidComponents() const { return ::getNumberValidComponents(m_chromaFormatIDC); }

  Int           getStride         (const ComponentID id) const { return ((m_iPicWidth     ) + (m_iMarginX  <<1)) >> getComponentScaleX(id); }
  Int           getTotalHeight    (const ComponentID id) const { return ((m_iPicHeight    ) + (m_iMarginY  <<1)) >> getComponentScaleY(id); }

  Int           getMarginX        (const ComponentID id) const { return m_iMarginX >> getComponentScaleX(id);  }
  Int           getMarginY        (const ComponentID id) const { return m_iMarginY >> getComponentScaleY(id);  }

  // ------------------------------------------------------------------------------------------------
  //  Access function for picture buffer
  // ------------------------------------------------------------------------------------------------

  //  Access starting position of picture buffer with margin
  Pel*          getBuf            (const ComponentID ch)       { return  m_apiPicBuf[ch];   }

  //  Access starting position of original picture
  Pel*          getAddr           (const ComponentID ch)       { return  m_piPicOrg[ch];   }
  const Pel*    getAddr           (const ComponentID ch) const { return  m_piPicOrg[ch];   }

  //  Access starting position of original picture for specific coding unit (CU) or partition unit (PU)
  Pel*          getAddr           (const ComponentID ch, const Int ctuRSAddr )       { return m_piPicOrg[ch] + m_ctuOffsetInBuffer[ch==0?0:1][ ctuRSAddr ]; }
  const Pel*    getAddr           (const ComponentID ch, const Int ctuRSAddr ) const { return m_piPicOrg[ch] + m_ctuOffsetInBuffer[ch==0?0:1][ ctuRSAddr ]; }
  Pel*          getAddr           (const ComponentID ch, const Int ctuRSAddr, const Int uiAbsZorderIdx )
                                     { return m_piPicOrg[ch] + m_ctuOffsetInBuffer[ch==0?0:1][ctuRSAddr] + m_subCuOffsetInBuffer[ch==0?0:1][g_auiZscanToRaster[uiAbsZorderIdx]]; }
  const Pel*    getAddr           (const ComponentID ch, const Int ctuRSAddr, const Int uiAbsZorderIdx ) const
                                     { return m_piPicOrg[ch] + m_ctuOffsetInBuffer[ch==0?0:1][ctuRSAddr] + m_subCuOffsetInBuffer[ch==0?0:1][g_auiZscanToRaster[uiAbsZorderIdx]]; }

  UInt          getComponentScaleX(const ComponentID id) const { return ::getComponentScaleX(id, m_chromaFormatIDC); }
  UInt          getComponentScaleY(const ComponentID id) const { return ::getComponentScaleY(id, m_chromaFormatIDC); }

  // ------------------------------------------------------------------------------------------------
  //  Miscellaneous
  // ------------------------------------------------------------------------------------------------

  //  Copy function to picture
  Void          copyToPic         ( TComPicYuv*  pcPicYuvDst ) const ;

  //  Extend function of picture buffer
  Void          extendPicBorder   ();

  //  Dump picture
  Void          dump              (const Char* pFileName, Bool bAdd = false) const ;

  // Set border extension flag
  Void          setBorderExtension(Bool b) { m_bIsBorderExtended = b; }
};// END CLASS DEFINITION TComPicYuv


// These functions now return the length of the digest strings.
UInt calcChecksum(const TComPicYuv& pic, TComDigest &digest);
UInt calcCRC     (const TComPicYuv& pic, TComDigest &digest);
UInt calcMD5     (const TComPicYuv& pic, TComDigest &digest);
std::string digestToString(const TComDigest &digest, Int numChar);
//! \}

#endif // __TCOMPICYUV__
