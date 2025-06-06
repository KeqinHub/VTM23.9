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

/** \file     SEIFilmGrainAnalyzer.h
    \brief    SMPTE RDD5 based film grain analysis functionality from SEI messages
*/

#ifndef __SEIFILMGRAINANALYZER__
#define __SEIFILMGRAINANALYZER__

#pragma once

#include "CommonLib/Picture.h"
#include "CommonLib/SEI.h"
#include "Utilities/VideoIOYuv.h"
#include "CommonLib/CommonDef.h"

#include <numeric>
#include <cmath>
#include <algorithm>


static constexpr double   PI                            =     3.14159265358979323846;

// POLYFIT
static constexpr int      MAXPAIRS                                  = 256;
static constexpr int      MAXORDER                                  = 8;     // maximum order of polinomial fitting
static constexpr int      MAX_REAL_SCALE                            = 16;
static constexpr int      ORDER                                     = 4;     // order of polinomial function
static constexpr int      QUANT_LEVELS                              = 4;     // number of quantization levels in lloyd max quantization
static constexpr int      INTERVAL_SIZE                             = 16;
static constexpr int      MIN_ELEMENT_NUMBER_PER_INTENSITY_INTERVAL = 8;
static constexpr int      MIN_POINTS_FOR_INTENSITY_ESTIMATION       = 40;    // 5*8 = 40; 5 intervals with at least 8 points
static constexpr int      MIN_BLOCKS_FOR_CUTOFF_ESTIMATION          = 2;     // 2 blocks of 64 x 64 size
static constexpr int      POINT_STEP                                = 16;    // step size in point extension
static constexpr int      MAX_NUM_POINT_TO_EXTEND                   = 4;     // max point in extension
static constexpr double   POINT_SCALE                               = 1.25;  // scaling in point extension
static constexpr double   VAR_SCALE_DOWN                            = 1.2;   // filter out large points
static constexpr double   VAR_SCALE_UP                              = 0.6;   // filter out large points
static constexpr int      NUM_PASSES                                = 2;     // number of passes when fitting the function
static constexpr int      NBRS                                      = 1;     // minimum number of surrounding points in order to keep it for further analysis (within the widnow range)
static constexpr int      WINDOW                                    = 1;     // window to check surrounding points
static constexpr int      MIN_INTENSITY                             = 40;
static constexpr int      MAX_INTENSITY                             = 950;

//! \ingroup SEIFilmGrainAnalyzer
//! \{

// ====================================================================================================================
// Class definition
// ====================================================================================================================

struct Picture;

typedef std::vector<std::vector<Intermediate_Int>> PelMatrix;
typedef std::vector<std::vector<double>>           PelMatrixDouble;

typedef std::vector<std::vector<long double>>      PelMatrixLongDouble;
typedef std::vector<long double>                   PelVectorLongDouble;

class Canny
{
public:
  Canny();
  ~Canny();

  unsigned int      m_convWidthG = 5, m_convHeightG = 5;		  // Pixel's row and col positions for Gauss filtering

  void detect_edges(const PelStorage* orig, PelStorage* dest, unsigned int uiBitDepth, ComponentID compID);

private:
  static const int  m_gx[3][3];                               // Sobel kernel x
  static const int  m_gy[3][3];                               // Sobel kernel y
  static const int  m_gauss5x5[5][5];                         // Gauss 5x5 kernel, integer approximation

  unsigned int      m_convWidthS = 3, m_convHeightS = 3;		  // Pixel's row and col positions for Sobel filtering

  double            m_lowThresholdRatio   = 0.1;               // low threshold rato
  int               m_highThresholdRatio  = 3;                 // high threshold rato

  void gradient   ( PelStorage* buff1, PelStorage* buff2,
                    unsigned int width, unsigned int height,
                    unsigned int convWidthS, unsigned int convHeightS, unsigned int bitDepth, ComponentID compID );
  void suppressNonMax ( PelStorage* buff1, PelStorage* buff2, unsigned int width, unsigned int height, ComponentID compID );
  void doubleThreshold( PelStorage *buff, unsigned int width, unsigned int height, /*unsigned int windowSizeRatio,*/
                       unsigned int bitDepth, ComponentID compID);
  void edgeTracking   ( PelStorage* buff1, unsigned int width, unsigned int height,
                       unsigned int windowWidth, unsigned int windowHeight, unsigned int bitDepth, ComponentID compID );
};


class Morph
{
public:
  Morph();
  ~Morph();

  int dilation  (PelStorage* buff, unsigned int bitDepth, ComponentID compID, int numIter, int iter = 0);
  int erosion   (PelStorage* buff, unsigned int bitDepth, ComponentID compID, int numIter, int iter = 0);

private:
  unsigned int m_kernelSize = 3;		// Dilation and erosion kernel size
};


