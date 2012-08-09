/*
** FAAD - Freeware Advanced Audio Decoder
** Copyright (C) 2002 M. Bakker
**  
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
** 
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software 
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
**
** $Id: specrec.c,v 1.3 2003/04/12 14:58:47 miguelfreitas Exp $
**/

/*
  Spectral reconstruction:
   - grouping/sectioning
   - inverse quantization
   - applying scalefactors
*/

#include "common.h"
#include "structs.h"

#include <string.h>
#include "specrec.h"
#include "syntax.h"
#include "data.h"
#include "iq_table.h"


#define bit_set(A, B) ((A) & (1<<(B)))

/* 4.5.2.3.4 */
/*
  - determine the number of windows in a window_sequence named num_windows
  - determine the number of window_groups named num_window_groups
  - determine the number of windows in each group named window_group_length[g]
  - determine the total number of scalefactor window bands named num_swb for
    the actual window type
  - determine swb_offset[swb], the offset of the first coefficient in
    scalefactor window band named swb of the window actually used
  - determine sect_sfb_offset[g][section],the offset of the first coefficient
    in section named section. This offset depends on window_sequence and
    scale_factor_grouping and is needed to decode the spectral_data().
*/
uint8_t window_grouping_info(faacDecHandle hDecoder, ic_stream *ics)
{
    uint8_t i, g;

    uint8_t sf_index = hDecoder->sf_index;

    switch (ics->window_sequence) {
    case ONLY_LONG_SEQUENCE:
    case LONG_START_SEQUENCE:
    case LONG_STOP_SEQUENCE:
        ics->num_windows = 1;
        ics->num_window_groups = 1;
        ics->window_group_length[ics->num_window_groups-1] = 1;
#ifdef LD_DEC
        if (hDecoder->object_type == LD)
        {
            if (hDecoder->frameLength == 512)
                ics->num_swb = num_swb_512_window[sf_index];
            else /* if (hDecoder->frameLength == 480) */
                ics->num_swb = num_swb_480_window[sf_index];
        } else {
#endif
            if (hDecoder->frameLength == 1024)
                ics->num_swb = num_swb_1024_window[sf_index];
            else /* if (hDecoder->frameLength == 960) */
                ics->num_swb = num_swb_960_window[sf_index];
#ifdef LD_DEC
        }
#endif

        /* preparation of sect_sfb_offset for long blocks */
        /* also copy the last value! */
#ifdef LD_DEC
        if (hDecoder->object_type == LD)
        {
            if (hDecoder->frameLength == 512)
            {
                for (i = 0; i < ics->num_swb; i++)
                {
                    ics->sect_sfb_offset[0][i] = swb_offset_512_window[sf_index][i];
                    ics->swb_offset[i] = swb_offset_512_window[sf_index][i];
                }
            } else /* if (hDecoder->frameLength == 480) */ {
                for (i = 0; i < ics->num_swb; i++)
                {
                    ics->sect_sfb_offset[0][i] = swb_offset_480_window[sf_index][i];
                    ics->swb_offset[i] = swb_offset_480_window[sf_index][i];
                }
            }
            ics->sect_sfb_offset[0][ics->num_swb] = hDecoder->frameLength;
            ics->swb_offset[ics->num_swb] = hDecoder->frameLength;
        } else {
#endif
            for (i = 0; i < ics->num_swb; i++)
            {
                ics->sect_sfb_offset[0][i] = swb_offset_1024_window[sf_index][i];
                ics->swb_offset[i] = swb_offset_1024_window[sf_index][i];
            }
            ics->sect_sfb_offset[0][ics->num_swb] = hDecoder->frameLength;
            ics->swb_offset[ics->num_swb] = hDecoder->frameLength;
#ifdef LD_DEC
        }
#endif
        return 0;
    case EIGHT_SHORT_SEQUENCE:
        ics->num_windows = 8;
        ics->num_window_groups = 1;
        ics->window_group_length[ics->num_window_groups-1] = 1;
        ics->num_swb = num_swb_128_window[sf_index];

        for (i = 0; i < ics->num_swb; i++)
            ics->swb_offset[i] = swb_offset_128_window[sf_index][i];
        ics->swb_offset[ics->num_swb] = hDecoder->frameLength/8;

        for (i = 0; i < ics->num_windows-1; i++) {
            if (bit_set(ics->scale_factor_grouping, 6-i) == 0)
            {
                ics->num_window_groups += 1;
                ics->window_group_length[ics->num_window_groups-1] = 1;
            } else {
                ics->window_group_length[ics->num_window_groups-1] += 1;
            }
        }

        /* preparation of sect_sfb_offset for short blocks */
        for (g = 0; g < ics->num_window_groups; g++)
        {
            uint16_t width;
            uint8_t sect_sfb = 0;
            uint16_t offset = 0;

            for (i = 0; i < ics->num_swb; i++)
            {
                if (i+1 == ics->num_swb)
                {
                    width = (hDecoder->frameLength/8) - swb_offset_128_window[sf_index][i];
                } else {
                    width = swb_offset_128_window[sf_index][i+1] -
                        swb_offset_128_window[sf_index][i];
                }
                width *= ics->window_group_length[g];
                ics->sect_sfb_offset[g][sect_sfb++] = offset;
                offset += width;
            }
            ics->sect_sfb_offset[g][sect_sfb] = offset;
        }
        return 0;
    default:
        return 1;
    }
}

