/*****************************************************************************
 * This file is part of Kvazaar HEVC encoder.
 *
 * Copyright (C) 2013-2015 Tampere University of Technology and others (see
 * COPYING file).
 *
 * Kvazaar is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * Kvazaar is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Kvazaar.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

#include "strategies/avx2/intra-avx2.h"

#if COMPILE_INTEL_AVX2 && defined X86_64
#include "kvazaar.h"
#if KVZ_BIT_DEPTH == 8

#include <immintrin.h>
#include <stdlib.h>

#include "strategyselector.h"
#include "strategies/missing-intel-intrinsics.h"


 /**
 * \brief Generage angular predictions.
 * \param log2_width    Log2 of width, range 2..5.
 * \param intra_mode    Angular mode in range 2..34.
 * \param in_ref_above  Pointer to -1 index of above reference, length=width*2+1.
 * \param in_ref_left   Pointer to -1 index of left reference, length=width*2+1.
 * \param dst           Buffer of size width*width.
 */
static void kvz_angular_pred_avx2(
  const int_fast8_t log2_width,
  const int_fast8_t intra_mode,
  const int_fast8_t channel_type,
  const kvz_pixel *const in_ref_above,
  const kvz_pixel *const in_ref_left,
  kvz_pixel *const dst)
{
  
  assert(log2_width >= 2 && log2_width <= 5);
  assert(intra_mode >= 2 && intra_mode <= 66);

  static const int16_t modedisp2sampledisp[32] = { 0,    1,    2,    3,    4,    6,     8,   10,   12,   14,   16,   18,   20,   23,   26,   29,   32,   35,   39,  45,  51,  57,  64,  73,  86, 102, 128, 171, 256, 341, 512, 1024 };
  static const int16_t modedisp2invsampledisp[32] = { 0, 16384, 8192, 5461, 4096, 2731, 2048, 1638, 1365, 1170, 1024, 910, 819, 712, 630, 565, 512, 468, 420, 364, 321, 287, 256, 224, 191, 161, 128, 96, 64, 48, 32, 16 }; // (512 * 32) / sampledisp
  static const int32_t pre_scale[] = { 8, 7, 6, 5, 5, 4, 4, 4, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 0, 0, 0, -1, -1, -2, -3 };
  static const int16_t intraGaussFilter[32][4] = {
  { 16, 32, 16, 0 },
  { 15, 29, 17, 3 },
  { 15, 29, 17, 3 },
  { 14, 29, 18, 3 },
  { 13, 29, 18, 4 },
  { 13, 28, 19, 4 },
  { 13, 28, 19, 4 },
  { 12, 28, 20, 4 },
  { 11, 28, 20, 5 },
  { 11, 27, 21, 5 },
  { 10, 27, 22, 5 },
  { 9, 27, 22, 6 },
  { 9, 26, 23, 6 },
  { 9, 26, 23, 6 },
  { 8, 25, 24, 7 },
  { 8, 25, 24, 7 },
  { 8, 24, 24, 8 },
  { 7, 24, 25, 8 },
  { 7, 24, 25, 8 },
  { 6, 23, 26, 9 },
  { 6, 23, 26, 9 },
  { 6, 22, 27, 9 },
  { 5, 22, 27, 10 },
  { 5, 21, 27, 11 },
  { 5, 20, 28, 11 },
  { 4, 20, 28, 12 },
  { 4, 19, 28, 13 },
  { 4, 19, 28, 13 },
  { 4, 18, 29, 13 },
  { 3, 18, 29, 14 },
  { 3, 17, 29, 15 },
  { 3, 17, 29, 15 }
  };
  static const int16_t cubic_filter[32][4] =
  {
    { 0, 64,  0,  0 },
    { -1, 63,  2,  0 },
    { -2, 62,  4,  0 },
    { -2, 60,  7, -1 },
    { -2, 58, 10, -2 },
    { -3, 57, 12, -2 },
    { -4, 56, 14, -2 },
    { -4, 55, 15, -2 },
    { -4, 54, 16, -2 },
    { -5, 53, 18, -2 },
    { -6, 52, 20, -2 },
    { -6, 49, 24, -3 },
    { -6, 46, 28, -4 },
    { -5, 44, 29, -4 },
    { -4, 42, 30, -4 },
    { -4, 39, 33, -4 },
    { -4, 36, 36, -4 },
    { -4, 33, 39, -4 },
    { -4, 30, 42, -4 },
    { -4, 29, 44, -5 },
    { -4, 28, 46, -6 },
    { -3, 24, 49, -6 },
    { -2, 20, 52, -6 },
    { -2, 18, 53, -5 },
    { -2, 16, 54, -4 },
    { -2, 15, 55, -4 },
    { -2, 14, 56, -4 },
    { -2, 12, 57, -3 },
    { -2, 10, 58, -2 },
    { -1,  7, 60, -2 },
    { 0,  4, 62, -2 },
    { 0,  2, 63, -1 },
  };

                                                    // Temporary buffer for modes 11-25.
                                                    // It only needs to be big enough to hold indices from -width to width-1.
  kvz_pixel tmp_ref[2 * 128] = { 0 };
  kvz_pixel temp_main[2 * 128] = { 0 };
  kvz_pixel temp_side[2 * 128] = { 0 };
  const int_fast32_t width = 1 << log2_width;

  uint32_t pred_mode = intra_mode; // ToDo: handle WAIP

  // Whether to swap references to always project on the left reference row.
  const bool vertical_mode = intra_mode >= 34;
  // Modes distance to horizontal or vertical mode.
  const int_fast8_t mode_disp = vertical_mode ? pred_mode - 50 : -(pred_mode - 18);
  //const int_fast8_t mode_disp = vertical_mode ? intra_mode - 26 : 10 - intra_mode;
  
  // Sample displacement per column in fractions of 32.
  const int_fast8_t sample_disp = (mode_disp < 0 ? -1 : 1) * modedisp2sampledisp[abs(mode_disp)];
  
  // TODO: replace latter width with height
  int scale = MIN(2, log2_width - pre_scale[abs(mode_disp)]);

  // Pointer for the reference we are interpolating from.
  kvz_pixel *ref_main;
  // Pointer for the other reference.
  const kvz_pixel *ref_side;

  // Set ref_main and ref_side such that, when indexed with 0, they point to
  // index 0 in block coordinates.
  if (sample_disp < 0) {
    for (int i = 0; i <= width + 1; i++) {
      temp_main[width + i] = (vertical_mode ? in_ref_above[i] : in_ref_left[i]);
      temp_side[width + i] = (vertical_mode ? in_ref_left[i] : in_ref_above[i]);
    }

    ref_main = temp_main + width;
    ref_side = temp_side + width;

    for (int i = -width; i <= -1; i++) {
      ref_main[i] = ref_side[MIN((-i * modedisp2invsampledisp[abs(mode_disp)] + 256) >> 9, width)];
    }

    

    //const uint32_t index_offset = width + 1;
    //const int32_t last_index = width;
    //const int_fast32_t most_negative_index = (width * sample_disp) >> 5;
    //// Negative sample_disp means, we need to use both references.

    //// TODO: update refs to take into account variating block size and shapes
    ////       (height is not always equal to width)
    //ref_side = (vertical_mode ? in_ref_left : in_ref_above) + 1;
    //ref_main = (vertical_mode ? in_ref_above : in_ref_left) + 1;

    //// Move the reference pixels to start from the middle to the later half of
    //// the tmp_ref, so there is room for negative indices.
    //for (int_fast32_t x = -1; x < width; ++x) {
    //  tmp_ref[x + index_offset] = ref_main[x];
    //}
    //// Get a pointer to block index 0 in tmp_ref.
    //ref_main = &tmp_ref[index_offset];
    //tmp_ref[index_offset -1] = tmp_ref[index_offset];

    //// Extend the side reference to the negative indices of main reference.
    //int_fast32_t col_sample_disp = 128; // rounding for the ">> 8"
    //int_fast16_t inv_abs_sample_disp = modedisp2invsampledisp[abs(mode_disp)];
    //// TODO: add 'vertical_mode ? height : width' instead of 'width'
    //
    //for (int_fast32_t x = -1; x > most_negative_index; x--) {
    //  col_sample_disp += inv_abs_sample_disp;
    //  int_fast32_t side_index = col_sample_disp >> 8;
    //  tmp_ref[x + index_offset - 1] = ref_side[side_index - 1];
    //}
    //tmp_ref[last_index + index_offset] = tmp_ref[last_index + index_offset - 1];
    //tmp_ref[most_negative_index + index_offset - 1] = tmp_ref[most_negative_index + index_offset];
  }
  else {
    
    for (int i = 0; i <= (width << 1); i++) {
      temp_main[i] = (vertical_mode ? in_ref_above[i] : in_ref_left[i]);
      temp_side[i] = (vertical_mode ? in_ref_left[i] : in_ref_above[i]);
    }

    const int log2_ratio = 0;
    const int s = 0;
    const int max_index = (0 << s) + 2;
    const int ref_length = width << 1;
    const kvz_pixel val = temp_main[ref_length];
    for (int j = 0; j <= max_index; j++) {
      temp_main[ref_length + j] = val;
    }

    ref_main = temp_main;
    ref_side = temp_side;
    //// sample_disp >= 0 means we don't need to refer to negative indices,
    //// which means we can just use the references as is.
    //ref_main = (vertical_mode ? in_ref_above : in_ref_left) + 1;
    //ref_side = (vertical_mode ? in_ref_left : in_ref_above) + 1;

    //memcpy(tmp_ref + width, ref_main, (width*2) * sizeof(kvz_pixel));
    //ref_main = &tmp_ref[width];
    //tmp_ref[width-1] = tmp_ref[width];
    //int8_t last_index = 1 + width*2;
    //tmp_ref[width + last_index] = tmp_ref[width + last_index - 1];
  }

  if (sample_disp != 0) {
    // The mode is not horizontal or vertical, we have to do interpolation.

    int_fast32_t delta_pos = 0;
    int_fast32_t delta_int[4] = { 0 };
    int_fast32_t delta_fract[4] = { 0 };
    for (int_fast32_t y = 0; y + 3 < width; y += 4) {

      for (int yy = 0; yy < 4; ++yy) {
        delta_pos += sample_disp;
        delta_int[yy] = delta_pos >> 5;
        delta_fract[yy] = delta_pos & (32 - 1);
      }

      if ((abs(sample_disp) & 0x1F) != 0) {
        
        // Luma Channel
        if (channel_type == 0) {

          int32_t ref_main_index[4] = { 0 };
          int16_t f[4][4] = { { 0 } };

          for (int yy = 0; yy < 4; ++yy) {

            ref_main_index[yy] = delta_int[yy];
            bool use_cubic = true; // Default to cubic filter
            static const int kvz_intra_hor_ver_dist_thres[8] = { 24, 24, 24, 14, 2, 0, 0, 0 };
            int filter_threshold = kvz_intra_hor_ver_dist_thres[log2_width];
            int dist_from_vert_or_hor = MIN(abs(pred_mode - 50), abs(pred_mode - 18));
            if (dist_from_vert_or_hor > filter_threshold) {
              static const int16_t modedisp2sampledisp[32] = { 0,    1,    2,    3,    4,    6,     8,   10,   12,   14,   16,   18,   20,   23,   26,   29,   32,   35,   39,  45,  51,  57,  64,  73,  86, 102, 128, 171, 256, 341, 512, 1024 };
              const int_fast8_t mode_disp = (pred_mode >= 34) ? pred_mode - 50 : 18 - pred_mode;
              const int_fast8_t sample_disp = (mode_disp < 0 ? -1 : 1) * modedisp2sampledisp[abs(mode_disp)];
              if ((abs(sample_disp) & 0x1F) != 0)
              {
                use_cubic = false;
              }
            }
            const int16_t filter_coeff[4] = { 16 - (delta_fract[yy] >> 1), 32 - (delta_fract[yy] >> 1), 16 + (delta_fract[yy] >> 1), delta_fract[yy] >> 1 };
            int16_t *temp_f = use_cubic ? cubic_filter[delta_fract[yy]] : filter_coeff;
            memcpy(f[yy], temp_f, 4 * sizeof(*temp_f));
          }

          // Do 4-tap intra interpolation filtering
          for (int_fast32_t x = 0; x < width; x++) {

            for (int yy = 0; yy < 4; ++yy){

              kvz_pixel *p = &ref_main[ref_main_index[yy]];
              dst[(y + yy) * width + x] = CLIP_TO_PIXEL(((int32_t)(f[yy][0] * p[0]) + (int32_t)(f[yy][1] * p[1]) + (int32_t)(f[yy][2] * p[2]) + (int32_t)(f[yy][3] * p[3]) + 32) >> 6);
              ref_main_index[yy] += 1;
            }
          }
        }
        else {
        
          // Do linear filtering
          for (int yy = 0; yy < 4; ++yy) {
            for (int_fast32_t x = 0; x < width; ++x) {
              kvz_pixel ref1 = ref_main[x + delta_int[yy] + 1];
              kvz_pixel ref2 = ref_main[x + delta_int[yy] + 2];
              dst[(y + yy) * width + x] = ref1 + ((delta_fract[yy] * (ref2 - ref1) + 16) >> 5);
            }
          }
        }
      }
      else {
        // Just copy the integer samples
        for (int yy = 0; yy < 4; ++yy) {
          for (int_fast32_t x = 0; x < width; x++) {
            dst[(y + yy) * width + x] = ref_main[x + delta_int[yy] + 1];
          }
        }
      }

     
      // PDPC
      bool PDPC_filter = (width >= 4 || channel_type != 0);
      if (pred_mode > 1 && pred_mode < 67) {
        if (mode_disp < 0) {
          PDPC_filter = false;
        }
        else if (mode_disp > 0) {
          PDPC_filter = (scale >= 0);
        }
      }
      if(PDPC_filter) {
        for (int yy = 0; yy < 4; ++yy) {

          int inv_angle_sum = 256;
          for (int x = 0; x < MIN(3 << scale, width); x++) {
            inv_angle_sum += modedisp2invsampledisp[abs(mode_disp)];

            int wL = 32 >> (2 * x >> scale);
            const kvz_pixel left = ref_side[(y + yy) + (inv_angle_sum >> 9) + 1];
            dst[(y + yy) * width + x] = dst[(y + yy) * width + x] + ((wL * (left - dst[(y + yy) * width + x]) + 32) >> 6);
          }
        }
      }

        /*
      if (pred_mode == 2 || pred_mode == 66) {
        int wT = 16 >> MIN(31, ((y << 1) >> scale));
        for (int x = 0; x < width; x++) {
          int wL = 16 >> MIN(31, ((x << 1) >> scale));
          if (wT + wL == 0) break;
          int c = x + y + 1;
          if (c >= 2 * width) { wL = 0; }
          if (c >= 2 * width) { wT = 0; }
          const kvz_pixel left = (wL != 0) ? ref_side[c] : 0;
          const kvz_pixel top  = (wT != 0) ? ref_main[c] : 0;
          dst[y * width + x] = CLIP_TO_PIXEL((wL * left + wT * top + (64 - wL - wT) * dst[y * width + x] + 32) >> 6);
        }
      } else if (sample_disp == 0 || sample_disp >= 12) {
        int inv_angle_sum_0 = 2;
        for (int x = 0; x < width; x++) {
          inv_angle_sum_0 += modedisp2invsampledisp[abs(mode_disp)];
          int delta_pos_0 = inv_angle_sum_0 >> 2;
          int delta_frac_0 = delta_pos_0 & 63;
          int delta_int_0 = delta_pos_0 >> 6;
          int delta_y = y + delta_int_0 + 1;
          // TODO: convert to JVET_K0500_WAIP
          if (delta_y > width + width - 1) break;

          int wL = 32 >> MIN(31, ((x << 1) >> scale));
          if (wL == 0) break;
          const kvz_pixel *p = ref_side + delta_y - 1;
          kvz_pixel left = p[delta_frac_0 >> 5];
          dst[y * width + x] = CLIP_TO_PIXEL((wL * left + (64 - wL) * dst[y * width + x] + 32) >> 6);
        }
      }*/
    }
  }
  else {
    // Mode is horizontal or vertical, just copy the pixels.

    // TODO: update outer loop to use height instead of width
    for (int_fast32_t y = 0; y < width; ++y) {
      for (int_fast32_t x = 0; x < width; ++x) {
        dst[y * width + x] = ref_main[x + 1];
      }
      if ((width >= 4 || channel_type != 0) && sample_disp >= 0) {
        int scale = (log2_width + log2_width - 2) >> 2;
        const kvz_pixel top_left = ref_main[0];
        const kvz_pixel left = ref_side[1 + y];
        for (int i = 0; i < MIN(3 << scale, width); i++) {
          const int wL = 32 >> (2 * i >> scale);
          const kvz_pixel val = dst[y * width + i];
          dst[y * width + i] = CLIP_TO_PIXEL(val + ((wL * (left - top_left) + 32) >> 6));
        }
      }
    }
  }

  // Flip the block if this is was a horizontal mode.
  if (!vertical_mode) {
    for (int_fast32_t y = 0; y < width - 1; ++y) {
      for (int_fast32_t x = y + 1; x < width; ++x) {
        SWAP(dst[y * width + x], dst[x * width + y], kvz_pixel);
      }
    }
  }
}

