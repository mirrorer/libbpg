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

/** \file     Debug.h
    \brief    Defines types and objects for environment-variable-based debugging and feature control
*/

#ifndef __DEBUG__
#define __DEBUG__

#include <iostream>
#include <iomanip>
#include <string>
#include <list>
#include <stdlib.h>
#include <sstream>
#include <TLibCommon/CommonDef.h>

#ifdef DEBUG_STRING
extern const Char *debug_reorder_data_inter_token[MAX_NUM_COMPONENT+1];
extern const Char *partSizeToString[NUMBER_OF_PART_SIZES];
#endif

// ---------------------------------------------------------------------------------------------- //

//constant print-out macro

#define PRINT_CONSTANT(NAME, NAME_WIDTH, VALUE_WIDTH) std::cout << std::setw(NAME_WIDTH) << #NAME << " = " << std::setw(VALUE_WIDTH) << NAME << std::endl;

// ---------------------------------------------------------------------------------------------- //

// ---- Environment variables for test/debug ---- //

class EnvVar
{
private:
  std::string m_sName;
  std::string m_sHelp;
  std::string m_sVal;
  Double      m_dVal;
  Int         m_iVal;
  Bool        m_bSet;

public:

  static std::list< std::pair<std::string, std::string> > &getEnvVarList();
  static std::list<EnvVar*>                               &getEnvVarInUse();
  static Void printEnvVar();
  static Void printEnvVarInUse();

  EnvVar(const std::string &sName, const std::string &sDefault, const std::string &sHelp);

  Double              getDouble()   const       { return m_dVal;    }
  Int                 getInt()      const       { return m_iVal;    }
  const std::string  &getString()   const       { return m_sVal;    }
  Bool                isSet()       const       { return m_bSet;    }
  Bool                isTrue()      const       { return m_iVal!=0; }
  const std::string  &getName()     const       { return m_sName;   }

};


// ---------------------------------------------------------------------------------------------- //

// ---- Control switches for debugging and feature control ---- //

namespace DebugOptionList
{
  extern EnvVar DebugSBAC;
  extern EnvVar DebugRQT;
  extern EnvVar DebugPred;
  extern EnvVar ForceLumaMode;
  extern EnvVar ForceChromaMode;

#ifdef DEBUG_STRING
  extern EnvVar DebugString_Structure;
  extern EnvVar DebugString_Pred;
  extern EnvVar DebugString_Resi;
  extern EnvVar DebugString_Reco;
  extern EnvVar DebugString_InvTran;
#endif
}

// ---------------------------------------------------------------------------------------------- //

Void printMacroSettings();

// ---------------------------------------------------------------------------------------------- //

//Debugging

extern Bool g_bFinalEncode;
extern UInt g_debugCounter;
extern Bool g_printDebug;
extern Void* g_debugAddr;

#ifdef DEBUG_ENCODER_SEARCH_BINS
extern const UInt debugEncoderSearchBinTargetLine;
extern const UInt debugEncoderSearchBinWindow;
#endif

#ifdef DEBUG_CABAC_BINS
extern const UInt debugCabacBinTargetLine;
extern const UInt debugCabacBinWindow;
#endif


Void printSBACCoeffData(  const UInt          lastX,
                          const UInt          lastY,
                          const UInt          width,
                          const UInt          height,
                          const UInt          chan,
                          const UInt          absPart,
                          const UInt          scanIdx,
                          const TCoeff *const pCoeff,
                          const Bool          finalEncode=true
                        );


Void printCbfArray( class TComDataCU* pcCU  );

UInt getDecimalWidth(const Double value);
UInt getZScanIndex(const UInt x, const UInt y);

//template specialisation for Char types to get it to render as a number
template <typename ValueType> inline Void writeValueToStream       (const ValueType &value, std::ostream &stream, const UInt outputWidth) { stream << std::setw(outputWidth) <<      value;  }
template <>                   inline Void writeValueToStream<Char >(const Char      &value, std::ostream &stream, const UInt outputWidth) { stream << std::setw(outputWidth) <<  Int(value); }
template <>                   inline Void writeValueToStream<UChar>(const UChar     &value, std::ostream &stream, const UInt outputWidth) { stream << std::setw(outputWidth) << UInt(value); }

