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
** $Id: output.h,v 1.2 2002/12/16 19:00:51 miguelfreitas Exp $
**/

#ifndef __OUTPUT_H__
#define __OUTPUT_H__

#ifdef __cplusplus
extern "C" {
#endif

void* output_to_PCM(real_t **input,
                    void *samplebuffer,
                    uint8_t channels,
                    uint16_t frame_len,
                    uint8_t format);

static int64_t dither_output(uint8_t dithering, uint8_t shapingtype,
                             uint16_t i, double Sum, uint8_t k);

#ifdef __cplusplus
}
#endif
#endif