/**
 * \brief Generate planar prediction.
 * \param log2_width    Log2 of width, range 2..5.
 * \param in_ref_above  Pointer to -1 index of above reference, length=width*2+1.
 * \param in_ref_left   Pointer to -1 index of left reference, length=width*2+1.
 * \param dst           Buffer of size width*width.
 */
static void kvz_intra_pred_planar_avx2(
  const int_fast8_t log2_width,
  const uint8_t *const ref_top,
  const uint8_t *const ref_left,
  uint8_t *const dst)
{
  assert(log2_width >= 2 && log2_width <= 5);

  const int_fast8_t width = 1 << log2_width;
  const uint8_t top_right = ref_top[width + 1];
  const uint8_t bottom_left = ref_left[width + 1];

  if (log2_width > 2) {
    
    __m128i v_width = _mm_set1_epi16(width);
    __m128i v_top_right = _mm_set1_epi16(top_right);
    __m128i v_bottom_left = _mm_set1_epi16(bottom_left);

    for (int y = 0; y < width; ++y) {

      __m128i x_plus_1 = _mm_setr_epi16(-7, -6, -5, -4, -3, -2, -1, 0);
      __m128i v_ref_left = _mm_set1_epi16(ref_left[y + 1]);
      __m128i y_plus_1 = _mm_set1_epi16(y + 1);

      for (int x = 0; x < width; x += 8) {
        x_plus_1 = _mm_add_epi16(x_plus_1, _mm_set1_epi16(8));
        __m128i v_ref_top = _mm_loadl_epi64((__m128i*)&(ref_top[x + 1]));
        v_ref_top = _mm_cvtepu8_epi16(v_ref_top);

        __m128i hor = _mm_add_epi16(_mm_mullo_epi16(_mm_sub_epi16(v_width, x_plus_1), v_ref_left), _mm_mullo_epi16(x_plus_1, v_top_right));
        __m128i ver = _mm_add_epi16(_mm_mullo_epi16(_mm_sub_epi16(v_width, y_plus_1), v_ref_top), _mm_mullo_epi16(y_plus_1, v_bottom_left));

        //dst[y * width + x] = ho

        __m128i chunk = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(ver, hor), v_width), (log2_width + 1));
        chunk = _mm_packus_epi16(chunk, chunk);
        _mm_storel_epi64((__m128i*)&(dst[y * width + x]), chunk);
      }
    }
  } else {
    // Only if log2_width == 2 <=> width == 4
    assert(width == 4);
    const __m128i rl_shufmask = _mm_setr_epi32(0x04040404, 0x05050505,
                                               0x06060606, 0x07070707);

    const __m128i xp1   = _mm_set1_epi32  (0x04030201);
    const __m128i yp1   = _mm_shuffle_epi8(xp1,   rl_shufmask);

    const __m128i rdist = _mm_set1_epi32  (0x00010203);
    const __m128i bdist = _mm_shuffle_epi8(rdist, rl_shufmask);

    const __m128i wid16 = _mm_set1_epi16  (width);
    const __m128i tr    = _mm_set1_epi8   (top_right);
    const __m128i bl    = _mm_set1_epi8   (bottom_left);

    uint32_t rt14    = *(const uint32_t *)(ref_top  + 1);
    uint32_t rl14    = *(const uint32_t *)(ref_left + 1);
    uint64_t rt14_64 = (uint64_t)rt14;
    uint64_t rl14_64 = (uint64_t)rl14;
    uint64_t rtl14   = rt14_64 | (rl14_64 << 32);

    __m128i rtl_v    = _mm_cvtsi64_si128   (rtl14);
    __m128i rt       = _mm_broadcastd_epi32(rtl_v);
    __m128i rl       = _mm_shuffle_epi8    (rtl_v,    rl_shufmask);

    __m128i rtrl_l   = _mm_unpacklo_epi8   (rt,       rl);
    __m128i rtrl_h   = _mm_unpackhi_epi8   (rt,       rl);

    __m128i bdrd_l   = _mm_unpacklo_epi8   (bdist,    rdist);
    __m128i bdrd_h   = _mm_unpackhi_epi8   (bdist,    rdist);

    __m128i hvs_lo   = _mm_maddubs_epi16   (rtrl_l,   bdrd_l);
    __m128i hvs_hi   = _mm_maddubs_epi16   (rtrl_h,   bdrd_h);

    __m128i xp1yp1_l = _mm_unpacklo_epi8   (xp1,      yp1);
    __m128i xp1yp1_h = _mm_unpackhi_epi8   (xp1,      yp1);
    __m128i trbl_lh  = _mm_unpacklo_epi8   (tr,       bl);

    __m128i addend_l = _mm_maddubs_epi16   (trbl_lh,  xp1yp1_l);
    __m128i addend_h = _mm_maddubs_epi16   (trbl_lh,  xp1yp1_h);

            addend_l = _mm_add_epi16       (addend_l, wid16);
            addend_h = _mm_add_epi16       (addend_h, wid16);

    __m128i sum_l    = _mm_add_epi16       (hvs_lo,   addend_l);
    __m128i sum_h    = _mm_add_epi16       (hvs_hi,   addend_h);

    // Shift right by log2_width + 1
    __m128i sum_l_t  = _mm_srli_epi16      (sum_l,    3);
    __m128i sum_h_t  = _mm_srli_epi16      (sum_h,    3);
    __m128i result   = _mm_packus_epi16    (sum_l_t,  sum_h_t);
    _mm_storeu_si128((__m128i *)dst, result);
  }
}

