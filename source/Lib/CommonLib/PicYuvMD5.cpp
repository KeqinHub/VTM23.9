/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2025, ITU/ISO/IEC
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

#include "Picture.h"
#include "SEI.h"
#include "libmd5/MD5.h"

//! \ingroup CommonLib
//! \{

/**
 * Update md5 using n samples from plane, each sample is adjusted to
 * OUTBIT_BITDEPTH_DIV8.
 */
template<uint32_t OUTPUT_BITDEPTH_DIV8>
static void md5_block(MD5& md5, const Pel* plane, uint32_t n)
{
  /* create a 64 byte buffer for packing Pel's into */
  uint8_t buf[64/OUTPUT_BITDEPTH_DIV8][OUTPUT_BITDEPTH_DIV8];
  for (uint32_t i = 0; i < n; i++)
  {
    Pel pel = plane[i];
    /* perform bitdepth and endian conversion */
    for (uint32_t d = 0; d < OUTPUT_BITDEPTH_DIV8; d++)
    {
      buf[i][d] = pel >> (d*8);
    }
  }
  md5.update((uint8_t*)buf, n * OUTPUT_BITDEPTH_DIV8);
}

/**
 * Update md5 with all samples in plane in raster order, each sample
 * is adjusted to OUTBIT_BITDEPTH_DIV8.
 */
template<uint32_t OUTPUT_BITDEPTH_DIV8>
static void md5_plane(MD5& md5, const Pel* plane, uint32_t width, uint32_t height, ptrdiff_t stride)
{
  /* N is the number of samples to process per md5 update.
   * All N samples must fit in buf */
  uint32_t N = 32;
  uint32_t width_modN = width % N;
  uint32_t width_less_modN = width - width_modN;

  for (uint32_t y = 0; y < height; y++)
  {
    /* convert pels into unsigned chars in little endian byte order.
     * NB, for 8bit data, data is truncated to 8bits. */
    for (uint32_t x = 0; x < width_less_modN; x += N)
    {
      md5_block<OUTPUT_BITDEPTH_DIV8>(md5, &plane[y*stride + x], N);
    }

    /* mop up any of the remaining line */
    md5_block<OUTPUT_BITDEPTH_DIV8>(md5, &plane[y*stride + width_less_modN], width_modN);
  }
}