/*
  For ONLY_LONG_SEQUENCE windows (num_window_groups = 1,
  window_group_length[0] = 1) the spectral data is in ascending spectral
  order.
  For the EIGHT_SHORT_SEQUENCE window, the spectral order depends on the
  grouping in the following manner:
  - Groups are ordered sequentially
  - Within a group, a scalefactor band consists of the spectral data of all
    grouped SHORT_WINDOWs for the associated scalefactor window band. To
    clarify via example, the length of a group is in the range of one to eight
    SHORT_WINDOWs.
  - If there are eight groups each with length one (num_window_groups = 8,
    window_group_length[0..7] = 1), the result is a sequence of eight spectra,
    each in ascending spectral order.
  - If there is only one group with length eight (num_window_groups = 1,
    window_group_length[0] = 8), the result is that spectral data of all eight
    SHORT_WINDOWs is interleaved by scalefactor window bands.
  - Within a scalefactor window band, the coefficients are in ascending
    spectral order.
*/
void quant_to_spec(ic_stream *ics, real_t *spec_data, uint16_t frame_len)
{
    uint8_t g, sfb, win;
    uint16_t width, bin;
    real_t *start_inptr, *start_win_ptr, *win_ptr;

    real_t tmp_spec[1024];
    real_t *tmp_spec_ptr, *spec_ptr;

    tmp_spec_ptr = tmp_spec;
    memset(tmp_spec_ptr, 0, frame_len*sizeof(real_t));

    spec_ptr = spec_data;
    tmp_spec_ptr = tmp_spec;
    start_win_ptr = tmp_spec_ptr;

    for (g = 0; g < ics->num_window_groups; g++)
    {
        uint16_t j = 0;
        uint16_t win_inc = 0;

        start_inptr = spec_ptr;

        win_inc = ics->swb_offset[ics->num_swb];

        for (sfb = 0; sfb < ics->num_swb; sfb++)
        {
            width = ics->swb_offset[sfb+1] - ics->swb_offset[sfb];

            win_ptr = start_win_ptr;

            for (win = 0; win < ics->window_group_length[g]; win++)
            {
                tmp_spec_ptr = win_ptr + j;

                for (bin = 0; bin < width; bin += 4)
                {
                    tmp_spec_ptr[0] = spec_ptr[0];
                    tmp_spec_ptr[1] = spec_ptr[1];
                    tmp_spec_ptr[2] = spec_ptr[2];
                    tmp_spec_ptr[3] = spec_ptr[3];
                    tmp_spec_ptr += 4;
                    spec_ptr += 4;
                }

                win_ptr += win_inc;
            }
            j += width;
        }
        start_win_ptr += (spec_ptr - start_inptr);
    }

    spec_ptr = spec_data;
    tmp_spec_ptr = tmp_spec;

    memcpy(spec_ptr, tmp_spec_ptr, frame_len*sizeof(real_t));
}

#ifndef FIXED_POINT
void build_tables(real_t *pow2_table)
{
    uint16_t i;

    /* build pow(2, 0.25*x) table for scalefactors */
    for(i = 0; i < POW_TABLE_SIZE; i++)
    {
        pow2_table[i] = REAL_CONST(pow(2.0, 0.25 * (i-100)));
    }
}
#endif