// Calculate the DC value for a 4x4 block. The algorithm uses slightly
// different addends, multipliers etc for different pixels in the block,
// but for a fixed-size implementation one vector wide, all the weights,
// addends etc can be preinitialized for each position.
static void pred_filtered_dc_4x4(const uint8_t *ref_top,
                                 const uint8_t *ref_left,
                                       uint8_t *out_block)
{
  const uint32_t rt_u32 = *(const uint32_t *)(ref_top  + 1);
  const uint32_t rl_u32 = *(const uint32_t *)(ref_left + 1);

  const __m128i zero    = _mm_setzero_si128();
  const __m128i twos    = _mm_set1_epi8(2);

  // Hack. Move 4 u8's to bit positions 0, 64, 128 and 192 in two regs, to
  // expand them to 16 bits sort of "for free". Set highest bits on all the
  // other bytes in vectors to zero those bits in the result vector.
  const __m128i rl_shuf_lo = _mm_setr_epi32(0x80808000, 0x80808080,
                                            0x80808001, 0x80808080);
  const __m128i rl_shuf_hi = _mm_add_epi8  (rl_shuf_lo, twos);

  // Every second multiplier is 1, because we want maddubs to calculate
  // a + bc = 1 * a + bc (actually 2 + bc). We need to fill a vector with
  // ((u8)2)'s for other stuff anyway, so that can also be used here.
  const __m128i mult_lo = _mm_setr_epi32(0x01030102, 0x01030103,
                                         0x01040103, 0x01040104);
  const __m128i mult_hi = _mm_setr_epi32(0x01040103, 0x01040104,
                                         0x01040103, 0x01040104);
  __m128i four         = _mm_cvtsi32_si128  (4);
  __m128i rt           = _mm_cvtsi32_si128  (rt_u32);
  __m128i rl           = _mm_cvtsi32_si128  (rl_u32);
  __m128i rtrl         = _mm_unpacklo_epi32 (rt, rl);

  __m128i sad0         = _mm_sad_epu8       (rtrl, zero);
  __m128i sad1         = _mm_shuffle_epi32  (sad0, _MM_SHUFFLE(1, 0, 3, 2));
  __m128i sad2         = _mm_add_epi64      (sad0, sad1);
  __m128i sad3         = _mm_add_epi64      (sad2, four);

  __m128i dc_64        = _mm_srli_epi64     (sad3, 3);
  __m128i dc_8         = _mm_broadcastb_epi8(dc_64);

  __m128i rl_lo        = _mm_shuffle_epi8   (rl, rl_shuf_lo);
  __m128i rl_hi        = _mm_shuffle_epi8   (rl, rl_shuf_hi);

  __m128i rt_lo        = _mm_unpacklo_epi8  (rt, zero);
  __m128i rt_hi        = zero;

  __m128i dc_addend    = _mm_unpacklo_epi8(dc_8, twos);

  __m128i dc_multd_lo  = _mm_maddubs_epi16(dc_addend,    mult_lo);
  __m128i dc_multd_hi  = _mm_maddubs_epi16(dc_addend,    mult_hi);

  __m128i rl_rt_lo     = _mm_add_epi16    (rl_lo,        rt_lo);
  __m128i rl_rt_hi     = _mm_add_epi16    (rl_hi,        rt_hi);

  __m128i res_lo       = _mm_add_epi16    (dc_multd_lo,  rl_rt_lo);
  __m128i res_hi       = _mm_add_epi16    (dc_multd_hi,  rl_rt_hi);

          res_lo       = _mm_srli_epi16   (res_lo,       2);
          res_hi       = _mm_srli_epi16   (res_hi,       2);

  __m128i final        = _mm_packus_epi16 (res_lo,       res_hi);
  _mm_storeu_si128((__m128i *)out_block, final);
}

