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

#ifndef __TCOMCODINGSTATISTICS__
#define __TCOMCODINGSTATISTICS__

#include "TypeDef.h"
#include <stdio.h>
#include <string>
#include <map>
#include <math.h>
#include "TComChromaFormat.h"

static const Int64 TCOMCODINGSTATISTICS_ENTROPYSCALE=32768;


enum TComCodingStatisticsType
{
  STATS__NAL_UNIT_TOTAL_BODY,// This is a special case and is not included in the total sums.
  STATS__NAL_UNIT_PACKING,
  STATS__EMULATION_PREVENTION_3_BYTES,
  STATS__NAL_UNIT_HEADER_BITS,
  STATS__CABAC_INITIALISATION,
  STATS__CABAC_BITS__TQ_BYPASS_FLAG,
  STATS__CABAC_BITS__SKIP_FLAG,
  STATS__CABAC_BITS__MERGE_FLAG,
  STATS__CABAC_BITS__MERGE_INDEX,
  STATS__CABAC_BITS__MVP_IDX,
  STATS__CABAC_BITS__SPLIT_FLAG,
  STATS__CABAC_BITS__PART_SIZE,
  STATS__CABAC_BITS__PRED_MODE,
  STATS__CABAC_BITS__INTRA_DIR_ANG,
  STATS__CABAC_BITS__INTER_DIR,
  STATS__CABAC_BITS__REF_FRM_IDX,
  STATS__CABAC_BITS__MVD,
  STATS__CABAC_BITS__MVD_EP,
  STATS__CABAC_BITS__TRANSFORM_SUBDIV_FLAG,
  STATS__CABAC_BITS__QT_ROOT_CBF,
  STATS__CABAC_BITS__DELTA_QP_EP,
  STATS__CABAC_BITS__CHROMA_QP_ADJUSTMENT,
  STATS__CABAC_BITS__QT_CBF,
  STATS__CABAC_BITS__CROSS_COMPONENT_PREDICTION,
  STATS__CABAC_BITS__TRANSFORM_SKIP_FLAGS,

  STATS__CABAC_BITS__LAST_SIG_X_Y,
  STATS__CABAC_BITS__SIG_COEFF_GROUP_FLAG,
  STATS__CABAC_BITS__SIG_COEFF_MAP_FLAG,
  STATS__CABAC_BITS__GT1_FLAG,
  STATS__CABAC_BITS__GT2_FLAG,
  STATS__CABAC_BITS__SIGN_BIT,
  STATS__CABAC_BITS__ESCAPE_BITS,

  STATS__CABAC_BITS__SAO,
  STATS__CABAC_TRM_BITS,
  STATS__CABAC_FIXED_BITS,
  STATS__CABAC_PCM_ALIGN_BITS,
  STATS__CABAC_PCM_CODE_BITS,
  STATS__BYTE_ALIGNMENT_BITS,
  STATS__TRAILING_BITS,
  STATS__EXPLICIT_RDPCM_BITS,
  STATS__CABAC_EP_BIT_ALIGNMENT,
  STATS__CABAC_BITS__ALIGNED_SIGN_BIT,
  STATS__CABAC_BITS__ALIGNED_ESCAPE_BITS,
  STATS__NUM_STATS
};

