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
** $Id: decoder.h,v 1.4 2003/08/25 21:51:41 f1rmb Exp $
**/

#ifndef __DECODER_H__
#define __DECODER_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
  #pragma pack(push, 8)
  #ifndef FAADAPI
    #define FAADAPI __cdecl
  #endif
#else
  #ifndef FAADAPI
    #define FAADAPI
  #endif
#endif

#include "bits.h"
#include "syntax.h"
#include "drc.h"
#include "specrec.h"
#include "filtbank.h"
#include "ic_predict.h"


/* library output formats */
#define FAAD_FMT_16BIT  1
#define FAAD_FMT_24BIT  2
#define FAAD_FMT_32BIT  3
#define FAAD_FMT_FLOAT  4
#define FAAD_FMT_DOUBLE 5
#define FAAD_FMT_16BIT_DITHER  6
#define FAAD_FMT_16BIT_L_SHAPE 7
#define FAAD_FMT_16BIT_M_SHAPE 8
#define FAAD_FMT_16BIT_H_SHAPE 9

#define FAAD_FMT_DITHER_LOWEST FAAD_FMT_16BIT_DITHER

#define LC_DEC_CAP            (1<<0)
#define MAIN_DEC_CAP          (1<<1)
#define LTP_DEC_CAP           (1<<2)
#define LD_DEC_CAP            (1<<3)
#define ERROR_RESILIENCE_CAP  (1<<4)
#define FIXED_POINT_CAP       (1<<5)

int8_t* FAADAPI faacDecGetErrorMessage(uint8_t errcode);

uint32_t FAADAPI faacDecGetCapabilities(void);

faacDecHandle FAADAPI faacDecOpen(void);

faacDecConfigurationPtr FAADAPI faacDecGetCurrentConfiguration(faacDecHandle hDecoder);

uint8_t FAADAPI faacDecSetConfiguration(faacDecHandle hDecoder,
                                    faacDecConfigurationPtr config);

/* Init the library based on info from the AAC file (ADTS/ADIF) */
int32_t FAADAPI faacDecInit(faacDecHandle hDecoder,
                            uint8_t *buffer,
                            uint32_t buffer_size,
                            uint32_t *samplerate,
                            uint8_t *channels);

/* Init the library using a DecoderSpecificInfo */
int8_t FAADAPI faacDecInit2(faacDecHandle hDecoder, uint8_t *pBuffer,
                         uint32_t SizeOfDecoderSpecificInfo,
                         uint32_t *samplerate, uint8_t *channels);

/* Init the library for DRM */
int8_t FAADAPI faacDecInitDRM(faacDecHandle hDecoder, uint32_t samplerate,
                              uint8_t channels);

void FAADAPI faacDecClose(faacDecHandle hDecoder);

void* FAADAPI faacDecDecode(faacDecHandle hDecoder,
                            faacDecFrameInfo *hInfo,
                            uint8_t *buffer,
                            uint32_t buffer_size);

element *decode_sce_lfe(faacDecHandle hDecoder,
                        faacDecFrameInfo *hInfo, bitfile *ld,
                        int16_t **spec_data, real_t **spec_coef,
                        uint8_t id_syn_ele);
element *decode_cpe(faacDecHandle hDecoder,
                    faacDecFrameInfo *hInfo, bitfile *ld,
                    int16_t **spec_data, real_t **spec_coef,
                    uint8_t id_syn_ele);
element **raw_data_block(faacDecHandle hDecoder, faacDecFrameInfo *hInfo,
                         bitfile *ld, element **elements,
                         int16_t **spec_data, real_t **spec_coef,
                         program_config *pce, drc_info *drc);

#ifdef _WIN32
  #pragma pack(pop)
#endif

#ifdef __cplusplus
}
#endif
#endif