static void pred_filtered_dc_8x8(const uint8_t *ref_top,
                                 const uint8_t *ref_left,
                                       uint8_t *out_block)
{
  const uint64_t rt_u64 = *(const uint64_t *)(ref_top  + 1);
  const uint64_t rl_u64 = *(const uint64_t *)(ref_left + 1);

  const __m128i zero128 = _mm_setzero_si128();
  const __m256i twos    = _mm256_set1_epi8(2);

  // DC multiplier is 2 at (0, 0), 3 at (*, 0) and (0, *), and 4 at (*, *).
  // There is a constant addend of 2 on each pixel, use values from the twos
  // register and multipliers of 1 for that, to use maddubs for an (a*b)+c
  // operation.
  const __m256i mult_up_lo = _mm256_setr_epi32(0x01030102, 0x01030103,
                                               0x01030103, 0x01030103,
                                               0x01040103, 0x01040104,
                                               0x01040104, 0x01040104);

  // The 6 lowest rows have same multipliers, also the DC values and addends
  // are the same so this works for all of those
  const __m256i mult_rest  = _mm256_permute4x64_epi64(mult_up_lo, _MM_SHUFFLE(3, 2, 3, 2));

  // Every 8-pixel row starts with the next pixel of ref_left. Along with
  // doing the shuffling, also expand u8->u16, ie. move bytes 0 and 1 from
  // ref_left to bit positions 0 and 128 in rl_up_lo, 2 and 3 to rl_up_hi,
  // etc. The places to be zeroed out are 0x80 instead of the usual 0xff,
  // because this allows us to form new masks on the fly by adding 0x02-bytes
  // to this mask and still retain the highest bits as 1 where things should
  // be zeroed out.
  const __m256i rl_shuf_up_lo = _mm256_setr_epi32(0x80808000, 0x80808080,
                                                  0x80808080, 0x80808080,
                                                  0x80808001, 0x80808080,
                                                  0x80808080, 0x80808080);
  // And don't waste memory or architectural regs, hope these instructions
  // will be placed in between the shuffles by the compiler to only use one
  // register for the shufmasks, and executed way ahead of time because their
  // regs can be renamed.
  const __m256i rl_shuf_up_hi = _mm256_add_epi8 (rl_shuf_up_lo, twos);
  const __m256i rl_shuf_dn_lo = _mm256_add_epi8 (rl_shuf_up_hi, twos);
  const __m256i rl_shuf_dn_hi = _mm256_add_epi8 (rl_shuf_dn_lo, twos);

  __m128i eight         = _mm_cvtsi32_si128     (8);
  __m128i rt            = _mm_cvtsi64_si128     (rt_u64);
  __m128i rl            = _mm_cvtsi64_si128     (rl_u64);
  __m128i rtrl          = _mm_unpacklo_epi64    (rt, rl);

  __m128i sad0          = _mm_sad_epu8          (rtrl, zero128);
  __m128i sad1          = _mm_shuffle_epi32     (sad0, _MM_SHUFFLE(1, 0, 3, 2));
  __m128i sad2          = _mm_add_epi64         (sad0, sad1);
  __m128i sad3          = _mm_add_epi64         (sad2, eight);

  __m128i dc_64         = _mm_srli_epi64        (sad3, 4);
  __m256i dc_8          = _mm256_broadcastb_epi8(dc_64);

  __m256i dc_addend     = _mm256_unpacklo_epi8  (dc_8, twos);

  __m256i dc_up_lo      = _mm256_maddubs_epi16  (dc_addend, mult_up_lo);
  __m256i dc_rest       = _mm256_maddubs_epi16  (dc_addend, mult_rest);

  // rt_dn is all zeros, as is rt_up_hi. This'll get us the rl and rt parts
  // in A|B, C|D order instead of A|C, B|D that could be packed into abcd
  // order, so these need to be permuted before adding to the weighed DC
  // values.
  __m256i rt_up_lo      = _mm256_cvtepu8_epi16   (rt);

  __m256i rlrlrlrl      = _mm256_broadcastq_epi64(rl);
  __m256i rl_up_lo      = _mm256_shuffle_epi8    (rlrlrlrl, rl_shuf_up_lo);

  // Everything ref_top is zero except on the very first row
  __m256i rt_rl_up_hi   = _mm256_shuffle_epi8    (rlrlrlrl, rl_shuf_up_hi);
  __m256i rt_rl_dn_lo   = _mm256_shuffle_epi8    (rlrlrlrl, rl_shuf_dn_lo);
  __m256i rt_rl_dn_hi   = _mm256_shuffle_epi8    (rlrlrlrl, rl_shuf_dn_hi);

  __m256i rt_rl_up_lo   = _mm256_add_epi16       (rt_up_lo, rl_up_lo);

  __m256i rt_rl_up_lo_2 = _mm256_permute2x128_si256(rt_rl_up_lo, rt_rl_up_hi, 0x20);
  __m256i rt_rl_up_hi_2 = _mm256_permute2x128_si256(rt_rl_up_lo, rt_rl_up_hi, 0x31);
  __m256i rt_rl_dn_lo_2 = _mm256_permute2x128_si256(rt_rl_dn_lo, rt_rl_dn_hi, 0x20);
  __m256i rt_rl_dn_hi_2 = _mm256_permute2x128_si256(rt_rl_dn_lo, rt_rl_dn_hi, 0x31);

  __m256i up_lo = _mm256_add_epi16(rt_rl_up_lo_2, dc_up_lo);
  __m256i up_hi = _mm256_add_epi16(rt_rl_up_hi_2, dc_rest);
  __m256i dn_lo = _mm256_add_epi16(rt_rl_dn_lo_2, dc_rest);
  __m256i dn_hi = _mm256_add_epi16(rt_rl_dn_hi_2, dc_rest);

          up_lo = _mm256_srli_epi16(up_lo, 2);
          up_hi = _mm256_srli_epi16(up_hi, 2);
          dn_lo = _mm256_srli_epi16(dn_lo, 2);
          dn_hi = _mm256_srli_epi16(dn_hi, 2);

  __m256i res_up = _mm256_packus_epi16(up_lo, up_hi);
  __m256i res_dn = _mm256_packus_epi16(dn_lo, dn_hi);

  _mm256_storeu_si256(((__m256i *)out_block) + 0, res_up);
  _mm256_storeu_si256(((__m256i *)out_block) + 1, res_dn);
}