template <typename ValueType>
Void printBlock(const ValueType    *const source,
                const UInt                width,
                const UInt                height,
                const UInt                stride,
                const UInt                outputValueWidth = 0,         //if set to 0, the maximum output width will be calculated and used
                const Bool                onlyPrintEdges   = false,     //print only the top row and left column for printing prediction reference samples
                const Bool                printInZScan     = false,     //output values in Z-scan format (useful for values addressed by AbsPartIdxes)
                const Int                 shiftLeftBy      = 0,         //set a negative value to right-shift instead
                const Bool                printAverage     = false,     //also print the average of the values in the block
                      std::ostream      & stream           = std::cout)
{
  //find the maximum output width
  UInt outputWidth = outputValueWidth;

  if (outputWidth == 0)
  {
    ValueType minimumValue = leftShift(source[0], shiftLeftBy);
    ValueType maximumValue = minimumValue;

    for (UInt y = 0; y < height; y++)
      for (UInt x = 0; x < width; x++)
      {
        ValueType value = 0;

        if (!onlyPrintEdges || (x == 0) || (y == 0))
        {
          value = leftShift(source[printInZScan ? getZScanIndex(x, y) : ((y * stride) + x)], shiftLeftBy);
        }

        if      (value < minimumValue) minimumValue = value;
        else if (value > maximumValue) maximumValue = value;
      }

    outputWidth = std::max<UInt>(getDecimalWidth(Double(minimumValue)), getDecimalWidth(Double(maximumValue))) + 1; //+1 so the numbers don't run into each other
  }

  //------------------
  //print out the block

  ValueType valueSum = 0;

  for (UInt y = 0; y < height; y++)
  {
    for (UInt x = 0; x < width; x++)
    {
      ValueType value = 0;

      if (!onlyPrintEdges || (x == 0) || (y == 0))
      {
        value     = leftShift(source[printInZScan ? getZScanIndex(x, y) : ((y * stride) + x)], shiftLeftBy);
        valueSum += value;
      }

      writeValueToStream(value, stream, outputWidth);
    }
    stream << "\n";
  }

  const Int valueCount = onlyPrintEdges ? Int((width + height) - 1) : Int(width * height);
  if (printAverage) stream << "Average: " << (valueSum / valueCount) << "\n";
  stream << "\n";
}


template <typename T>
Void printBlockToStream( std::ostream &ss, const Char *pLinePrefix, const T * blkSrc, const UInt width, const UInt height, const UInt stride, const UInt subBlockWidth=0, const UInt subBlockHeight=0, const UInt defWidth=3 )
{
  for (UInt y=0; y<height; y++)
  {
    if (subBlockHeight!=0 && (y%subBlockHeight)==0 && y!=0)
      ss << pLinePrefix << '\n';

    ss << pLinePrefix;
    for (UInt x=0; x<width; x++)
    {
      if (subBlockWidth!=0 && (x%subBlockWidth)==0 && x!=0)
        ss << std::setw(defWidth+2) << "";

      ss << std::setw(defWidth) << blkSrc[y*stride + x] << ' ';
    }
    ss << '\n';
  }
}

class TComYuv;
Void printBlockToStream( std::ostream &ss, const Char *pLinePrefix, TComYuv &src, const UInt numSubBlocksAcross=1, const UInt numSubBlocksUp=1, const UInt defWidth=3 );

// ---------------------------------------------------------------------------------------------- //

//String manipulation functions for aligning and wrapping printed text

std::string splitOnSettings(const std::string &input);

std::string lineWrap(const std::string &input, const UInt maximumLineLength);

std::string indentNewLines(const std::string &input, const UInt indentBy);

// ---------------------------------------------------------------------------------------------- //

#ifdef DEBUG_STRING
  Int DebugStringGetPredModeMask(PredMode mode);
  Void DebugInterPredResiReco(std::string &sDebug, TComYuv &pred, TComYuv &resi, TComYuv &reco, Int predmode_mask);
#endif


#endif /* __DEBUG__ */
