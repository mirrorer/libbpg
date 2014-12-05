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

/** \file     Debug.cpp
    \brief    Defines types and objects for environment-variable-based debugging and feature control
*/

#include "Debug.h"
#include <algorithm>
#include <math.h>
#include "TComDataCU.h"
#include "TComPic.h"
#include "TComYuv.h"

static const UInt settingNameWidth  = 66;
static const UInt settingHelpWidth  = 84;
static const UInt settingValueWidth = 3;

#ifdef DEBUG_STRING
// these strings are used to reorder the debug output so that the encoder and decoder match.
const Char *debug_reorder_data_inter_token[MAX_NUM_COMPONENT+1]
 = {"Start of channel 0 inter debug\n", "Start of channel 1 inter debug\n", "Start of channel 2 inter debug\n", "End of inter residual debug\n"} ;
const Char *partSizeToString[NUMBER_OF_PART_SIZES]={"2Nx2N(0)", "2NxN(1)", "Nx2N(2)", "NxN(3)", "2Nx(N/2+3N/2)(4)", "2Nx(3N/2+N/2)(5)", "(N/2+3N/2)x2N(6)", "(3N/2+N/2)x2N(7)"};
#endif

// --------------------------------------------------------------------------------------------------------------------- //

//EnvVar definition

std::list<std::pair<std::string, std::string> > &EnvVar::getEnvVarList()
{
  static std::list<std::pair<std::string, std::string> > varInfoList;
  return varInfoList;
}

std::list<EnvVar*> &EnvVar::getEnvVarInUse()
{
  static std::list<EnvVar*> varInUseList;
  return varInUseList;
}

static inline Void printPair(const std::pair<std::string, std::string> &p)
{
  if (p.second=="")
  {
    std::cout << "\n" << std::setw(settingNameWidth) << p.first << "\n" << std::endl;
  }
  else
  {
    std::cout << std::setw(settingNameWidth) << p.first << ":   " << p.second << "\n" << std::endl;
  }
}

static inline Void printVal(const EnvVar* env)
{
  std::cout << std::setw(settingNameWidth) << env->getName() << " = " << std::setw(settingValueWidth) << env->getInt() << " (string = " << std::setw(15) << env->getString() << ")" << std::endl;
}

//static inline Bool sameEnvName( const std::pair<std::string, std::string> &a,
//                                const std::pair<std::string, std::string> &b )
//{
//  // only check env name
//  return (a.first==b.first);
//}

Void EnvVar::printEnvVar()
{
//  getEnvVarList().unique(sameEnvName);
  if (getEnvVarList().size()!=0)
  {
    std::cout << "--- Environment variables:\n" << std::endl;
    for_each(getEnvVarList().begin(), getEnvVarList().end(), printPair);
  }
  std::cout << std::endl;
}

Void EnvVar::printEnvVarInUse()
{
  if (getEnvVarInUse().size()!=0)
  {
    std::cout << "RExt Environment variables set as follows: \n" << std::endl;
    for_each(getEnvVarInUse().begin(), getEnvVarInUse().end(), printVal);
  }
  std::cout << std::endl;
}

EnvVar::EnvVar(const std::string &sName, const std::string &sDefault, const std::string &sHelp) :
                                                                m_sName(sName),
                                                                m_sHelp(sHelp),
                                                                m_sVal(),
                                                                m_dVal(0),
                                                                m_iVal(0),
                                                                m_bSet(false)
{
  if (getenv(m_sName.c_str()))
  {
    m_sVal = getenv(m_sName.c_str());
    m_bSet = true;
    getEnvVarInUse().push_back(this);
  }
  else m_sVal = sDefault;

  m_dVal = strtod(m_sVal.c_str(), 0);
  m_iVal = Int(m_dVal);

  getEnvVarList().push_back( std::pair<std::string, std::string>(m_sName, indentNewLines(lineWrap(splitOnSettings(m_sHelp), settingHelpWidth), (settingNameWidth + 4))) );
}