static INLINE __m256i cvt_u32_si256(const uint32_t u)
{
  const __m256i zero = _mm256_setzero_si256();
  return _mm256_insert_epi32(zero, u, 0);
}

static void pred_filtered_dc_16x16(const uint8_t *ref_top,
                                   const uint8_t *ref_left,
                                         uint8_t *out_block)
{
  const __m128i rt_128 = _mm_loadu_si128((const __m128i *)(ref_top  + 1));
  const __m128i rl_128 = _mm_loadu_si128((const __m128i *)(ref_left + 1));

  const __m128i zero_128 = _mm_setzero_si128();
  const __m256i zero     = _mm256_setzero_si256();
  const __m256i twos     = _mm256_set1_epi8(2);

  const __m256i mult_r0  = _mm256_setr_epi32(0x01030102, 0x01030103,
                                             0x01030103, 0x01030103,
                                             0x01030103, 0x01030103,
                                             0x01030103, 0x01030103);

  const __m256i mult_left = _mm256_set1_epi16(0x0103);

  // Leftmost bytes' blend mask, to move bytes (pixels) from the leftmost
  // column vector to the result row
  const __m256i lm8_bmask = _mm256_setr_epi32(0xff, 0, 0, 0, 0xff, 0, 0, 0);

  __m128i sixteen = _mm_cvtsi32_si128(16);
  __m128i sad0_t  = _mm_sad_epu8 (rt_128, zero_128);
  __m128i sad0_l  = _mm_sad_epu8 (rl_128, zero_128);
  __m128i sad0    = _mm_add_epi64(sad0_t, sad0_l);

  __m128i sad1    = _mm_shuffle_epi32      (sad0, _MM_SHUFFLE(1, 0, 3, 2));
  __m128i sad2    = _mm_add_epi64          (sad0, sad1);
  __m128i sad3    = _mm_add_epi64          (sad2, sixteen);

  __m128i dc_64   = _mm_srli_epi64         (sad3, 5);
  __m256i dc_8    = _mm256_broadcastb_epi8 (dc_64);

  __m256i rt      = _mm256_cvtepu8_epi16   (rt_128);
  __m256i rl      = _mm256_cvtepu8_epi16   (rl_128);

  uint8_t rl0       = *(uint8_t *)(ref_left + 1);
  __m256i rl_r0     = cvt_u32_si256((uint32_t)rl0);

  __m256i rlrt_r0   = _mm256_add_epi16(rl_r0, rt);

  __m256i dc_addend = _mm256_unpacklo_epi8(dc_8, twos);
  __m256i r0        = _mm256_maddubs_epi16(dc_addend, mult_r0);
  __m256i left_dcs  = _mm256_maddubs_epi16(dc_addend, mult_left);

          r0        = _mm256_add_epi16    (r0,       rlrt_r0);
          r0        = _mm256_srli_epi16   (r0, 2);
  __m256i r0r0      = _mm256_packus_epi16 (r0, r0);
          r0r0      = _mm256_permute4x64_epi64(r0r0, _MM_SHUFFLE(3, 1, 2, 0));

  __m256i leftmosts = _mm256_add_epi16    (left_dcs,  rl);
          leftmosts = _mm256_srli_epi16   (leftmosts, 2);

  // Contain the leftmost column's bytes in both lanes of lm_8
  __m256i lm_8      = _mm256_packus_epi16 (leftmosts, zero);
          lm_8      = _mm256_permute4x64_epi64(lm_8,  _MM_SHUFFLE(2, 0, 2, 0));

  __m256i lm8_r1    = _mm256_srli_epi32       (lm_8, 8);
  __m256i r1r1      = _mm256_blendv_epi8      (dc_8, lm8_r1, lm8_bmask);
  __m256i r0r1      = _mm256_blend_epi32      (r0r0, r1r1, 0xf0);

  _mm256_storeu_si256((__m256i *)out_block, r0r1);

  // Starts from 2 because row 0 (and row 1) is handled separately
  __m256i lm8_l     = _mm256_bsrli_epi128     (lm_8, 2);
  __m256i lm8_h     = _mm256_bsrli_epi128     (lm_8, 3);
          lm_8      = _mm256_blend_epi32      (lm8_l, lm8_h, 0xf0);

  for (uint32_t y = 2; y < 16; y += 2) {
    __m256i curr_row = _mm256_blendv_epi8 (dc_8, lm_8, lm8_bmask);
    _mm256_storeu_si256((__m256i *)(out_block + (y << 4)), curr_row);
    lm_8 = _mm256_bsrli_epi128(lm_8, 2);
  }
}