static inline const Char* getName(TComCodingStatisticsType name)
{
  static const Char *statNames[]=
  {
    "NAL_UNIT_TOTAL_BODY", // This is a special case and is not included in the total sums.
    "NAL_UNIT_PACKING",
    "EMULATION_PREVENTION_3_BYTES",
    "NAL_UNIT_HEADER_BITS",
    "CABAC_INITIALISATION-and-rounding",
    "CABAC_BITS__TQ_BYPASS_FLAG",
    "CABAC_BITS__SKIP_FLAG",
    "CABAC_BITS__MERGE_FLAG",
    "CABAC_BITS__MERGE_INDEX",
    "CABAC_BITS__MVP_IDX",
    "CABAC_BITS__SPLIT_FLAG",
    "CABAC_BITS__PART_SIZE",
    "CABAC_BITS__PRED_MODE",
    "CABAC_BITS__INTRA_DIR_ANG",
    "CABAC_BITS__INTER_DIR",
    "CABAC_BITS__REF_FRM_IDX",
    "CABAC_BITS__MVD",
    "CABAC_BITS__MVD_EP",
    "CABAC_BITS__TRANSFORM_SUBDIV_FLAG",
    "CABAC_BITS__QT_ROOT_CBF",
    "CABAC_BITS__DELTA_QP_EP",
    "CABAC_BITS__CHROMA_QP_ADJUSTMENT",
    "CABAC_BITS__QT_CBF",
    "CABAC_BITS__CROSS_COMPONENT_PREDICTION",
    "CABAC_BITS__TRANSFORM_SKIP_FLAGS",
    "CABAC_BITS__LAST_SIG_X_Y",
    "CABAC_BITS__SIG_COEFF_GROUP_FLAG",
    "CABAC_BITS__SIG_COEFF_MAP_FLAG",
    "CABAC_BITS__GT1_FLAG",
    "CABAC_BITS__GT2_FLAG",
    "CABAC_BITS__SIGN_BIT",
    "CABAC_BITS__ESCAPE_BITS",
    "CABAC_BITS__SAO",
    "CABAC_TRM_BITS",
    "CABAC_FIXED_BITS",
    "CABAC_PCM_ALIGN_BITS",
    "CABAC_PCM_CODE_BITS",
    "BYTE_ALIGNMENT_BITS",
    "TRAILING_BITS",
    "EXPLICIT_RDPCM_BITS",
    "CABAC_EP_BIT_ALIGNMENT",
    "CABAC_BITS__ALIGNED_SIGN_BIT",
    "CABAC_BITS__ALIGNED_ESCAPE_BITS"
  };
  assert(STATS__NUM_STATS == sizeof(statNames)/sizeof(Char *) && name < STATS__NUM_STATS);
  return statNames[name];
}

static inline Bool isAlignedBins(TComCodingStatisticsType statT) { return statT==STATS__CABAC_BITS__ALIGNED_SIGN_BIT || statT==STATS__CABAC_BITS__ALIGNED_ESCAPE_BITS; }

static const UInt CODING_STATS_NUM_WIDTHS=7;
static const UInt CODING_STATS_NUM_SUBCLASSES=CODING_STATS_NUM_WIDTHS*(1+MAX_NUM_COMPONENT+MAX_NUM_CHANNEL_TYPE);

class TComCodingStatisticsClassType
{
public:

  TComCodingStatisticsClassType(const TComCodingStatisticsType t)
    : type(t), subClass(0)
  { }
  TComCodingStatisticsClassType(const TComCodingStatisticsType t, const UInt log2w )
    : type(t), subClass(log2w)
  { }
  TComCodingStatisticsClassType(const TComCodingStatisticsType t, const Int log2w )
    : type(t), subClass(log2w)
  { }
  TComCodingStatisticsClassType(const TComCodingStatisticsType t, const ComponentID cid )
    : type(t), subClass((cid+1)*CODING_STATS_NUM_WIDTHS)
  { }
  TComCodingStatisticsClassType(const TComCodingStatisticsType t, const ChannelType chid )
    : type(t), subClass((chid+MAX_NUM_COMPONENT+1)*CODING_STATS_NUM_WIDTHS)
  { }
  TComCodingStatisticsClassType(const TComCodingStatisticsType t, const UInt log2w, const ComponentID cid )
    : type(t), subClass((cid+1)*CODING_STATS_NUM_WIDTHS + log2w)
  { }
  TComCodingStatisticsClassType(const TComCodingStatisticsType t, const UInt log2w, const ChannelType chid )
    : type(t), subClass((chid+MAX_NUM_COMPONENT+1)*CODING_STATS_NUM_WIDTHS + log2w)
  { }

  static UInt GetSubClassWidth(const UInt subClass)
  {
    return subClass%CODING_STATS_NUM_WIDTHS;
  }

  static const Char *GetSubClassString(const UInt subClass)
  {
    assert (subClass<CODING_STATS_NUM_SUBCLASSES);
    static const Char *strings[1+MAX_NUM_COMPONENT+MAX_NUM_CHANNEL_TYPE]={"-", "Y", "Cb", "Cr", "Luma", "Chroma"};
    return strings[subClass/CODING_STATS_NUM_WIDTHS];
  }