// --------------------------------------------------------------------------------------------------------------------- //

// Debug environment variables:

EnvVar Debug("-- Debugging","","");

EnvVar DebugOptionList::DebugSBAC             ("DEBUG_SBAC",        "0", "Output debug data from SBAC entropy coder (coefficient data etc.)"                              );
EnvVar DebugOptionList::DebugRQT              ("DEBUG_RQT",         "0", "Output RQT debug data from entropy coder"                                                       );
EnvVar DebugOptionList::DebugPred             ("DEBUG_PRED",        "0", "Output prediction debug"                                                                        );
EnvVar DebugOptionList::ForceLumaMode         ("FORCE_LUMA_MODE",   "0", "Force a particular intra direction for Luma (0-34)"                                             );
EnvVar DebugOptionList::ForceChromaMode       ("FORCE_CHROMA_MODE", "0", "Force a particular intra direction for chroma (0-5)"                                            );

#ifdef DEBUG_STRING
EnvVar DebugOptionList::DebugString_Structure ("DEBUG_STRUCTURE",   "0", "Produce output on chosen structure                        bit0=intra, bit1=inter");
EnvVar DebugOptionList::DebugString_Pred      ("DEBUG_PRED",        "0", "Produce output on prediction data.                        bit0=intra, bit1=inter");
EnvVar DebugOptionList::DebugString_Resi      ("DEBUG_RESI",        "0", "Produce output on residual data.                          bit0=intra, bit1=inter");
EnvVar DebugOptionList::DebugString_Reco      ("DEBUG_RECO",        "0", "Produce output on reconstructed data.                     bit0=intra, bit1=inter");
EnvVar DebugOptionList::DebugString_InvTran   ("DEBUG_INV_QT",      "0", "Produce output on inverse-quantiser and transform stages. bit0=intra, bit1=inter");
#endif

// --------------------------------------------------------------------------------------------------------------------- //

//macro value printing function

Void printMacroSettings()
{
  std::cout << "Non-environment-variable-controlled macros set as follows: \n" << std::endl;

  //------------------------------------------------

  //setting macros

  PRINT_CONSTANT(RExt__DECODER_DEBUG_BIT_STATISTICS,                                settingNameWidth, settingValueWidth);
  PRINT_CONSTANT(RExt__HIGH_BIT_DEPTH_SUPPORT,                                      settingNameWidth, settingValueWidth);
  PRINT_CONSTANT(RExt__HIGH_PRECISION_FORWARD_TRANSFORM,                            settingNameWidth, settingValueWidth);

  PRINT_CONSTANT(O0043_BEST_EFFORT_DECODING,                                        settingNameWidth, settingValueWidth);

  PRINT_CONSTANT(RD_TEST_SAO_DISABLE_AT_PICTURE_LEVEL,                              settingNameWidth, settingValueWidth);

  //------------------------------------------------

  std::cout << std::endl;
}


// --------------------------------------------------------------------------------------------------------------------- //

//Debugging

UInt  g_debugCounter  = 0;
Bool  g_printDebug    = false;
Void* g_debugAddr     = NULL;

#ifdef DEBUG_ENCODER_SEARCH_BINS
const UInt debugEncoderSearchBinTargetLine = 0;
const UInt debugEncoderSearchBinWindow     = 1000000;
#endif

#ifdef DEBUG_CABAC_BINS
const UInt debugCabacBinTargetLine = 0;
const UInt debugCabacBinWindow     = 1000000;
#endif