static void pred_filtered_dc_32x32(const uint8_t *ref_top,
                                   const uint8_t *ref_left,
                                         uint8_t *out_block)
{
  const __m256i rt = _mm256_loadu_si256((const __m256i *)(ref_top  + 1));
  const __m256i rl = _mm256_loadu_si256((const __m256i *)(ref_left + 1));

  const __m256i zero = _mm256_setzero_si256();
  const __m256i twos = _mm256_set1_epi8(2);

  const __m256i mult_r0lo = _mm256_setr_epi32(0x01030102, 0x01030103,
                                              0x01030103, 0x01030103,
                                              0x01030103, 0x01030103,
                                              0x01030103, 0x01030103);

  const __m256i mult_left = _mm256_set1_epi16(0x0103);
  const __m256i lm8_bmask = cvt_u32_si256    (0xff);

  const __m256i bshif_msk = _mm256_setr_epi32(0x04030201, 0x08070605,
                                              0x0c0b0a09, 0x800f0e0d,
                                              0x03020100, 0x07060504,
                                              0x0b0a0908, 0x0f0e0d0c);
  __m256i debias = cvt_u32_si256(32);
  __m256i sad0_t = _mm256_sad_epu8         (rt,     zero);
  __m256i sad0_l = _mm256_sad_epu8         (rl,     zero);
  __m256i sad0   = _mm256_add_epi64        (sad0_t, sad0_l);

  __m256i sad1   = _mm256_permute4x64_epi64(sad0,   _MM_SHUFFLE(1, 0, 3, 2));
  __m256i sad2   = _mm256_add_epi64        (sad0,   sad1);
  __m256i sad3   = _mm256_shuffle_epi32    (sad2,   _MM_SHUFFLE(1, 0, 3, 2));
  __m256i sad4   = _mm256_add_epi64        (sad2,   sad3);
  __m256i sad5   = _mm256_add_epi64        (sad4,   debias);
  __m256i dc_64  = _mm256_srli_epi64       (sad5,   6);

  __m128i dc_64_ = _mm256_castsi256_si128  (dc_64);
  __m256i dc_8   = _mm256_broadcastb_epi8  (dc_64_);

  __m256i rtlo   = _mm256_unpacklo_epi8    (rt, zero);
  __m256i rllo   = _mm256_unpacklo_epi8    (rl, zero);
  __m256i rthi   = _mm256_unpackhi_epi8    (rt, zero);
  __m256i rlhi   = _mm256_unpackhi_epi8    (rl, zero);

  __m256i dc_addend = _mm256_unpacklo_epi8 (dc_8, twos);
  __m256i r0lo   = _mm256_maddubs_epi16    (dc_addend, mult_r0lo);
  __m256i r0hi   = _mm256_maddubs_epi16    (dc_addend, mult_left);
  __m256i c0dc   = r0hi;

          r0lo   = _mm256_add_epi16        (r0lo, rtlo);
          r0hi   = _mm256_add_epi16        (r0hi, rthi);

  __m256i rlr0   = _mm256_blendv_epi8      (zero, rl, lm8_bmask);
          r0lo   = _mm256_add_epi16        (r0lo, rlr0);

          r0lo   = _mm256_srli_epi16       (r0lo, 2);
          r0hi   = _mm256_srli_epi16       (r0hi, 2);
  __m256i r0     = _mm256_packus_epi16     (r0lo, r0hi);

  _mm256_storeu_si256((__m256i *)out_block, r0);

  __m256i c0lo   = _mm256_add_epi16        (c0dc, rllo);
  __m256i c0hi   = _mm256_add_epi16        (c0dc, rlhi);
          c0lo   = _mm256_srli_epi16       (c0lo, 2);
          c0hi   = _mm256_srli_epi16       (c0hi, 2);

  __m256i c0     = _mm256_packus_epi16     (c0lo, c0hi);

  // r0 already handled!
  for (uint32_t y = 1; y < 32; y++) {
    if (y == 16) {
      c0 = _mm256_permute4x64_epi64(c0, _MM_SHUFFLE(1, 0, 3, 2));
    } else {
      c0 = _mm256_shuffle_epi8     (c0, bshif_msk);
    }
    __m256i curr_row = _mm256_blendv_epi8 (dc_8, c0, lm8_bmask);
    _mm256_storeu_si256(((__m256i *)out_block) + y, curr_row);
  }
}