  TComCodingStatisticsType type;
  UInt subClass;
};



class TComCodingStatistics
{
  public:


    struct StatLogValue
    {
      UInt values[512+1];
      StatLogValue()
      {
        const Double es=Double(TCOMCODINGSTATISTICS_ENTROPYSCALE);
        values[0]=0;
        for(UInt i=1; i<sizeof(values)/sizeof(UInt); i++)
        {
          values[i]=UInt(    log(Double(i))*es/log(2.0)  );
        }
      }
    };

    struct SStat
    {
      SStat() : bits(0), count(0), sum(0) { }
      Int64 bits;
      Int64 count;
      Int64 sum;
      Void clear() { bits=0; count=0; sum=0; }

      SStat &operator+=(const SStat &src)
      { bits+=src.bits; count+=src.count; sum+=src.sum; return *this; }
    };

    class TComCodingStatisticsData
    {
      private:
        SStat statistics[STATS__NUM_STATS+1][CODING_STATS_NUM_SUBCLASSES];
        SStat statistics_ep[STATS__NUM_STATS+1][CODING_STATS_NUM_SUBCLASSES ];
        std::map<std::string, SStat> mappings_ep;
        friend class TComCodingStatistics;
    };

  private:

    TComCodingStatisticsData data;

    TComCodingStatistics() : data()
    { }

    static Void OutputLine(const Char *pName, const Char sep, UInt width, const Char *pSubClassStr, const SStat &sCABAC, const SStat &sEP)
    {
      if (width==0)
        OutputLine(pName, sep, "-", pSubClassStr, sCABAC, sEP);
      else
        printf("%c%-45s%c  %6d %6s %12lld %12lld %12lld %12lld %12lld %12lld %12lld (%12lld)%c\n",
          sep=='~'?'[':' ', pName, sep, 1<<width, pSubClassStr,
              sCABAC.count, sCABAC.sum, sCABAC.bits, sEP.count, sEP.sum, sEP.bits, sCABAC.bits+sEP.bits, (sCABAC.bits+sEP.bits)/8, sep=='~'?']':' ');
    }
    static Void OutputLine(const Char *pName, const Char sep, const Char *pWidthString, const Char *pSubClassStr, const SStat &sCABAC, const SStat &sEP)
    {
      printf("%c%-45s%c  %6s %6s %12lld %12lld %12lld %12lld %12lld %12lld %12lld (%12lld)%c\n",
          sep=='~'?'[':' ', pName, sep, pWidthString, pSubClassStr,
              sCABAC.count, sCABAC.sum, sCABAC.bits, sEP.count, sEP.sum, sEP.bits, sCABAC.bits+sEP.bits, (sCABAC.bits+sEP.bits)/8, sep=='~'?']':' ');
    }
    static Void OutputLine(const Char *pName, const Char sep, const Char *pWidthString, const Char *pSubClassStr,  const SStat &sEP)
    {
      printf("%c%-45s%c  %6s %6s %12s %12s %12s %12lld %12lld %12lld %12lld (%12lld)%c\n",
          sep=='~'?'[':' ', pName, sep, pWidthString, pSubClassStr,
              "", "", "", sEP.count, sEP.sum, sEP.bits, sEP.bits, (sEP.bits)/8, sep=='~'?']':' ');
    }

    static Void OutputDashedLine(const Char *pText)
    {
      printf("--%s",pText);
      UInt tot=0;
      for(;pText[tot]!=0; tot++);
      tot+=2;
      for (; tot<168; tot++)
      {
        printf("-");
      }
      printf("\n");
    }

