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
** $Id: ssr.h,v 1.1 2002/12/16 19:01:29 miguelfreitas Exp $
**/

#ifndef __SSR_H__
#define __SSR_H__

#ifdef __cplusplus
extern "C" {
#endif

#define SSR_BANDS 4
#define PQFTAPS 96

void ssr_decode(ssr_info *ssr, fb_info *fb, uint8_t window_sequence,
                uint8_t window_shape, uint8_t window_shape_prev,
                real_t *freq_in, real_t *time_out, real_t *overlap,
                real_t ipqf_buffer[SSR_BANDS][96/4],
                real_t *prev_fmd, uint16_t frame_len);


static void ssr_gain_control(ssr_info *ssr, real_t *data, real_t *output,
                             real_t *overlap, real_t *prev_fmd, uint8_t band,
                             uint8_t window_sequence, uint16_t frame_len);
static void ssr_gc_function(ssr_info *ssr, real_t *prev_fmd,
                            real_t *gc_function, uint8_t window_sequence,
                            uint16_t frame_len);


#ifdef __cplusplus
}
#endif
#endif