Void printSBACCoeffData(  const UInt          lastX,
                          const UInt          lastY,
                          const UInt          width,
                          const UInt          height,
                          const UInt          chan,
                          const UInt          absPart,
                          const UInt          scanIdx,
                          const TCoeff *const pCoeff,
                          const Bool          finalEncode
                        )
{
  if (DebugOptionList::DebugSBAC.getInt()!=0 && finalEncode)
  {
    std::cout << "Size: " << width << "x" << height << ", Last X/Y: (" << lastX << ", " << lastY << "), absPartIdx: " << absPart << ", scanIdx: " << scanIdx << ", chan: " << chan << std::endl;
    for (Int i=0; i<width*height; i++)
    {
      std::cout << std::setw(3) << pCoeff[i];// + dcVal;
      if (i%width == width-1) std::cout << std::endl;
      else                    std::cout << ",";
    }
    std::cout << std::endl;
  }
}

Void printCbfArray( TComDataCU* pcCU  )
{
  const UInt CUSizeInParts = pcCU->getWidth(0)/4;
  const UInt numValidComp=pcCU->getPic()->getNumberValidComponents();
  for (UInt ch=0; ch<numValidComp; ch++)
  {
    const ComponentID compID=ComponentID(ch);
    printf("channel: %d\n", ch);
    for (Int y=0; y<CUSizeInParts; y++)
    {
      for (Int x=0; x<CUSizeInParts; x++)
      {
        printf(x+1==CUSizeInParts?"%3d\n":"%3d, ", pcCU->getCbf(compID)[g_auiRasterToZscan[y*CUSizeInParts + x]]);
      }
    }
  }
}

UInt getDecimalWidth(const Double value)
{
  return (value == 0) ? 1 : (UInt(floor(log10(fabs(value)))) + ((value < 0) ? 2 : 1));
                                                               //for the minus sign
}

UInt getZScanIndex(const UInt x, const UInt y)
{
  UInt remainingX = x;
  UInt remainingY = y;
  UInt offset     = 0;
  UInt result     = 0;

  while ((remainingX != 0) || (remainingY != 0))
  {
    result |= ((remainingX & 0x1) << offset) | ((remainingY & 0x1) << (offset + 1));

    remainingX >>= 1;
    remainingY >>= 1;
    offset      += 2;
  }

  return result;
}


// --------------------------------------------------------------------------------------------------------------------- //

//String manipulation functions for aligning and wrapping printed text


std::string splitOnSettings(const std::string &input)
{
  std::string result = input;

  std::string::size_type searchFromPosition = 0;

  while (searchFromPosition < result.length())
  {
    //find the " = " that is used to define each setting
    std::string::size_type equalsPosition = result.find(" = ", searchFromPosition);

    if (equalsPosition == std::string::npos) break;

    //then find the end of the numeric characters
    std::string::size_type splitPosition = result.find_last_of("1234567890", equalsPosition);

    //then find the last space before the first numeric character...
    if (splitPosition != std::string::npos) splitPosition = result.find_last_of(' ', splitPosition);

    //...and replace it with a new line
    if (splitPosition != std::string::npos) result.replace(splitPosition, 1, 1, '\n');

    //start the next search from the end of the " = " string
    searchFromPosition = (equalsPosition + 3);
  }

  return result;
}


std::string lineWrap(const std::string &input, const UInt maximumLineLength)
{
  if (maximumLineLength == 0) return input;
  std::string result = input;

  std::string::size_type lineStartPosition = result.find_first_not_of(' '); //don't wrap any leading spaces in the string

  while (lineStartPosition != std::string::npos)
  {
    //------------------------------------------------

    const std::string::size_type searchFromPosition = lineStartPosition + maximumLineLength;

    if (searchFromPosition >= result.length()) break;

    //------------------------------------------------

    //first check to see if there is another new line character before the maximum line length
    //we can't use find for this unfortunately because it doesn't take both a beginning and an end for its search range
    std::string::size_type nextLineStartPosition = std::string::npos;
    for (std::string::size_type currentPosition = lineStartPosition; currentPosition <= searchFromPosition; currentPosition++)
    {
      if (result[currentPosition] == '\n') { nextLineStartPosition = currentPosition + 1; break; }
    }

    //------------------------------------------------

    //if there ia another new line character before the maximum line length, we need to start this loop again from that position
    if (nextLineStartPosition != std::string::npos) lineStartPosition = nextLineStartPosition;
    else
    {
      std::string::size_type spacePosition = std::string::npos;

      //search backwards for the last space character (must use signed Int because lineStartPosition can be 0)
      for (Int currentPosition = Int(searchFromPosition); currentPosition >= Int(lineStartPosition); currentPosition--)
      {
        if (result[currentPosition] == ' ') { spacePosition = currentPosition; break; }
      }

      //if we didn't find a space searching backwards, we must hyphenate
      if (spacePosition == std::string::npos)
      {
        result.insert(searchFromPosition, "-\n");
        lineStartPosition = searchFromPosition + 2; //make sure the next search ignores the hyphen
      }
      else //if we found a space to split on, replace it with a new line character
      {
        result.replace(spacePosition, 1, 1, '\n');
        lineStartPosition = spacePosition + 1;
      }
    }

    //------------------------------------------------
  }

  return result;
}