static INLINE real_t iquant(int16_t q)
{
    int16_t sgn = 1;

    if (q == 0) return 0;

    if (q < 0)
    {
        q = -q;
        sgn = -1;
    }

    if (q >= IQ_TABLE_SIZE)
        return sgn * iq_table[q>>3] * 16;

    return sgn * iq_table[q];
}

void inverse_quantization(real_t *x_invquant, int16_t *x_quant, uint16_t frame_len)
{
    int16_t i;
    int16_t *in_ptr = x_quant;
    real_t *out_ptr = x_invquant;

    for(i = frame_len/4-1; i >= 0; --i)
    {
        out_ptr[0] = iquant(in_ptr[0]);
        out_ptr[1] = iquant(in_ptr[1]);
        out_ptr[2] = iquant(in_ptr[2]);
        out_ptr[3] = iquant(in_ptr[3]);
        out_ptr += 4;
        in_ptr += 4;
    }
}

#ifndef FIXED_POINT
static INLINE real_t get_scale_factor_gain(uint16_t scale_factor, real_t *pow2_table)
{
    if (scale_factor < POW_TABLE_SIZE)
        return pow2_table[scale_factor];
    else
        return REAL_CONST(pow(2.0, 0.25 * (scale_factor - 100)));
}
#else
static real_t pow2_table[] =
{
    COEF_CONST(0.59460355750136),
    COEF_CONST(0.70710678118655),
    COEF_CONST(0.84089641525371),
    COEF_CONST(1.0),
    COEF_CONST(1.18920711500272),
    COEF_CONST(1.41421356237310),
    COEF_CONST(1.68179283050743)
};
#endif

#ifdef FIXED_POINT
void apply_scalefactors(ic_stream *ics, real_t *x_invquant, uint16_t frame_len)
#else
void apply_scalefactors(ic_stream *ics, real_t *x_invquant, real_t *pow2_table,
                        uint16_t frame_len)
#endif
{
    uint8_t g, sfb;
    uint16_t top;
    real_t *fp;
#ifndef FIXED_POINT
    real_t scale;
#else
    int32_t exp, frac;
#endif
    uint8_t groups = 0;
    uint16_t nshort = frame_len/8;

    for (g = 0; g < ics->num_window_groups; g++)
    {
        uint16_t k = 0;

        /* using this 128*groups doesn't hurt long blocks, because
           long blocks only have 1 group, so that means 'groups' is
           always 0 for long blocks
        */
        fp = x_invquant + (groups*nshort);

        for (sfb = 0; sfb < ics->max_sfb; sfb++)
        {
            top = ics->sect_sfb_offset[g][sfb+1];

#ifndef FIXED_POINT
            scale = get_scale_factor_gain(ics->scale_factors[g][sfb], pow2_table);
#else
            exp = (ics->scale_factors[g][sfb] - 100) / 4;
            frac = (ics->scale_factors[g][sfb] - 100) % 4;
#endif

            /* minimum size of a sf band is 4 and always a multiple of 4 */
            for ( ; k < top; k += 4)
            {
#ifndef FIXED_POINT
                fp[0] = MUL(fp[0],scale);
                fp[1] = MUL(fp[1],scale);
                fp[2] = MUL(fp[2],scale);
                fp[3] = MUL(fp[3],scale);
#else
                if (exp < 0)
                {
                    fp[0] >>= -exp;
                    fp[1] >>= -exp;
                    fp[2] >>= -exp;
                    fp[3] >>= -exp;
                } else {
                    fp[0] <<= exp;
                    fp[1] <<= exp;
                    fp[2] <<= exp;
                    fp[3] <<= exp;
                }

                if (frac)
                {
                    fp[0] = MUL_R_C(fp[0],pow2_table[frac + 3]);
                    fp[1] = MUL_R_C(fp[1],pow2_table[frac + 3]);
                    fp[2] = MUL_R_C(fp[2],pow2_table[frac + 3]);
                    fp[3] = MUL_R_C(fp[3],pow2_table[frac + 3]);
                }
#endif
                fp += 4;
            }
        }
        groups += ics->window_group_length[g];
    }
}
