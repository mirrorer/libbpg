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

#include "TComPicYuv.h"
#include "libmd5/MD5.h"

//! \ingroup TLibCommon
//! \{

/**
 * Update md5 using n samples from plane, each sample is adjusted to
 * OUTBIT_BITDEPTH_DIV8.
 */
template<UInt OUTPUT_BITDEPTH_DIV8>
static Void md5_block(MD5& md5, const Pel* plane, UInt n)
{
  /* create a 64 byte buffer for packing Pel's into */
  UChar buf[64/OUTPUT_BITDEPTH_DIV8][OUTPUT_BITDEPTH_DIV8];
  for (UInt i = 0; i < n; i++)
  {
    Pel pel = plane[i];
    /* perform bitdepth and endian conversion */
    for (UInt d = 0; d < OUTPUT_BITDEPTH_DIV8; d++)
    {
      buf[i][d] = pel >> (d*8);
    }
  }
  md5.update((UChar*)buf, n * OUTPUT_BITDEPTH_DIV8);
}

/**
 * Update md5 with all samples in plane in raster order, each sample
 * is adjusted to OUTBIT_BITDEPTH_DIV8.
 */
template<UInt OUTPUT_BITDEPTH_DIV8>
static Void md5_plane(MD5& md5, const Pel* plane, UInt width, UInt height, UInt stride)
{
  /* N is the number of samples to process per md5 update.
   * All N samples must fit in buf */
  UInt N = 32;
  UInt width_modN = width % N;
  UInt width_less_modN = width - width_modN;

  for (UInt y = 0; y < height; y++)
  {
    /* convert pels into unsigned chars in little endian byte order.
     * NB, for 8bit data, data is truncated to 8bits. */
    for (UInt x = 0; x < width_less_modN; x += N)
      md5_block<OUTPUT_BITDEPTH_DIV8>(md5, &plane[y*stride + x], N);

    /* mop up any of the remaining line */
    md5_block<OUTPUT_BITDEPTH_DIV8>(md5, &plane[y*stride + width_less_modN], width_modN);
  }
}


UInt compCRC(Int bitdepth, const Pel* plane, UInt width, UInt height, UInt stride, TComDigest &digest)
{
  UInt crcMsb;
  UInt bitVal;
  UInt crcVal = 0xffff;
  UInt bitIdx;
  for (UInt y = 0; y < height; y++)
  {
    for (UInt x = 0; x < width; x++)
    {
      // take CRC of first pictureData byte
      for(bitIdx=0; bitIdx<8; bitIdx++)
      {
        crcMsb = (crcVal >> 15) & 1;
        bitVal = (plane[y*stride+x] >> (7 - bitIdx)) & 1;
        crcVal = (((crcVal << 1) + bitVal) & 0xffff) ^ (crcMsb * 0x1021);
      }
      // take CRC of second pictureData byte if bit depth is greater than 8-bits
      if(bitdepth > 8)
      {
        for(bitIdx=0; bitIdx<8; bitIdx++)
        {
          crcMsb = (crcVal >> 15) & 1;
          bitVal = (plane[y*stride+x] >> (15 - bitIdx)) & 1;
          crcVal = (((crcVal << 1) + bitVal) & 0xffff) ^ (crcMsb * 0x1021);
        }
      }
    }
  }
  for(bitIdx=0; bitIdx<16; bitIdx++)
  {
    crcMsb = (crcVal >> 15) & 1;
    crcVal = ((crcVal << 1) & 0xffff) ^ (crcMsb * 0x1021);
  }

  digest.hash.push_back((crcVal>>8)  & 0xff);
  digest.hash.push_back( crcVal      & 0xff);
  return 2;
}

UInt calcCRC(const TComPicYuv& pic, TComDigest &digest)
{
  UInt digestLen=0;
  digest.hash.clear();
  for(Int chan=0; chan<pic.getNumberValidComponents(); chan++)
  {
    const ComponentID compID=ComponentID(chan);
    digestLen=compCRC(g_bitDepth[toChannelType(compID)], pic.getAddr(compID), pic.getWidth(compID), pic.getHeight(compID), pic.getStride(compID), digest);
  }
  return digestLen;
}

UInt compChecksum(Int bitdepth, const Pel* plane, UInt width, UInt height, UInt stride, TComDigest &digest)
{
  UInt checksum = 0;
  UChar xor_mask;

  for (UInt y = 0; y < height; y++)
  {
    for (UInt x = 0; x < width; x++)
    {
      xor_mask = (x & 0xff) ^ (y & 0xff) ^ (x >> 8) ^ (y >> 8);
      checksum = (checksum + ((plane[y*stride+x] & 0xff) ^ xor_mask)) & 0xffffffff;

      if(bitdepth > 8)
      {
        checksum = (checksum + ((plane[y*stride+x]>>8) ^ xor_mask)) & 0xffffffff;
      }
    }
  }

  digest.hash.push_back((checksum>>24) & 0xff);
  digest.hash.push_back((checksum>>16) & 0xff);
  digest.hash.push_back((checksum>>8)  & 0xff);
  digest.hash.push_back( checksum      & 0xff);
  return 4;
}

UInt calcChecksum(const TComPicYuv& pic, TComDigest &digest)
{
  UInt digestLen=0;
  digest.hash.clear();
  for(Int chan=0; chan<pic.getNumberValidComponents(); chan++)
  {
    const ComponentID compID=ComponentID(chan);
    digestLen=compChecksum(g_bitDepth[toChannelType(compID)], pic.getAddr(compID), pic.getWidth(compID), pic.getHeight(compID), pic.getStride(compID), digest);
  }
  return digestLen;
}
/**
 * Calculate the MD5sum of pic, storing the result in digest.
 * MD5 calculation is performed on Y' then Cb, then Cr; each in raster order.
 * Pel data is inserted into the MD5 function in little-endian byte order,
 * using sufficient bytes to represent the picture bitdepth.  Eg, 10bit data
 * uses little-endian two byte words; 8bit data uses single byte words.
 */
UInt calcMD5(const TComPicYuv& pic, TComDigest &digest)
{
  /* choose an md5_plane packing function based on the system bitdepth */
  typedef Void (*MD5PlaneFunc)(MD5&, const Pel*, UInt, UInt, UInt);
  MD5PlaneFunc md5_plane_func;

  MD5 md5[MAX_NUM_COMPONENT];

  digest.hash.clear();
  for(Int chan=0; chan<pic.getNumberValidComponents(); chan++)
  {
    const ComponentID compID=ComponentID(chan);
    md5_plane_func = g_bitDepth[toChannelType(compID)] <= 8 ? (MD5PlaneFunc)md5_plane<1> : (MD5PlaneFunc)md5_plane<2>;
    UChar tmp_digest[MD5_DIGEST_STRING_LENGTH];
    md5_plane_func(md5[compID], pic.getAddr(compID), pic.getWidth(compID), pic.getHeight(compID), pic.getStride(compID));
    md5[compID].finalize(tmp_digest);
    for(UInt i=0; i<MD5_DIGEST_STRING_LENGTH; i++)
    {
      digest.hash.push_back(tmp_digest[i]);
    }
  }
  return 16;
}

std::string digestToString(const TComDigest &digest, Int numChar)
{
  static const Char* hex = "0123456789abcdef";
  std::string result;

  for(Int pos=0; pos<Int(digest.hash.size()); pos++)
  {
    if ((pos % numChar) == 0 && pos!=0 ) result += ',';
    result += hex[digest.hash[pos] >> 4];
    result += hex[digest.hash[pos] & 0xf];
  }

  return result;
}

//! \}