    ~TComCodingStatistics()
    {
      const Int64 es=TCOMCODINGSTATISTICS_ENTROPYSCALE;

      Int64 cr=0; // CABAC remainder, which is added to "STATS__CABAC_INITIALISATION"
      {
        Int64 totalCABACbits=0, roundedCABACbits=0;
        for(Int i=STATS__NAL_UNIT_PACKING; i<STATS__NUM_STATS; i++)
        {
          for(UInt c=0; c<CODING_STATS_NUM_SUBCLASSES; c++)
          {
            totalCABACbits+=data.statistics[i][c].bits;
            roundedCABACbits+=data.statistics[i][c].bits/es;
          }
        }
        Int64 remainder=totalCABACbits - roundedCABACbits*es;
        cr=(remainder+es/2)/es;
      }

      printf("Note %s will be excluded from the total as it should be the sum of all the other entries (except for %s)\n", getName(STATS__NAL_UNIT_TOTAL_BODY), getName(STATS__NAL_UNIT_PACKING));
      printf(" %-45s-   Width   Type  CABAC Count    CABAC Sum   CABAC bits     EP Count       EP Sum      EP bits   Total bits ( Total bytes)\n", "Decoder statistics");

      OutputDashedLine("");
      SStat cabacTotalBits, epTotalBits;
      SStat statTotals_cabac[CODING_STATS_NUM_SUBCLASSES];
      SStat statTotals_ep[CODING_STATS_NUM_SUBCLASSES];

      for(Int i=0; i<STATS__NUM_STATS; i++)
      {
        SStat cabacSubTotal, epSubTotal;
        Bool bHadClassifiedEntry=false;
        const Char *pName=getName(TComCodingStatisticsType(i));

        for(UInt c=0; c<CODING_STATS_NUM_SUBCLASSES; c++)
        {
          SStat &sCABACorig=data.statistics[i][c];
          SStat &sEP=data.statistics_ep[i][c];

          if (sCABACorig.bits==0 && sEP.bits==0) continue;

          SStat sCABAC;
          {
            Int64 thisCABACbits=sCABACorig.bits/es; if (i==STATS__CABAC_INITIALISATION && sCABACorig.bits!=0) { thisCABACbits+=cr; cr=0; }
            sCABAC.bits=thisCABACbits; sCABAC.count=sCABACorig.count; sCABAC.sum=sCABACorig.sum;
          }
          UInt width=TComCodingStatisticsClassType::GetSubClassWidth(c);
          OutputLine(pName, ':', width, TComCodingStatisticsClassType::GetSubClassString(c), sCABAC, sEP);
          cabacSubTotal+=sCABAC;
          epSubTotal+=sEP;
          if (i!=STATS__NAL_UNIT_TOTAL_BODY)
          {
            cabacTotalBits+=sCABAC;
            epTotalBits+=sEP;
            statTotals_cabac[c]+=sCABAC;
            statTotals_ep[c]+=sEP;
          }
          bHadClassifiedEntry=bHadClassifiedEntry||(c!=0);
        }
        if (bHadClassifiedEntry)
        {
          OutputLine(pName, '~', "~~ST~~", "~~ST~~", cabacSubTotal, epSubTotal);
        }
        if (i==STATS__NAL_UNIT_TOTAL_BODY)
        {
          OutputDashedLine("");
        }
      }
      OutputDashedLine("");
      OutputLine("CABAC Sub-total", '~', "~~ST~~", "~~ST~~", cabacTotalBits, epTotalBits);

      OutputDashedLine("CAVLC HEADER BITS");
      SStat cavlcTotalBits;
      for(std::map<std::string, SStat>::iterator it=data.mappings_ep.begin(); it!=data.mappings_ep.end(); it++)
      {
        SStat s=it->second;
        cavlcTotalBits+=s;
        OutputLine(it->first.c_str(), ':', "-", "-", s);
      }

      OutputDashedLine("");
      OutputLine("CAVLC Header Sub-total", '~', "~~ST~~", "~~ST~~", cavlcTotalBits);

      // Now output the breakdowns
      OutputDashedLine("CABAC Break down by size");
      for(UInt s=0; s<CODING_STATS_NUM_WIDTHS; s++)
      {
        SStat subTotalCabac, subTotalEP;
        for(UInt c=0; c<CODING_STATS_NUM_SUBCLASSES; c+=CODING_STATS_NUM_WIDTHS)
        {
          subTotalCabac+=statTotals_cabac[c+s];
          subTotalEP+=statTotals_ep[c+s];
        }
        if (subTotalCabac.bits!=0 || subTotalEP.bits!=0)
        {
          OutputLine("CABAC by size Sub-total", '=', s, "All", subTotalCabac, subTotalEP);
        }
      }
      OutputDashedLine("Break down by component/Channel type");
      for(UInt c=0; c<CODING_STATS_NUM_SUBCLASSES; c+=CODING_STATS_NUM_WIDTHS)
      {
        SStat subTotalCabac, subTotalEP;
        for(UInt s=0; s<CODING_STATS_NUM_WIDTHS; s++)
        {
          subTotalCabac+=statTotals_cabac[c+s];
          subTotalEP+=statTotals_ep[c+s];
        }
        if (subTotalCabac.bits!=0 || subTotalEP.bits!=0)
        {
          OutputLine("CABAC by type Sub-total", '=', "-", TComCodingStatisticsClassType::GetSubClassString(c), subTotalCabac, subTotalEP);
        }
      }
      OutputDashedLine("Break down by size and component/Channel type");
      for(UInt c=0; c<CODING_STATS_NUM_SUBCLASSES; c+=CODING_STATS_NUM_WIDTHS)
      {
        for(UInt s=0; s<CODING_STATS_NUM_WIDTHS; s++)
        {
          SStat subTotalCabac, subTotalEP;
          subTotalCabac+=statTotals_cabac[c+s];
          subTotalEP+=statTotals_ep[c+s];
          if (subTotalCabac.bits!=0 || subTotalEP.bits!=0)
          {
            OutputLine("CABAC by size and type Sub-total", '=', s, TComCodingStatisticsClassType::GetSubClassString(c), subTotalCabac, subTotalEP);
          }
        }
      }

      OutputDashedLine("");
      OutputLine("CABAC Sub-total", '~', "~~ST~~", "~~ST~~", cabacTotalBits, epTotalBits);
      OutputLine("CAVLC Header Sub-total", '~', "~~ST~~", "~~ST~~", cavlcTotalBits);
      OutputDashedLine("GRAND TOTAL");
      epTotalBits+=cavlcTotalBits;
      OutputLine("TOTAL", '~', "~~GT~~", "~~GT~~", cabacTotalBits, epTotalBits);
    }


