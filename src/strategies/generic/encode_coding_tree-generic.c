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

#include "strategyselector.h"

#include "cabac.h"
#include "context.h"
#include "encode_coding_tree-generic.h"
#include "encode_coding_tree.h"


 /**
 * \brief Encode block coefficients
 *
 * \param state     current encoder state
 * \param cabac     current cabac state
 * \param coeff     Input coefficients
 * \param width     Block width
 * \param type      plane type / luminance or chrominance
 * \param scan_mode    scan type (diag, hor, ver) DEPRECATED?
 *
 * This method encodes coefficients of a block
 *
 */
void kvz_encode_coeff_nxn_generic(encoder_state_t * const state,
  cabac_data_t * const cabac,
  const coeff_t *coeff,
  uint8_t width,
  uint8_t type,
  int8_t scan_mode,
  int8_t tr_skip) {
  //const encoder_control_t * const encoder = state->encoder_control;
  //int c1 = 1;
  uint8_t last_coeff_x = 0;
  uint8_t last_coeff_y = 0;
  int32_t i;
  // ToDo: large block support in VVC?
  uint32_t sig_coeffgroup_flag[32 * 32] = { 0 };

  int32_t scan_pos;
  //int32_t next_sig_pos;
  uint32_t blk_pos, pos_y, pos_x, sig, ctx_sig;

  // CONSTANTS


  const uint32_t log2_block_size = kvz_g_convert_to_bit[width] + 2;
  const uint32_t *scan =
    kvz_g_sig_last_scan[scan_mode][log2_block_size - 1];
  const uint32_t *scan_cg = g_sig_last_scan_cg[log2_block_size - 2][scan_mode];
  const uint32_t clipped_log2_size = log2_block_size > 4 ? 4 : log2_block_size;
  const uint32_t num_blk_side = width >> clipped_log2_size;


  // Init base contexts according to block type
  cabac_ctx_t *base_coeff_group_ctx = &(cabac->ctx.sig_coeff_group_model[(type == 0 ? 0 : 1) * 2]);
  

  unsigned scan_cg_last = -1;
  unsigned scan_pos_last = -1;

  for (int i = 0; i < width * width; i++) {
    if (coeff[scan[i]]) {
      scan_pos_last = i;
      sig_coeffgroup_flag[scan_cg[i >> clipped_log2_size]] = 1;
    }
  }
  scan_cg_last = scan_pos_last >> clipped_log2_size;

  int pos_last = scan[scan_pos_last];

  last_coeff_y = (uint8_t)(pos_last / width);
  last_coeff_x = (uint8_t)(pos_last - (last_coeff_y * width));


  // Code last_coeff_x and last_coeff_y
  kvz_encode_last_significant_xy(cabac,
    last_coeff_x,
    last_coeff_y,
    width,
    width,
    type,
    scan_mode);



  uint32_t quant_state_transition_table = 0; //ToDo: dep quant enable changes this
  int32_t quant_state = 0;
  uint8_t  ctx_offset[16];
  int32_t temp_diag = -1;
  int32_t temp_sum = -1;

  int32_t reg_bins = (width*width * 28) >> 4; //8 for 2x2

  // significant_coeff_flag
  for (i = scan_cg_last; i >= 0; i--) {

    //int32_t abs_coeff[64*64];
    int32_t cg_blk_pos = scan_cg[i];
    int32_t cg_pos_y = cg_blk_pos / (MIN((uint8_t)32, width) >> (clipped_log2_size / 2));
    int32_t cg_pos_x = cg_blk_pos - (cg_pos_y * (MIN((uint8_t)32, width) >> (clipped_log2_size / 2)));


    /*if (type == 0 && width <= 32) {
      if ((width == 32 && (cg_pos_x >= (16 >> clipped_log2_size))) || (width == 32 && (cg_pos_y >= (16 >> clipped_log2_size)))) {
        continue;
      }
    }*/






    // !!! residual_coding_subblock() !!!

    // Encode significant coeff group flag when not the last or the first
    if (i == scan_cg_last || i == 0) {
      sig_coeffgroup_flag[cg_blk_pos] = 1;
    } else {
      uint32_t sig_coeff_group = (sig_coeffgroup_flag[cg_blk_pos] != 0);
      uint32_t ctx_sig = kvz_context_get_sig_coeff_group(sig_coeffgroup_flag, cg_pos_x,
        cg_pos_y, (MIN((uint8_t)32, width) >> (clipped_log2_size / 2)));
      cabac->cur_ctx = &base_coeff_group_ctx[ctx_sig];
      CABAC_BIN(cabac, sig_coeff_group, "significant_coeffgroup_flag");
    }


    if (sig_coeffgroup_flag[cg_blk_pos]) {

      uint32_t next_pass = 0;
      int32_t min_sub_pos = i << clipped_log2_size; // LOG2_SCAN_SET_SIZE;
      int32_t first_sig_pos = (i == scan_cg_last) ? scan_pos_last : (min_sub_pos + (1 << clipped_log2_size) - 1);
      int32_t next_sig_pos = first_sig_pos;

      int32_t infer_sig_pos = (next_sig_pos != scan_pos_last) ? ((i != 0) ? min_sub_pos : -1) : next_sig_pos;
      int32_t num_non_zero = 0;
      int32_t last_nz_pos_in_cg = -1;
      int32_t first_nz_pos_in_cg = next_sig_pos;
      int32_t remainder_abs_coeff = -1;
      uint32_t coeff_signs = 0;


      /*
         ****  FIRST PASS ****
      */
      for (next_sig_pos = first_sig_pos; next_sig_pos >= min_sub_pos && reg_bins >= 4; next_sig_pos--) {


        blk_pos = scan[next_sig_pos];
        pos_y = blk_pos / width;
        pos_x = blk_pos - (pos_y * width);

        sig = (coeff[blk_pos] != 0) ? 1 : 0;
        if (num_non_zero || next_sig_pos != infer_sig_pos) {
          ctx_sig = kvz_context_get_sig_ctx_idx_abs(coeff, pos_x, pos_y, width, width, type, &temp_diag, &temp_sum);
          cabac_ctx_t* sig_ctx_luma = &(cabac->ctx.cu_sig_model_luma[MAX(0, quant_state - 1)][ctx_sig]);
          cabac_ctx_t* sig_ctx_chroma = &(cabac->ctx.cu_sig_model_chroma[MAX(0, quant_state - 1)][ctx_sig]);
          cabac->cur_ctx = (type == 0 ? sig_ctx_luma : sig_ctx_chroma);

          CABAC_BIN(cabac, sig, "sig_coeff_flag");
          reg_bins--;

        } else if (next_sig_pos != scan_pos_last) {
          ctx_sig = kvz_context_get_sig_ctx_idx_abs(coeff, pos_x, pos_y, width, width, type, &temp_diag, &temp_sum);
        }


        if (sig) {
          assert(next_sig_pos - min_sub_pos >= 0 && next_sig_pos - min_sub_pos < 16);
          uint8_t* offset = &ctx_offset[next_sig_pos - min_sub_pos];
          num_non_zero++;
          // ctxOffsetAbs()
          {
            *offset = 0;
            if (temp_diag != -1) {
              *offset = MIN(temp_sum, 4) + 1;
              *offset += (!temp_diag ? (type == 0 /* luma channel*/ ? 15 : 5) : type == 0 /* luma channel*/ ? temp_diag < 3 ? 10 : (temp_diag < 10 ? 5 : 0) : 0);
            }
          }


          last_nz_pos_in_cg = MAX(last_nz_pos_in_cg, next_sig_pos);
          first_nz_pos_in_cg = next_sig_pos;

          remainder_abs_coeff = abs(coeff[blk_pos]) - 1;

          // If shift sign pattern and add current sign
          coeff_signs = (next_sig_pos != scan_pos_last ? 2 * coeff_signs : coeff_signs) + (coeff[blk_pos] < 0);



          // Code "greater than 1" flag
          uint8_t gt1 = remainder_abs_coeff ? 1 : 0;
          cabac->cur_ctx = (type == 0) ? &(cabac->ctx.cu_gtx_flag_model_luma[1][*offset]) :
            &(cabac->ctx.cu_gtx_flag_model_chroma[1][*offset]);
          CABAC_BIN(cabac, gt1, "gt1_flag");
          reg_bins--;

          if (gt1) {
            remainder_abs_coeff -= 1;

            // Code coeff parity
            cabac->cur_ctx = (type == 0) ? &(cabac->ctx.cu_parity_flag_model_luma[*offset]) :
              &(cabac->ctx.cu_parity_flag_model_chroma[*offset]);
            CABAC_BIN(cabac, remainder_abs_coeff & 1, "par_flag");
            remainder_abs_coeff >>= 1;

            reg_bins--;
            uint8_t gt2 = remainder_abs_coeff ? 1 : 0;
            cabac->cur_ctx = (type == 0) ? &(cabac->ctx.cu_gtx_flag_model_luma[0][*offset]) :
              &(cabac->ctx.cu_gtx_flag_model_chroma[0][*offset]);
            CABAC_BIN(cabac, gt2, "gt2_flag");
            reg_bins--;
          }
        }

        quant_state = (quant_state_transition_table >> ((quant_state << 2) + ((coeff[blk_pos] & 1) << 1))) & 3;
      }


      /*
      ****  SECOND PASS: Go-rice  ****
      */
      uint32_t rice_param = 0;
      uint32_t pos0 = 0;
      for (scan_pos = first_sig_pos; scan_pos > next_sig_pos; scan_pos--) {
        blk_pos = scan[scan_pos];
        pos_y = blk_pos / width;
        pos_x = blk_pos - (pos_y * width);
        int32_t abs_sum = kvz_abs_sum(coeff, pos_x, pos_y, width, width, 4);

        rice_param = g_go_rice_pars[abs_sum];
        uint32_t second_pass_abs_coeff = abs(coeff[blk_pos]);
        if (second_pass_abs_coeff >= 4) {
          uint32_t remainder = (second_pass_abs_coeff - 4) >> 1;
          kvz_cabac_write_coeff_remain(cabac, remainder, rice_param, 5);
        }
      }

      /*
      ****  coeff bypass  ****
      */
      for (scan_pos = next_sig_pos; scan_pos >= min_sub_pos; scan_pos--) {
        blk_pos = scan[scan_pos];
        pos_y = blk_pos / width;
        pos_x = blk_pos - (pos_y * width);
        uint32_t coeff_abs = abs(coeff[blk_pos]);
        int32_t abs_sum = kvz_abs_sum(coeff, pos_x, pos_y, width, width, 0);
        rice_param = g_go_rice_pars[abs_sum];        
        pos0 = ((quant_state<2)?1:2) << rice_param;
        uint32_t remainder = (coeff_abs == 0 ? pos0 : coeff_abs <= pos0 ? coeff_abs - 1 : coeff_abs);
        kvz_cabac_write_coeff_remain(cabac, remainder, rice_param, 5);
        quant_state = (quant_state_transition_table >> ((quant_state << 2) + ((coeff_abs & 1) << 1))) & 3;
        if (coeff_abs) {
          num_non_zero++;
          last_nz_pos_in_cg = MAX(last_nz_pos_in_cg, scan_pos);
          coeff_signs <<= 1;
          if (coeff[blk_pos] < 0) coeff_signs++;
        }
      }

      uint32_t num_signs = num_non_zero;

      if (state->encoder_control->cfg.signhide_enable && (last_nz_pos_in_cg - first_nz_pos_in_cg >= 4)) {
        num_signs--;
        coeff_signs >>= 1;
      }

      CABAC_BINS_EP(cabac, coeff_signs, num_signs, "coeff_signs");
    }
  }
}


int kvz_strategy_register_encode_generic(void* opaque, uint8_t bitdepth)
{
  bool success = true;

  success &= kvz_strategyselector_register(opaque, "encode_coeff_nxn", "generic", 0, &kvz_encode_coeff_nxn_generic);

  return success;
}