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
** $Id: bits.c,v 1.4 2003/04/12 14:58:46 miguelfreitas Exp $
**/

#include "common.h"
#include "structs.h"

#include <stdlib.h>
#include <string.h>
#include "bits.h"

/* initialize buffer, call once before first getbits or showbits */
void faad_initbits(bitfile *ld, void *_buffer, uint32_t buffer_size)
{
    uint32_t tmp;

    ld->buffer = malloc((buffer_size+12)*sizeof(uint8_t));
    memset(ld->buffer, 0, (buffer_size+12)*sizeof(uint8_t));
    memcpy(ld->buffer, _buffer, buffer_size*sizeof(uint8_t));

    ld->buffer_size = buffer_size;

    tmp = getdword((uint32_t*)ld->buffer);
#ifndef ARCH_IS_BIG_ENDIAN
    BSWAP(tmp);
#endif
    ld->bufa = tmp;

    tmp = getdword((uint32_t*)ld->buffer + 1);
#ifndef ARCH_IS_BIG_ENDIAN
    BSWAP(tmp);
#endif
    ld->bufb = tmp;

    ld->start = (uint32_t*)ld->buffer;
    ld->tail = ((uint32_t*)ld->buffer + 2);

    ld->bits_left = 32;

    ld->bytes_used = 0;
    ld->no_more_reading = 0;
    ld->error = 0;
}

void faad_endbits(bitfile *ld)
{
    if (ld)
        if (ld->buffer) free(ld->buffer);
}


uint32_t faad_get_processed_bits(bitfile *ld)
{
    return 8 * (4*(ld->tail - ld->start) - 4) - (ld->bits_left);
}

uint8_t faad_byte_align(bitfile *ld)
{
    uint8_t remainder = (uint8_t)((32 - ld->bits_left) % 8);

    if (remainder)
    {
        faad_flushbits(ld, 8 - remainder);
        return (8 - remainder);
    }
    return 0;
}

/* rewind to beginning */
void faad_rewindbits(bitfile *ld)
{
    uint32_t tmp;

    tmp = ld->start[0];
#ifndef ARCH_IS_BIG_ENDIAN
    BSWAP(tmp);
#endif
    ld->bufa = tmp;

    tmp = ld->start[1];
#ifndef ARCH_IS_BIG_ENDIAN
    BSWAP(tmp);
#endif
    ld->bufb = tmp;
    ld->bits_left = 32;
    ld->tail = &ld->start[2];
    ld->bytes_used = 0;
    ld->no_more_reading = 0;
}

uint8_t *faad_getbitbuffer(bitfile *ld, uint32_t bits
                       DEBUGDEC)
{
    uint16_t i;
    uint8_t temp;
    uint16_t bytes = (uint16_t)bits / 8;
    uint8_t remainder = (uint8_t)bits % 8;

    uint8_t *buffer = (uint8_t*)malloc((bytes+1)*sizeof(uint8_t));

    for (i = 0; i < bytes; i++)
    {
        buffer[i] = (uint8_t)faad_getbits(ld, 8 DEBUGVAR(print,var,dbg));
    }

    if (remainder)
    {
        temp = (uint8_t)faad_getbits(ld, remainder DEBUGVAR(print,var,dbg)) << (8-remainder);

        buffer[bytes] = temp;
    }

    return buffer;
}

/* reversed bit reading routines, used for RVLC and HCR */
void faad_initbits_rev(bitfile *ld, void *buffer,
                       uint32_t bits_in_buffer)
{
    uint32_t tmp;
    int32_t index;

    ld->buffer_size = bit2byte(bits_in_buffer);

    index = (bits_in_buffer+31)/32 - 1;

    ld->start = (uint32_t*)buffer + index - 2;

    tmp = getdword((uint32_t*)buffer + index);
#ifndef ARCH_IS_BIG_ENDIAN
    BSWAP(tmp);
#endif
    ld->bufa = tmp;

    tmp = getdword((uint32_t*)buffer + index - 1);
#ifndef ARCH_IS_BIG_ENDIAN
    BSWAP(tmp);
#endif
    ld->bufb = tmp;

    ld->tail = (uint32_t*)buffer + index;

    ld->bits_left = bits_in_buffer % 32;
    if (ld->bits_left == 0)
        ld->bits_left = 32;

    ld->bytes_used = 0;
    ld->no_more_reading = 0;
    ld->error = 0;
}