std::string indentNewLines(const std::string &input, const UInt indentBy)
{
  std::string result = input;

  const std::string indentString(indentBy, ' ');
  std::string::size_type offset = 0;

  while ((offset = result.find('\n', offset)) != std::string::npos)
  {
    if ((++offset) >= result.length()) break; //increment offset so we don't find the same \n again and do no indentation at the end
    result.insert(offset, indentString);
  }

  return result;
}


// --------------------------------------------------------------------------------------------------------------------- //


Void printBlockToStream( std::ostream &ss, const Char *pLinePrefix, TComYuv &src, const UInt numSubBlocksAcross, const UInt numSubBlocksUp, const UInt defWidth )
{
  const UInt numValidComp=src.getNumberValidComponents();

  for (UInt ch=0; ch<numValidComp ; ch++)
  {
    const ComponentID compID = ComponentID(ch);
    const UInt width  = src.getWidth(compID);
    const UInt height = src.getHeight(compID);
    const UInt stride = src.getStride(compID);
    const Pel* blkSrc = src.getAddr(compID);
    const UInt subBlockWidth=width/numSubBlocksAcross;
    const UInt subBlockHeight=height/numSubBlocksUp;

    ss << pLinePrefix << " compID: " << compID << "\n";
    for (UInt y=0; y<height; y++)
    {
      if ((y%subBlockHeight)==0 && y!=0)
        ss << pLinePrefix << '\n';

      ss << pLinePrefix;
      for (UInt x=0; x<width; x++)
      {
        if ((x%subBlockWidth)==0 && x!=0)
          ss << std::setw(defWidth+2) << "";

        ss << std::setw(defWidth) << blkSrc[y*stride + x] << ' ';
      }
      ss << '\n';
    }
    ss << pLinePrefix << " --- \n";
  }
}

#ifdef DEBUG_STRING
Int DebugStringGetPredModeMask(PredMode mode)
{
  return (mode==MODE_INTRA)?1:2;
}

Void DebugInterPredResiReco(std::string &sDebug, TComYuv &pred, TComYuv &resi, TComYuv &reco, Int predmode_mask)
{
  if (DebugOptionList::DebugString_Pred.getInt()&predmode_mask)
  {
    std::stringstream ss(std::stringstream::out);
    printBlockToStream(ss, "###inter-pred: ", pred);
    std::string debugTmp;
    debugTmp=ss.str();
    sDebug=debugTmp+sDebug;
  }
  if (DebugOptionList::DebugString_Resi.getInt()&predmode_mask)
  {
    std::stringstream ss(std::stringstream::out);
    printBlockToStream(ss, "###inter-resi: ", resi);
    sDebug+=ss.str();
  }
  if (DebugOptionList::DebugString_Reco.getInt()&predmode_mask)
  {
    std::stringstream ss(std::stringstream::out);
    printBlockToStream(ss, "###inter-reco: ", reco);
    sDebug+=ss.str();
  }
}
#endif