/**
* \brief Generage intra DC prediction with post filtering applied.
* \param log2_width    Log2 of width, range 2..5.
* \param in_ref_above  Pointer to -1 index of above reference, length=width*2+1.
* \param in_ref_left   Pointer to -1 index of left reference, length=width*2+1.
* \param dst           Buffer of size width*width.
*/
static void kvz_intra_pred_filtered_dc_avx2(
  const int_fast8_t log2_width,
  const uint8_t *ref_top,
  const uint8_t *ref_left,
        uint8_t *out_block)
{
  assert(log2_width >= 2 && log2_width <= 5);

  if (log2_width == 2) {
    pred_filtered_dc_4x4(ref_top, ref_left, out_block);
  } else if (log2_width == 3) {
    pred_filtered_dc_8x8(ref_top, ref_left, out_block);
  } else if (log2_width == 4) {
    pred_filtered_dc_16x16(ref_top, ref_left, out_block);
  } else if (log2_width == 5) {
    pred_filtered_dc_32x32(ref_top, ref_left, out_block);
  }
}

#endif //KVZ_BIT_DEPTH == 8
#endif //COMPILE_INTEL_AVX2 && defined X86_64

int kvz_strategy_register_intra_avx2(void* opaque, uint8_t bitdepth)
{
  bool success = true;
#if COMPILE_INTEL_AVX2 && defined X86_64
#if KVZ_BIT_DEPTH == 8
  if (bitdepth == 8) {
    success &= kvz_strategyselector_register(opaque, "angular_pred", "avx2", 40, &kvz_angular_pred_avx2);
    success &= kvz_strategyselector_register(opaque, "intra_pred_planar", "avx2", 40, &kvz_intra_pred_planar_avx2);
    success &= kvz_strategyselector_register(opaque, "intra_pred_filtered_dc", "avx2", 40, &kvz_intra_pred_filtered_dc_avx2);
  }
#endif //KVZ_BIT_DEPTH == 8
#endif //COMPILE_INTEL_AVX2 && defined X86_64
  return success;
}