class FGAnalyser
{
public:
  FGAnalyser();
  ~FGAnalyser();

  void init(const int width,
            const int height,
            const int sourcePaddingWidth,
            const int sourcePaddingHeight,
            const InputColourSpaceConversion ipCSC,
            const bool         clipInputVideoToRec709Range,
            const ChromaFormat inputChroma,
            const BitDepths& inputBitDepths,
            const BitDepths& outputBitDepths,
            const int frameSkip,
            const bool doAnalysis[],
            std::string filmGrainExternalMask,
            std::string filmGrainExternalDenoised);
  void destroy        ();
  void initBufs       (Picture* pic);
  void estimate_grain (Picture* pic);

  int                                     getLog2scaleFactor()  { return m_log2ScaleFactor; };
  SEIFilmGrainCharacteristics::CompModel  getCompModel(int idx) { return m_compModel[idx];  };

private:
  std::string                      m_filmGrainExternalMask     = "";
  std::string                      m_filmGrainExternalDenoised = "";
  int                              m_sourcePadding[2];
  InputColourSpaceConversion       m_ipCSC;
  bool                             m_clipInputVideoToRec709Range;
  BitDepths                        m_bitDepthsIn;
  int                              m_frameSkip;
  ChromaFormat                     m_chromaFormatIdc;
  BitDepths     m_bitDepths;
  bool          m_doAnalysis[MAX_NUM_COMPONENT] = { false, false, false };

  Canny    m_edgeDetector;
  Morph    m_morphOperation;
  double   m_lowIntensityRatio            = 0.1;                    // supress everything below 0.1*maxIntensityOffset

  static constexpr double m_tapFilter[3]  = { 1, 2, 1 };
  static constexpr double m_normTap       = 4.0;

  // fg model parameters
  int                                    m_log2ScaleFactor;
  SEIFilmGrainCharacteristics::CompModel m_compModel[MAX_NUM_COMPONENT];

  PelStorage *m_originalBuf = nullptr;
  PelStorage *m_workingBuf  = nullptr;
  PelStorage *m_maskBuf     = nullptr;

  void findMask                     ();

  void estimate_grain_parameters    ();
  void block_transform              (const PelStorage& buff1, std::vector<PelMatrix>& squared_dct_grain_block_list, int offsetX, int offsetY, unsigned int bitDepth, ComponentID compID);
  void estimate_cutoff_freq         (const std::vector<PelMatrix>& blocks, ComponentID compID);
  int  cutoff_frequency             (std::vector<double>& mean);
  void estimate_scaling_factors     (std::vector<int>& data_x, std::vector<int>& data_y, unsigned int bitDepth, ComponentID compID);
  bool fit_function                 (std::vector<int>& data_x, std::vector<int>& data_y, std::vector<double>& coeffs, std::vector<double>& scalingVec,
                                     int order, int bitDepth, bool second_pass);
  void avg_scaling_vec              (std::vector<double> &scalingVec, ComponentID compID, int bitDepth);
  bool lloyd_max                    (std::vector<double>& scalingVec, std::vector<int>& quantizedVec, double& distortion, int numQuantizedLevels, int bitDepth);
  void quantize                     (std::vector<double>& scalingVec, std::vector<double>& quantizedVec, double& distortion, std::vector<double> partition, std::vector<double> codebook);
  void extend_points                (std::vector<int>& data_x, std::vector<int>& data_y, int bitDepth);

  void setEstimatedParameters       (std::vector<int>& quantizedVec, unsigned int bitDepth, ComponentID compID);
  void define_intervals_and_scalings(std::vector<std::vector<int>>& parameters, std::vector<int>& quantizedVec, int bitDepth);
  void scale_down                   (std::vector<std::vector<int>>& parameters, int bitDepth);
  void confirm_intervals            (std::vector<std::vector<int>>& parameters);

  long double ldpow                 (long double n, unsigned p);
  int         meanVar               (PelStorage& buffer, int windowSize, ComponentID compID, int offsetX, int offsetY, bool getVar);
  int         count_edges           (PelStorage& buffer, int windowSize, ComponentID compID, int offsetX, int offsetY);

  void subsample                    (const PelStorage& input, PelStorage& output, ComponentID compID, const int factor = 2, const int padding = 0) const;
  void upsample                     (const PelStorage& input, PelStorage& output, ComponentID compID, const int factor = 2, const int padding = 0) const;
  void combineMasks                 (PelStorage& buff, PelStorage& buff2, ComponentID compID);
  void suppressLowIntensity         (const PelStorage& buff1, PelStorage& buff2, unsigned int bitDepth, ComponentID compID);

}; // END CLASS DEFINITION

//! \}

#endif // __SEIFILMGRAINANALYZER__