  public:
    static TComCodingStatistics& GetSingletonInstance()
    {
      static TComCodingStatistics inst;
      return inst;
    }

    static const TComCodingStatisticsData &GetStatistics()         { return GetSingletonInstance().data; }
    static Void SetStatistics(const TComCodingStatisticsData &src) { GetSingletonInstance().data=src; }

    static SStat &GetStatisticEP(const TComCodingStatisticsClassType &stat) { return GetSingletonInstance().data.statistics_ep[stat.type][stat.subClass]; }

    static SStat &GetStatisticEP(const std::string &str) { return GetSingletonInstance().data.mappings_ep[str]; }

    static SStat &GetStatisticEP(const Char *pKey) {return GetStatisticEP(std::string(pKey)); }

    static Void IncrementStatisticEP(const TComCodingStatisticsClassType &stat, const Int numBits, const Int value)
    {
      SStat &s=GetStatisticEP(stat);
      s.bits+=numBits;
      s.count++;
      s.sum+=value;
    }

    static Void IncrementStatisticEP(const std::string &str, const Int numBits, const Int value)
    {
      SStat &s=GetStatisticEP(str);
      s.bits+=numBits;
      s.count++;
      s.sum+=value;
    }

    static Void IncrementStatisticEP(const Char *pKey, const Int numBits, const Int value)
    {
      SStat &s=GetStatisticEP(pKey);
      s.bits+=numBits;
      s.count++;
      s.sum+=value;
    }

    StatLogValue values;

    static Void UpdateCABACStat(const TComCodingStatisticsClassType &stat, UInt uiRangeBefore, UInt uiRangeAfter, Int val)
    {
      TComCodingStatistics &inst=GetSingletonInstance();
      // doing rangeBefore*p(x)=rangeAfter
      // p(x)=rangeAfter/rangeBefore
      // entropy = -log2(p(x))=-log(p(x))/log(2) = -(log rangeAfter - log rangeBefore) / log(2) = (log rangeBefore / log 2 - log rangeAfter / log 2)
      SStat &s=inst.data.statistics[stat.type][stat.subClass];
      s.bits+=inst.values.values[uiRangeBefore]-inst.values.values[uiRangeAfter];
      s.count++;
      s.sum+=val;
    }
};

#endif