uint32_t compCRC(int bitdepth, const Pel *plane, uint32_t width, uint32_t height, ptrdiff_t stride, PictureHash &digest)
{
  uint32_t crcMsb;
  uint32_t bitVal;
  uint32_t crcVal = 0xffff;
  uint32_t bitIdx;
  for (uint32_t y = 0; y < height; y++)
  {
    for (uint32_t x = 0; x < width; x++)
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

uint32_t calcCRC(const CPelUnitBuf& pic, PictureHash &digest, const BitDepths &bitDepths)
{
  uint32_t digestLen=0;
  digest.hash.clear();
  for (uint32_t chan = 0; chan< (uint32_t)pic.bufs.size(); chan++)
  {
    const ComponentID compID = ComponentID(chan);
    const CPelBuf area = pic.get(compID);
    digestLen =
      compCRC(bitDepths[toChannelType(compID)], area.bufAt(0, 0), area.width, area.height, area.stride, digest);
  }
  return digestLen;
}

uint32_t compChecksum(int bitdepth, const Pel *plane, uint32_t width, uint32_t height, ptrdiff_t stride,
                      PictureHash &digest, const BitDepths & /*bitDepths*/)
{
  uint32_t checksum = 0;
  uint8_t xor_mask;

  for (uint32_t y = 0; y < height; y++)
  {
    for (uint32_t x = 0; x < width; x++)
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

uint32_t calcChecksum(const CPelUnitBuf& pic, PictureHash &digest, const BitDepths &bitDepths)
{
  uint32_t digestLen=0;
  digest.hash.clear();
  for(uint32_t chan=0; chan< (uint32_t)pic.bufs.size(); chan++)
  {
    const ComponentID compID=ComponentID(chan);
    const CPelBuf area = pic.get(compID);
    digestLen = compChecksum(bitDepths[toChannelType(compID)], area.bufAt(0, 0), area.width, area.height, area.stride,
                             digest, bitDepths);
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
uint32_t calcMD5(const CPelUnitBuf& pic, PictureHash &digest, const BitDepths &bitDepths)
{
  return calcMD5WithCropping(pic, digest, bitDepths, 0, 0, 0, 0);
}

uint32_t calcMD5WithCropping(const CPelUnitBuf &pic, PictureHash &digest, const BitDepths &bitDepths,
                             const int leftOffset, const int rightOffset, const int topOffset, const int bottomOffset)
{
  /* choose an md5_plane packing function based on the system bitdepth */
  typedef void (*MD5PlaneFunc)(MD5 &, const Pel *, uint32_t, uint32_t, ptrdiff_t);
  MD5PlaneFunc md5_plane_func;

  MD5 md5[MAX_NUM_COMPONENT];

  digest.hash.clear();

  for (uint32_t chan = 0; chan< (uint32_t)pic.bufs.size(); chan++)
  {
    const ComponentID compID           = ComponentID(chan);
    const CPelBuf     area             = pic.get(compID);
    const int         chromaScaleX     = getComponentScaleX(compID, pic.chromaFormat);
    const int         chromaScaleY     = getComponentScaleY(compID, pic.chromaFormat);
    const int         compLeftOffset   = leftOffset >> chromaScaleX;
    const int         compRightOffset  = rightOffset >> chromaScaleX;
    const int         compTopOffset    = topOffset >> chromaScaleY;
    const int         compBottomOffset = bottomOffset >> chromaScaleY;
    md5_plane_func = bitDepths[toChannelType(compID)] <= 8 ? (MD5PlaneFunc) md5_plane<1> : (MD5PlaneFunc) md5_plane<2>;
    uint8_t tmp_digest[MD5_DIGEST_STRING_LENGTH];
    md5_plane_func(md5[compID], area.bufAt(compLeftOffset, compTopOffset),
                   area.width - compRightOffset - compLeftOffset, area.height - compTopOffset - compBottomOffset,
                   area.stride);
    md5[compID].finalize(tmp_digest);
    for (uint32_t i = 0; i < MD5_DIGEST_STRING_LENGTH; i++)
    {
      digest.hash.push_back(tmp_digest[i]);
    }
  }

  return 16;
}

std::string hashToString(const PictureHash &digest, int numChar)
{
  static const char* hex = "0123456789abcdef";
  std::string result;

  for(int pos=0; pos<int(digest.hash.size()); pos++)
  {
    if ((pos % numChar) == 0 && pos!=0 )
    {
      result += ',';
    }
    result += hex[digest.hash[pos] >> 4];
    result += hex[digest.hash[pos] & 0xf];
  }

  return result;
}

int calcAndPrintHashStatus(const CPelUnitBuf& pic, const SEIDecodedPictureHash* pictureHashSEI, const BitDepths &bitDepths, const MsgLevel msgl)
{
  /* calculate MD5sum for entire reconstructed picture */
  PictureHash recon_digest;
  int numChar=0;
  const char* hashType = "\0";

  if (pictureHashSEI)
  {
    CHECK ((uint32_t)pic.bufs.size() != ( pictureHashSEI->singleCompFlag ? 1 : 3 ), "The value of dph_sei_single_component_flag shall be equal to (ChromaFormatIdc == 0).");
    switch (pictureHashSEI->method)
    {
    case HashType::MD5:
    {
      hashType = "MD5";
      numChar  = calcMD5(pic, recon_digest, bitDepths);
      break;
    }
    case HashType::CRC:
    {
      hashType = "CRC";
      numChar  = calcCRC(pic, recon_digest, bitDepths);
      break;
    }
    case HashType::CHECKSUM:
    {
      hashType = "Checksum";
      numChar  = calcChecksum(pic, recon_digest, bitDepths);
      break;
    }
    default:
    {
      THROW("Unknown hash type");
      break;
    }
    }
  }

  /* compare digest against received version */
  const char* ok = "(unk)";
  bool mismatch = false;

  if (pictureHashSEI)
  {
    ok = "(OK)";
    if (recon_digest != pictureHashSEI->m_pictureHash)
    {
      ok = "(***ERROR***)";
      mismatch = true;
    }
  }

  msg( msgl, "[%s:%s,%s] ", hashType, hashToString(recon_digest, numChar).c_str(), ok);

  if (mismatch)
  {
    msg( msgl, "[rx%s:%s] ", hashType, hashToString(pictureHashSEI->m_pictureHash, numChar).c_str());
  }
  return mismatch;
}

//! \}
