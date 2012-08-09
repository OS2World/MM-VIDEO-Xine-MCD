/*
 * Copyright (C) 2000-2001 the xine project
 *
 * This file is part of xine, a free video player.
 *
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * ADPCM Decoders by Mike Melanson (melanson@pcisys.net)
 *
 * This file is in charge of decoding all of the various ADPCM data
 * formats that various entities have created. Details about the data
 * formats can be found here:
 *   http://www.pcisys.net/~melanson/codecs/
 * CD-ROM/XA ADPCM decoder by Stuart Caie (kyzer@4u.net)
 * - based on information in the USENET post by Jac Goudsmit (jac@codim.nl)
 *   <01bbc34c$dbf64020$f9c8a8c0@cray.codim.nl>
 * - tested for correctness using Jon Atkins's CDXA software:
 *   http://jonatkins.org/cdxa/
 *   this is also useful for extracting streams from Playstation discs
 *
 *
 * $Id: adpcm.c,v 1.31 2003/06/06 14:29:41 mroi Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "xine_internal.h"
#include "video_out.h"
#include "audio_out.h"
#include "buffer.h"
#include "xineutils.h"
#include "bswap.h"

/* pertinent tables */
static int ima_adpcm_step[89] = {
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
  19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
  50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
  130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
  337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
  876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
  2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
  5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
  15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static int dialogic_ima_step[49] = {
  16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
  50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
  157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
  494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552
};

static int ima_adpcm_index[16] = {
  -1, -1, -1, -1, 2, 4, 6, 8,
  -1, -1, -1, -1, 2, 4, 6, 8
};

static int ms_adapt_table[] = {
  230, 230, 230, 230, 307, 409, 512, 614,
  768, 614, 512, 409, 307, 230, 230, 230
};

static int ms_adapt_coeff1[] = {
  256, 512, 0, 192, 240, 460, 392
};

static int ms_adapt_coeff2[] = {
  0, -256, 0, 64, 0, -208, -232
};

static int ea_adpcm_table[] = {
  0, 240, 460, 392, 0, 0, -208, -220, 0, 1,
  3, 4, 7, 8, 10, 11, 0, -1, -3, -4
};

static int xa_adpcm_table[] = {
  0, 240, 460, 392, 0, 0, -208, -220
};

#define QT_IMA_ADPCM_PREAMBLE_SIZE 2
#define QT_IMA_ADPCM_BLOCK_SIZE 0x22
#define QT_IMA_ADPCM_SAMPLES_PER_BLOCK \
  ((QT_IMA_ADPCM_BLOCK_SIZE - QT_IMA_ADPCM_PREAMBLE_SIZE) * 2)

#define MS_ADPCM_PREAMBLE_SIZE 7
#define MS_IMA_ADPCM_PREAMBLE_SIZE 4
#define DK4_ADPCM_PREAMBLE_SIZE 4
#define DK3_ADPCM_PREAMBLE_SIZE 16

/* useful macros */
/* clamp a number between 0 and 88 */
#define CLAMP_0_TO_88(x)  if (x < 0) x = 0; else if (x > 88) x = 88;
/* clamp a number within a signed 16-bit range */
#define CLAMP_S16(x)  if (x < -32768) x = -32768; \
  else if (x > 32767) x = 32767;
/* clamp a number above 16 */
#define CLAMP_ABOVE_16(x)  if (x < 16) x = 16;
/* sign extend a 16-bit value */
#define SE_16BIT(x)  if (x & 0x8000) x -= 0x10000;
/* sign extend a 4-bit value */
#define SE_4BIT(x)  if (x & 0x8) x -= 0x10;

#define AUDIOBUFSIZE 128*1024

typedef struct {
  audio_decoder_class_t   decoder_class;
} adpcm_class_t;

typedef struct adpcm_decoder_s {
  audio_decoder_t  audio_decoder;

  xine_stream_t    *stream;

  uint32_t         rate;
  uint32_t         bits_per_sample;
  uint32_t         channels;
  uint32_t         ao_cap_mode;

  unsigned int     buf_type;
  int              output_open;

  unsigned char    *buf;
  int               bufsize;
  int               size;

  /* these fields are used for decoding ADPCM data transported in MS file */
  unsigned short   *decode_buffer;
  unsigned int      in_block_size;
  unsigned int      out_block_size;  /* size in samples (2 bytes/sample) */

  int              xa_mode; /* 1 for mode A, 0 for mode B or mode C */
  int              xa_p_l;  /* previous sample, left/mono channel */
  int              xa_p_r;  /* previous sample, right channel */
  int              xa_pp_l; /* 2nd-previous sample, left/mono channel */
  int              xa_pp_r; /* 2nd-previous sample, right channel */

} adpcm_decoder_t;

/*
 * decode_ima_nibbles
 *
 * So many different audio encoding formats leverage off of the IMA
 * ADPCM algorithm that it makes sense to create a function that takes
 * care of handling the common decoding portion.
 *
 * This function takes a buffer of ADPCM nibbles that are stored in an
 * array of signed 16-bit numbers. The function then decodes the nibbles
 * in place so that the buffer contains the decoded audio when the function
 * is finished. 
 *
 * The addresses of the initial predictor and index values are passed,
 * rather than their values, so that the function can return the final
 * predictor and index values after decoding. This is done in case the 
 * calling function cares (in the case of IMA ADPCM from Westwood Studios' 
 * VQA files, the values are initialized to 0 at the beginning of the file
 * and maintained throughout all of the IMA blocks).
 */
static void decode_ima_nibbles(unsigned short *output,
  int output_size, int channels,
  int *predictor_l, int *index_l,
  int *predictor_r, int *index_r) {

  int step[2];
  int predictor[2];
  int index[2];
  int diff;
  int i;
  int sign;
  int delta;
  int channel_number = 0;

  /* take care of the left */
  step[0] = ima_adpcm_step[*index_l];
  predictor[0] = *predictor_l;
  index[0] = *index_l;

  /* only handle the right if non-NULL pointers */
  if (index_r) {
    step[1] = ima_adpcm_step[*index_r];
    predictor[1] = *predictor_r;
    index[1] = *index_r;
  }

  for (i = 0; i < output_size; i++) {
    delta = output[i];

    index[channel_number] += ima_adpcm_index[delta];
    CLAMP_0_TO_88(index[channel_number]);

    sign = delta & 8;
    delta = delta & 7;

    diff = step[channel_number] >> 3;
    if (delta & 4) diff += step[channel_number];
    if (delta & 2) diff += step[channel_number] >> 1;
    if (delta & 1) diff += step[channel_number] >> 2;

    if (sign)
      predictor[channel_number] -= diff;
    else
      predictor[channel_number] += diff;

    CLAMP_S16(predictor[channel_number]);
    output[i] = predictor[channel_number];
    step[channel_number] = ima_adpcm_step[index[channel_number]];

    /* toggle channel */
    channel_number ^= channels - 1;
  }

  /* save the index and predictor values in case the calling function cares */
  *predictor_l = predictor[0];
  *index_l = index[0];

  /* only save the right channel information if pointers are non-NULL */
  if (predictor_r) {
    *predictor_r = predictor[1];
    *index_r = index[1];
  }
}

#define DK3_GET_NEXT_NIBBLE() \
    if (decode_top_nibble_next) \
    { \
      nibble = (last_byte >> 4) & 0x0F; \
      decode_top_nibble_next = 0; \
    } \
    else \
    { \
      last_byte = this->buf[i + j++]; \
      if (j > this->in_block_size) break; \
      nibble = last_byte & 0x0F; \
      decode_top_nibble_next = 1; \
    }

static void dk3_adpcm_decode_block(adpcm_decoder_t *this, buf_element_t *buf) {

  int i, j;
  audio_buffer_t *audio_buffer;
  int bytes_to_send;

  int sum_pred;
  int diff_pred;
  int sum_index;
  int diff_index;
  int diff_channel;
  int out_ptr;

  unsigned char last_byte = 0;
  unsigned char nibble;
  int decode_top_nibble_next = 0;

  /* ADPCM work variables */
  int sign;
  int delta;
  int step;
  int diff;

  /* make sure the input size checks out */
  if ((this->size % this->in_block_size) != 0) {
#ifdef LOG
    printf ("adpcm: received DK3 ADPCM block that does not line up\n");
#endif
    this->size = 0;
    return;
  }

  /* iterate through each block in the in buffer */
  for (i = 0; i < this->size; i += this->in_block_size) {

    sum_pred = LE_16(&this->buf[i + 10]);
    diff_pred = LE_16(&this->buf[i + 12]);
    SE_16BIT(sum_pred);
    SE_16BIT(diff_pred);
    diff_channel = diff_pred;
    sum_index = this->buf[i + 14];
    diff_index = this->buf[i + 15];

    j = DK3_ADPCM_PREAMBLE_SIZE;  /* start past the preamble */
    out_ptr = 0;
    last_byte = 0;
    decode_top_nibble_next = 0;
    while (j < this->in_block_size) {

      /* process the first predictor of the sum channel */
      DK3_GET_NEXT_NIBBLE();

      step = ima_adpcm_step[sum_index];

      sign = nibble & 8;
      delta = nibble & 7;

      diff = step >> 3;
      if (delta & 4) diff += step;
      if (delta & 2) diff += step >> 1;
      if (delta & 1) diff += step >> 2;

      if (sign)
        sum_pred -= diff;
      else
        sum_pred += diff;

      CLAMP_S16(sum_pred);

      sum_index += ima_adpcm_index[nibble];
      CLAMP_0_TO_88(sum_index);

      /* process the diff channel predictor */
      DK3_GET_NEXT_NIBBLE();

      step = ima_adpcm_step[diff_index];

      sign = nibble & 8;
      delta = nibble & 7;

      diff = step >> 3;
      if (delta & 4) diff += step;
      if (delta & 2) diff += step >> 1;
      if (delta & 1) diff += step >> 2;

      if (sign)
        diff_pred -= diff;
      else
        diff_pred += diff;

      CLAMP_S16(diff_pred);

      diff_index += ima_adpcm_index[nibble];
      CLAMP_0_TO_88(diff_index);

      /* output the first pair of stereo PCM samples */
      diff_channel = (diff_channel + diff_pred) / 2;
      this->decode_buffer[out_ptr++] = sum_pred + diff_channel;
      this->decode_buffer[out_ptr++] = sum_pred - diff_channel;

      /* process the second predictor of the sum channel */
      DK3_GET_NEXT_NIBBLE();

      step = ima_adpcm_step[sum_index];

      sign = nibble & 8;
      delta = nibble & 7;

      diff = step >> 3;
      if (delta & 4) diff += step;
      if (delta & 2) diff += step >> 1;
      if (delta & 1) diff += step >> 2;

      if (sign)
        sum_pred -= diff;
      else
        sum_pred += diff;

      CLAMP_S16(sum_pred);

      sum_index += ima_adpcm_index[nibble];
      CLAMP_0_TO_88(sum_index);

      /* output the second pair of stereo PCM samples */
      this->decode_buffer[out_ptr++] = sum_pred + diff_channel;
      this->decode_buffer[out_ptr++] = sum_pred - diff_channel;
    }

    /* dispatch the decoded audio */
    j = 0;
    while (j < out_ptr) {
      audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);
      if (audio_buffer->mem_size == 0) {
#ifdef LOG
        printf ("adpcm: Help! Allocated audio buffer with nothing in it!\n");
#endif
        return;
      }

      /* out_ptr and j are sample counts, mem_size is a byte count */
      if (((out_ptr - j) * 2) > audio_buffer->mem_size)
        bytes_to_send = audio_buffer->mem_size;
      else
        bytes_to_send = (out_ptr - j) * 2;

      xine_fast_memcpy(audio_buffer->mem, &this->decode_buffer[j],
        bytes_to_send);
      /* byte count / 2 (bytes / sample) / channels */
      audio_buffer->num_frames = bytes_to_send / 2 / this->channels;

      audio_buffer->vpts = buf->pts;
      buf->pts = 0;  /* only first buffer gets the real pts */
      this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);

      j += bytes_to_send / 2;  /* 2 bytes per sample */
    }
  }

  /* reset buffer */
  this->size = 0;
}

static void dk4_adpcm_decode_block(adpcm_decoder_t *this, buf_element_t *buf) {

  int predictor_l = 0;
  int predictor_r = 0;
  int index_l = 0;
  int index_r = 0;

  int i, j;
  unsigned int out_ptr = 0;
  audio_buffer_t *audio_buffer;
  int bytes_to_send;

  /* make sure the input size checks out */
  if ((this->size % this->in_block_size) != 0) {
#ifdef LOG
    printf ("adpcm: received DK4 ADPCM block that does not line up\n");
#endif
    this->size = 0;
    return;
  }

  /* iterate through each block in the in buffer */
  for (i = 0; i < this->size; i += this->in_block_size) {

    out_ptr = 0;

    /* the first predictor value goes straight to the output */
    predictor_l = this->decode_buffer[0] = LE_16(&this->buf[i + 0]);
    SE_16BIT(predictor_l);
    index_l = this->buf[i + 2];
    if (this->channels == 2) {
      predictor_r = this->decode_buffer[1] = LE_16(&this->buf[i + 4]);
      SE_16BIT(predictor_r);
      index_r = this->buf[i + 6];
    }

    /* break apart the ADPCM nibbles */
    out_ptr = this->channels;
    for (j = DK4_ADPCM_PREAMBLE_SIZE * this->channels; 
      j < this->in_block_size; j++) {
      this->decode_buffer[out_ptr++] = this->buf[i + j] >> 4;
      this->decode_buffer[out_ptr++] = this->buf[i + j] & 0x0F;
    }

    /* process the nibbles */
    decode_ima_nibbles(&this->decode_buffer[this->channels],
      out_ptr - this->channels,
      this->channels,
      &predictor_l, &index_l,
      &predictor_r, &index_r);

    /* dispatch the decoded audio */
    j = 0;
    while (j < out_ptr) {
      audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);
      if (audio_buffer->mem_size == 0) {
#ifdef LOG
        printf ("adpcm: Help! Allocated audio buffer with nothing in it!\n");
#endif
        return;
      }

      /* out_ptr and j are sample counts, mem_size is a byte count */
      if (((out_ptr - j) * 2) > audio_buffer->mem_size)
        bytes_to_send = audio_buffer->mem_size;
      else
        bytes_to_send = (out_ptr - j) * 2;

      xine_fast_memcpy(audio_buffer->mem, &this->decode_buffer[j],
        bytes_to_send);
      /* byte count / 2 (bytes / sample) / channels */
      audio_buffer->num_frames = bytes_to_send / 2 / this->channels;

      audio_buffer->vpts = buf->pts;
      buf->pts = 0;  /* only first buffer gets the real pts */
      this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);

      j += bytes_to_send / 2;  /* 2 bytes per sample */
    }
  }

  /* reset buffer */
  this->size = 0;
}

static void ms_ima_adpcm_decode_block(adpcm_decoder_t *this, 
  buf_element_t *buf) {

  int predictor_l = 0;
  int predictor_r = 0;
  int index_l = 0;
  int index_r = 0;
  int channel_counter;
  int channel_index;
  int channel_index_l;
  int channel_index_r;

  int i, j;
  audio_buffer_t *audio_buffer;
  int bytes_to_send;

  /* check the size */
  if ((this->size % this->in_block_size) != 0) {
#ifdef LOG
    printf ("adpcm: received MS IMA block that does not line up\n");
#endif
    this->size = 0;
    return;
  }

  /* iterate through each block in the in buffer */
  for (i = 0; i < this->size; i += this->in_block_size) {

    /* initialize algorithm for this block */
    predictor_l = LE_16(&this->buf[i]);
    SE_16BIT(predictor_l);
    index_l = this->buf[i + 2];
    if (this->channels == 2) {
      predictor_r = LE_16(&this->buf[i + MS_IMA_ADPCM_PREAMBLE_SIZE]);
      SE_16BIT(predictor_r);
      index_r = this->buf[i + MS_IMA_ADPCM_PREAMBLE_SIZE + 2];
    }

    /* break apart all of the nibbles in the block */
    if (this->channels == 1) {
      for (j = 0;
        j < (this->in_block_size - MS_IMA_ADPCM_PREAMBLE_SIZE) / 2; j++) {
        this->decode_buffer[j * 2 + 0] =
          this->buf[i + MS_IMA_ADPCM_PREAMBLE_SIZE + j] & 0x0F;
        this->decode_buffer[j * 2 + 1] =
          this->buf[i + MS_IMA_ADPCM_PREAMBLE_SIZE + j] >> 4;
      }
    } else {
      /* encoded as 8 nibbles (4 bytes) per channel; switch channel every
       * 4th byte */
      channel_counter = 0;
      channel_index_l = 0;
      channel_index_r = 1;
      channel_index = channel_index_l;
      for (j = 0;
        j < (this->in_block_size - MS_IMA_ADPCM_PREAMBLE_SIZE * 2); j++) {
        this->decode_buffer[channel_index + 0] =
          this->buf[i + MS_IMA_ADPCM_PREAMBLE_SIZE * 2 + j] & 0x0F;
        this->decode_buffer[channel_index + 2] =
          this->buf[i + MS_IMA_ADPCM_PREAMBLE_SIZE * 2 + j] >> 4;
        channel_index += 4;
        channel_counter++;
        if (channel_counter == 4) {
          channel_index_l = channel_index;
          channel_index = channel_index_r;
        } else if (channel_counter == 8) {
          channel_index_r = channel_index;
          channel_index = channel_index_l;
          channel_counter = 0;
        }
      }
    }

    /* process the nibbles */
    decode_ima_nibbles(this->decode_buffer,
      this->out_block_size,
      this->channels,
      &predictor_l, &index_l,
      &predictor_r, &index_r);

    /* dispatch the decoded audio */
    j = 0;
    while (j < this->out_block_size) {
      audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);
      if (audio_buffer->mem_size == 0) {
#ifdef LOG
        printf ("adpcm: Help! Allocated audio buffer with nothing in it!\n");
#endif
        return;
      }

      /* out_block_size and j are sample counts, mem_size is a byte count */
      if (((this->out_block_size - j) * 2) > audio_buffer->mem_size)
        bytes_to_send = audio_buffer->mem_size;
      else
        bytes_to_send = (this->out_block_size - j) * 2;

      xine_fast_memcpy(audio_buffer->mem, &this->decode_buffer[j],
        bytes_to_send);
      /* byte count / 2 (bytes / sample) / channels */
      audio_buffer->num_frames = bytes_to_send / 2 / this->channels;

      audio_buffer->vpts = buf->pts;
      buf->pts = 0;  /* only first buffer gets the real pts */
      this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);

      j += bytes_to_send / 2;  /* 2 bytes per sample */
    }
  }

  /* reset buffer */
  this->size = 0;

}

static void qt_ima_adpcm_decode_block(adpcm_decoder_t *this, 
  buf_element_t *buf) {

  int initial_predictor_l = 0;
  int initial_predictor_r = 0;
  int initial_index_l = 0;
  int initial_index_r = 0;

  int i, j;
  unsigned short *output;
  unsigned int out_ptr;
  audio_buffer_t *audio_buffer;

  /* check the size */
  if ((this->size % (QT_IMA_ADPCM_BLOCK_SIZE * this->channels) != 0)) {
#ifdef LOG
    printf ("adpcm: received QT IMA block that does not line up\n");
#endif
    this->size = 0;
    return;
  }

  audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);
  output = (unsigned short *)audio_buffer->mem;
  out_ptr = 0;

  /* iterate through the blocks (and there are 2 bytes/sample) */
  for (i = 0; i < this->size; i+=
    (QT_IMA_ADPCM_BLOCK_SIZE * this->channels)) {

    /* send the buffer if it gets full */
    if ((audio_buffer->mem_size / 2) <= 
      out_ptr + (QT_IMA_ADPCM_SAMPLES_PER_BLOCK * this->channels)) {

      audio_buffer->vpts = buf->pts;
      buf->pts = 0;
      audio_buffer->num_frames = out_ptr / this->channels;
      this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);

      /* get a new audio buffer */
      audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);
      output = (unsigned short *)audio_buffer->mem;
      out_ptr = 0;
    }

    /* get the left (or mono) channel preamble bytes */
    initial_predictor_l = BE_16(&this->buf[i]);
    initial_index_l = initial_predictor_l;

    /* mask, sign-extend, and clamp the predictor portion */
    initial_predictor_l &= 0xFF80;
    SE_16BIT(initial_predictor_l);
    CLAMP_S16(initial_predictor_l);

    /* mask and clamp the index portion */
    initial_index_l &= 0x7F;
    CLAMP_0_TO_88(initial_index_l);

    /* if stereo, handle the right channel too */
    if (this->channels > 1) {
      initial_predictor_r = BE_16(&this->buf[i + QT_IMA_ADPCM_BLOCK_SIZE]);
      initial_index_r = initial_predictor_r;

      /* mask, sign-extend, and clamp the predictor portion */
      initial_predictor_r &= 0xFF80;
      SE_16BIT(initial_predictor_r);
      CLAMP_S16(initial_predictor_r);

      /* mask and clamp the index portion */
      initial_index_r &= 0x7F;
      CLAMP_0_TO_88(initial_index_r);
    }

    /* break apart all of the nibbles in the block */
    if (this->channels == 1)
      for (j = 0; j < QT_IMA_ADPCM_SAMPLES_PER_BLOCK / 2; j++) {
        output[out_ptr + j * 2 + 0] = this->buf[i + 2 + j] & 0x0F;
        output[out_ptr + j * 2 + 1] = this->buf[i + 2 + j] >> 4;
      } 
    else
      for (j = 0; j < QT_IMA_ADPCM_SAMPLES_PER_BLOCK / 2 * 2; j++) {
        output[out_ptr + j * 4 + 0] = this->buf[i + 2 + j] & 0x0F;
        output[out_ptr + j * 4 + 1] = 
          this->buf[i + 2 + QT_IMA_ADPCM_BLOCK_SIZE + j] & 0x0F;
        output[out_ptr + j * 4 + 2] = this->buf[i + 2 + j] >> 4;
        output[out_ptr + j * 4 + 3] = 
          this->buf[i + 2 + QT_IMA_ADPCM_BLOCK_SIZE + j] >> 4;
      }

    /* process the nibbles */
    decode_ima_nibbles(&output[out_ptr],
      QT_IMA_ADPCM_SAMPLES_PER_BLOCK * this->channels, 
      this->channels,
      &initial_predictor_l, &initial_index_l,
      &initial_predictor_r, &initial_index_r);

    out_ptr += QT_IMA_ADPCM_SAMPLES_PER_BLOCK * this->channels;
  }

  audio_buffer->vpts = buf->pts;
  audio_buffer->num_frames = out_ptr / this->channels;

  this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);
  this->size = 0;
}

static void ms_adpcm_decode_block(adpcm_decoder_t *this, buf_element_t *buf) {

  int i, j;
  unsigned int out_ptr = 0;
  audio_buffer_t *audio_buffer;
  int bytes_to_send;

  int current_channel = 0;
  int idelta[2];
  int sample1[2];
  int sample2[2];
  int coeff1[2];
  int coeff2[2];
  int upper_nibble = 1;
  int nibble;
  int snibble;  /* signed nibble */
  int predictor;

  /* make sure the input size checks out */
  if ((this->size % this->in_block_size) != 0) {
#ifdef LOG
    printf ("adpcm: received MS ADPCM block that does not line up\n");
#endif
    this->size = 0;
    return;
  }

  /* iterate through each block in the in buffer */
  for (i = 0; i < this->size; i += this->in_block_size) {

    /* fetch the header information, in stereo if both channels are present */
    j = i;
    upper_nibble = 1;
    current_channel = 0;
    out_ptr = 0;
#ifdef LOG
    if (this->buf[j] > 6)
      printf("MS ADPCM: coefficient (%d) out of range (should be [0..6])\n",
        this->buf[j]);
#endif
    coeff1[0] = ms_adapt_coeff1[this->buf[j]];
    coeff2[0] = ms_adapt_coeff2[this->buf[j]];
    j++;
    if (this->channels == 2) {
      if (this->buf[j] > 6) {
#ifdef LOG
        printf(
               "MS ADPCM: coefficient (%d) out of range (should be [0..6])\n",
	       this->buf[j]);
#endif
      }
      coeff1[1] = ms_adapt_coeff1[this->buf[j]];
      coeff2[1] = ms_adapt_coeff2[this->buf[j]];
      j++;
    }

    idelta[0] = LE_16(&this->buf[j]);
    j += 2;
    SE_16BIT(idelta[0]);
    if (this->channels == 2) {
      idelta[1] = LE_16(&this->buf[j]);
      j += 2;
      SE_16BIT(idelta[1]);
    }

    sample1[0] = LE_16(&this->buf[j]);
    j += 2;
    SE_16BIT(sample1[0]);
    if (this->channels == 2) {
      sample1[1] = LE_16(&this->buf[j]);
      j += 2;
      SE_16BIT(sample1[1]);
    }

    sample2[0] = LE_16(&this->buf[j]);
    j += 2;
    SE_16BIT(sample2[0]);
    if (this->channels == 2) {
      sample2[1] = LE_16(&this->buf[j]);
      j += 2;
      SE_16BIT(sample2[1]);
    }

    /* first 2 samples go directly to the output */
    if (this->channels == 1) {
      this->decode_buffer[out_ptr++] = sample2[0];
      this->decode_buffer[out_ptr++] = sample1[0];
    } else {
      this->decode_buffer[out_ptr++] = sample2[0];
      this->decode_buffer[out_ptr++] = sample2[1];
      this->decode_buffer[out_ptr++] = sample1[0];
      this->decode_buffer[out_ptr++] = sample1[1];
    }

    j = MS_ADPCM_PREAMBLE_SIZE * this->channels;
    while (j < this->in_block_size) {
      /* get the next nibble */
      if (upper_nibble)
        nibble = snibble = this->buf[i + j] >> 4;
      else
        nibble = snibble = this->buf[i + j++] & 0x0F;
      upper_nibble ^= 1;
      SE_4BIT(snibble);

      predictor = (
        ((sample1[current_channel] * coeff1[current_channel]) +
         (sample2[current_channel] * coeff2[current_channel])) / 256) +
        (snibble * idelta[current_channel]);
      CLAMP_S16(predictor);
      sample2[current_channel] = sample1[current_channel];
      sample1[current_channel] = predictor;
      this->decode_buffer[out_ptr++] = predictor;

      /* compute the next adaptive scale factor (a.k.a. the variable idelta) */
      idelta[current_channel] =
        (ms_adapt_table[nibble] * idelta[current_channel]) / 256;
      CLAMP_ABOVE_16(idelta[current_channel]);

      /* toggle the channel */
      current_channel ^= this->channels - 1;
    }

    /* dispatch the decoded audio */
    j = 0;
    while (j < out_ptr) {
      audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);
      if (audio_buffer->mem_size == 0) {
#ifdef LOG
        printf ("adpcm: Help! Allocated audio buffer with nothing in it!\n");
#endif
        return;
      }

      /* out_ptr and j are sample counts, mem_size is a byte count */
      if (((out_ptr - j) * 2) > audio_buffer->mem_size)
        bytes_to_send = audio_buffer->mem_size;
      else
        bytes_to_send = (out_ptr - j) * 2;

      xine_fast_memcpy(audio_buffer->mem, &this->decode_buffer[j],
        bytes_to_send);
      /* byte count / 2 (bytes / sample) / channels */
      audio_buffer->num_frames = bytes_to_send / 2 / this->channels;

      audio_buffer->vpts = buf->pts;
      buf->pts = 0;  /* only first buffer gets the real pts */
      this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);

      j += bytes_to_send / 2;  /* 2 bytes per sample */
    }
  }

  /* reset buffer */
  this->size = 0;
}

static void smjpeg_adpcm_decode_block(adpcm_decoder_t *this, buf_element_t *buf) {

  unsigned int block_size;
  int predictor = 0;
  int index = 0;

  int i;
  unsigned int out_ptr = 0;
  audio_buffer_t *audio_buffer;
  int bytes_to_send;

  /* fetch the size for this block and check if the decode buffer needs
   * to increase */
  block_size = buf->size - 4;  /* compensate for preamble */
  block_size *= 2;  /* 2 samples / byte */
  if (block_size > this->out_block_size) {
    this->out_block_size = block_size;
    if (this->decode_buffer) {
      free(this->decode_buffer);
    }
    this->decode_buffer = xine_xmalloc(this->out_block_size * 2);
  }

  out_ptr = 0;
  predictor = BE_16(&this->buf[0]);
  index = this->buf[2];

  /* break apart the ADPCM nibbles (iterate through each byte in block) */
  for (i = 0; i < block_size / 2; i++) {
    this->decode_buffer[out_ptr++] = this->buf[i + 4] & 0x0F;
    this->decode_buffer[out_ptr++] = this->buf[i + 4] >> 4;
  }

  /* process the nibbles */
  decode_ima_nibbles(this->decode_buffer,
    out_ptr,
    1,
    &predictor, &index,
    0, 0);

  /* dispatch the decoded audio */
  i = 0;
  while (i < out_ptr) {
    audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);
    if (audio_buffer->mem_size == 0) {
#ifdef LOG
      printf ("adpcm: Help! Allocated audio buffer with nothing in it!\n");
#endif
      return;
    }

    /* out_ptr and i are sample counts, mem_size is a byte count */
    if (((out_ptr - i) * 2) > audio_buffer->mem_size)
      bytes_to_send = audio_buffer->mem_size;
    else
      bytes_to_send = (out_ptr - i) * 2;

    xine_fast_memcpy(audio_buffer->mem, &this->decode_buffer[i],
      bytes_to_send);
    /* byte count / 2 (bytes / sample) / channels */
    audio_buffer->num_frames = bytes_to_send / 2 / this->channels;

    audio_buffer->vpts = buf->pts;
    buf->pts = 0;  /* only first buffer gets the real pts */
    this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);

    i += bytes_to_send / 2;  /* 2 bytes per sample */
  }

  /* reset buffer */
  this->size = 0;
}

static void vqa_adpcm_decode_block(adpcm_decoder_t *this, buf_element_t *buf) {

  /* VQA IMA blocks do not have a preamble with an initial index and
   * predictor; there is one master index and predictor pair per channel that
   * is initialized to 0 and maintained throughout all of the VQA IMA
   * blocks. (That is why the following variables are static.) */
  static int index_l = 0;
  static int index_r = 0;
  static int predictor_l = 0;
  static int predictor_r = 0;

  int out_ptr = 0;
  int i;
  audio_buffer_t *audio_buffer;
  int bytes_to_send;

  /* break apart the ADPCM nibbles */
  for (i = 0; i < this->size; i++) {
    if (this->channels == 1) {
      this->decode_buffer[out_ptr++] = this->buf[i] & 0x0F;
      this->decode_buffer[out_ptr++] = (this->buf[i] >> 4) & 0x0F;
    } else {
      if ((i & 0x1) == 0) {
        /* left channel */
        this->decode_buffer[out_ptr + 0] = this->buf[i] & 0x0F;
        this->decode_buffer[out_ptr + 2] = (this->buf[i] >> 4) & 0x0F;
      } else {
        /* right channel */
        this->decode_buffer[out_ptr + 1] = this->buf[i] & 0x0F;
        this->decode_buffer[out_ptr + 3] = (this->buf[i] >> 4) & 0x0F;
        out_ptr += 4;
      }
    }
  }

  /* process the nibbles */
  decode_ima_nibbles(this->decode_buffer,
    out_ptr,
    this->channels,
    &predictor_l, &index_l,
    &predictor_r, &index_r);

  /* dispatch the decoded audio */
  i = 0;
  while (i < out_ptr) {
    audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);
    if (audio_buffer->mem_size == 0) {
#ifdef LOG
      printf ("adpcm: Help! Allocated audio buffer with nothing in it!\n");
#endif
      return;
    }

    /* out_ptr and i are sample counts, mem_size is a byte count */
    if (((out_ptr - i) * 2) > audio_buffer->mem_size)
      bytes_to_send = audio_buffer->mem_size;
    else
      bytes_to_send = (out_ptr - i) * 2;

    xine_fast_memcpy(audio_buffer->mem, &this->decode_buffer[i],
      bytes_to_send);
    /* byte count / 2 (bytes / sample) / channels */
    audio_buffer->num_frames = bytes_to_send / 2 / this->channels;

    audio_buffer->vpts = buf->pts;
    buf->pts = 0;  /* only first buffer gets the real pts */
    this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);

    i += bytes_to_send / 2;  /* 2 bytes per sample */
  }

  /* reset buffer */
  this->size = 0;
}

static void ea_adpcm_decode_block(adpcm_decoder_t *this, buf_element_t *buf) {
  uint32_t samples_in_chunk;
  int32_t previous_left_sample, previous_right_sample;
  int32_t current_left_sample, current_right_sample;
  int32_t next_left_sample, next_right_sample;
  int32_t coeff1l, coeff2l, coeff1r, coeff2r;
  uint8_t shift_left, shift_right;

  int count1, count2, i = 0, j = 0;

  samples_in_chunk = ALE_32(&this->buf[i]);
  i += 4;
  current_left_sample = (int16_t)ALE_16(&this->buf[i]);
  i += 2;
  previous_left_sample = (int16_t)ALE_16(&this->buf[i]);
  i += 2;
  current_right_sample = (int16_t)ALE_16(&this->buf[i]);
  i += 2;
  previous_right_sample = (int16_t)ALE_16(&this->buf[i]);
  i += 2;

  if (samples_in_chunk * 4 > this->out_block_size) {
    this->out_block_size = samples_in_chunk * 4;
    if (this->decode_buffer) {
      free(this->decode_buffer);
    }
    this->decode_buffer = xine_xmalloc(this->out_block_size);
  }

  for (count1 = 0; count1 < samples_in_chunk/28;count1++) {
    coeff1l = ea_adpcm_table[(this->buf[i] >> 4) & 0x0F];
    coeff2l = ea_adpcm_table[((this->buf[i] >> 4) & 0x0F) + 4];
    coeff1r = ea_adpcm_table[this->buf[i] & 0x0F];
    coeff2r = ea_adpcm_table[(this->buf[i] & 0x0F) + 4];
    i++;

    shift_left = ((this->buf[i] >> 4) & 0x0F) + 8;
    shift_right = (this->buf[i] & 0x0F) + 8;
    i++;

    for (count2 = 0; count2 < 28; count2++) {
      next_left_sample = (((this->buf[i] & 0xF0) << 24) >> shift_left);
      next_right_sample = (((this->buf[i] & 0x0F) << 28) >> shift_right);
      i++;

      next_left_sample = (next_left_sample + (current_left_sample * coeff1l) + (previous_left_sample * coeff2l) + 0x80) >> 8;
      next_right_sample = (next_right_sample + (current_right_sample * coeff1r) + (previous_right_sample * coeff2r) + 0x80) >> 8;
      CLAMP_S16(next_left_sample);
      CLAMP_S16(next_right_sample);

      previous_left_sample = current_left_sample;
      current_left_sample = next_left_sample;
      previous_right_sample = current_right_sample;
      current_right_sample = next_right_sample;
      this->decode_buffer[j] = (unsigned short)current_left_sample;
      j++;
      this->decode_buffer[j] = (unsigned short)current_right_sample;
      j++;
    }
  }

  i = 0;
  while (i < j) {
    audio_buffer_t *audio_buffer;
    int bytes_to_send;

    audio_buffer = this->stream->audio_out->get_buffer(this->stream->audio_out);
    if (audio_buffer->mem_size == 0) {
#ifdef LOG
      printf ("adpcm: Help! Allocated audio buffer with nothing in it!\n");
#endif
      return;
    }

    if (((j - i) * 2) > audio_buffer->mem_size) {
      bytes_to_send = audio_buffer->mem_size;
    }
    else {
      bytes_to_send = (j - i) * 2;
    }

    xine_fast_memcpy(audio_buffer->mem, &this->decode_buffer[i], bytes_to_send);

    audio_buffer->num_frames = (bytes_to_send / 4);
    audio_buffer->vpts = buf->pts;
    buf->pts = 0;
    this->stream->audio_out->put_buffer(this->stream->audio_out, audio_buffer, this->stream);

    i += bytes_to_send / 2;
  }

  this->size = 0;
}

/* clamp a number between 0 and 48 */
#define CLAMP_0_TO_48(x)  if (x < 0) x = 0; else if (x > 48) x = 48;
/* clamp a number within a signed 12-bit range */
#define CLAMP_S12(x)  if (x < -2048) x = -2048; \
  else if (x > 2048) x = 2048;
static void dialogic_ima_decode_block(adpcm_decoder_t *this, buf_element_t *buf) {

  int i;
  unsigned int out_ptr = 0;
  audio_buffer_t *audio_buffer;
  unsigned int block_size;

  /* IMA ADPCM work variables */
  /* the predictor and index values are initialized to 0 and maintained
   * throughout the entire stream */
  static int predictor = 0;
  static int index = 16;
  int step = index;
  int diff;
  int sign;
  int delta;

  /* fetch the size for this block and check if the decode buffer needs
   * to increase */
  block_size = buf->size * 2;  /* 2 samples / byte */
  if (block_size > this->out_block_size) {
    this->out_block_size = block_size;
    if (this->decode_buffer) {
      free(this->decode_buffer);
    }
    this->decode_buffer = xine_xmalloc(this->out_block_size * 2);
  }

  /* break apart the nibbles */
  for (i = 0; i < this->size; i++) {
    this->decode_buffer[out_ptr++] = this->buf[i] >> 4;
    this->decode_buffer[out_ptr++] = this->buf[i] & 0xF;
  }

  /* decode the nibbles in place using an alternate IMA step table */
  for (i = 0; i < out_ptr; i++) {

    delta = this->decode_buffer[i];
    index += ima_adpcm_index[delta];
    CLAMP_0_TO_48(index);

    sign = delta & 8;
    delta = delta & 7;

    diff = step >> 3;
    if (delta & 4) diff += step;
    if (delta & 2) diff += step >> 1;
    if (delta & 1) diff += step >> 2;

    if (sign)
      predictor -= diff;
    else
      predictor += diff;

    CLAMP_S12(predictor);
    this->decode_buffer[i] = predictor << 4;
    step = dialogic_ima_step[index];
  }

  /* dispatch the decoded audio */
  audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);
  audio_buffer->vpts = buf->pts;
  audio_buffer->num_frames = out_ptr;
  xine_fast_memcpy(audio_buffer->mem, this->decode_buffer, out_ptr * 2);

  this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);

  /* reset buffer */
  this->size = 0;
}


static void xa_adpcm_decode_block(adpcm_decoder_t *this, buf_element_t *buf) {
  int32_t p_l, pp_l, coeff_p_l, coeff_pp_l, range_l;
  int32_t p_r, pp_r, coeff_p_r, coeff_pp_r, range_r;
  int32_t snd_group, snd_unit, snd_data, samp, i, j;
  uint8_t *inp;

  /* restore decoding history */
  p_l  = this->xa_p_l; pp_l = this->xa_pp_l;
  p_r  = this->xa_p_r; pp_r = this->xa_pp_r;

  inp = &this->buf[0];
  j = 0;

  if (this->xa_mode) {
    if (this->channels == 2) {
      /* mode A (8 bits per sample / 4 sound units) stereo
       * - sound units 0,2 are left channel, 1,3 are right channel
       * - sound data (8 bits) is shifted left to 16-bit border, then
       *   shifted right by the range parameter, therefore it's shifted
       *   (8-range) bits left.
       * - two coefficients tables (4 entries each) are merged into one
       * - coefficients are multiples of 1/256, so '>> 8' is applied
       *   after multiplication to get correct answer.
       */
      for (snd_group = 0; snd_group < 18; snd_group++, inp += 128) {
	for (snd_unit = 0; snd_unit < 4; snd_unit += 2) {
	  /* get left channel coeffs and range */
	  coeff_p_l  = xa_adpcm_table[((inp[snd_unit] >> 4) & 0x3)];
	  coeff_pp_l = xa_adpcm_table[((inp[snd_unit] >> 4) & 0x3) + 4];
	  range_l    = 8 - (inp[snd_unit] & 0xF);

	  /* get right channel coeffs and range */
	  coeff_p_r  = xa_adpcm_table[((inp[snd_unit+1] >> 4) & 0x3)];
	  coeff_pp_r = xa_adpcm_table[((inp[snd_unit+1] >> 4) & 0x3) + 4];
	  range_r    = 8 - (inp[snd_unit+1] & 0xF);

	  for (snd_data = 0; snd_data < 28; snd_data++) {
	    /* left channel */
	    samp = ((signed char *)inp)[16 + (snd_data << 2) + snd_unit];
	    samp <<= range_l;
	    samp += (coeff_p_l * p_l + coeff_pp_l * pp_l) >> 8;
	    CLAMP_S16(samp);
	    pp_l = p_l;
	    p_l = samp;
	    this->decode_buffer[j++] = (unsigned short) samp;

	    /* right channel */
	    samp = ((signed char *)inp)[16 + (snd_data << 2) + snd_unit+1];
	    samp <<= range_r;
	    samp += (coeff_p_r * p_r + coeff_pp_r * pp_r) >> 8;
	    CLAMP_S16(samp);
	    pp_r = p_r;
	    p_r = samp;
	    this->decode_buffer[j++] = (unsigned short) samp;
	  }
	}
      }
    }
    else {
      /* mode A (8 bits per sample / 4 sound units) mono
       * - other details as before
       */
      for (snd_group = 0; snd_group < 18; snd_group++, inp += 128) {
	for (snd_unit = 0; snd_unit < 4; snd_unit++) {
	  /* get coeffs and range */
	  coeff_p_l  = xa_adpcm_table[((inp[snd_unit] >> 4) & 0x3)];
	  coeff_pp_l = xa_adpcm_table[((inp[snd_unit] >> 4) & 0x3) + 4];
	  range_l    = 8 - (inp[snd_unit] & 0xF);

	  for (snd_data = 0; snd_data < 28; snd_data++) {
	    samp = ((signed char *)inp)[16 + (snd_data << 2) + snd_unit];
	    samp <<= range_l;
	    samp += (coeff_p_l * p_l + coeff_pp_l * pp_l) >> 8;
	    CLAMP_S16(samp);
	    pp_l = p_l; p_l = samp;
	    this->decode_buffer[j++] = (unsigned short) samp;
	  }
	}
      }
    }
  }
  else {
    if (this->channels == 2) {
      /* mode B/C (4 bits per sample / 8 sound units) stereo
       * - sound units 0,2,4,6 are left channel, 1,3,5,7 are right channel
       * - sound parameters 0-7 are stored as 16 bytes in the order
       *   "0123012345674567", so inp[x+4] gives sound parameter x while
       *   inp[x] doesn't.
       * - sound data (4 bits) is shifted left to 16-bit border, then
       *   shifted right by the range parameter, therefore it's shifted
       *   (12-range) bits left.
       * - other details as before
       */
      for (snd_group = 0; snd_group < 18; snd_group++, inp += 128) {
	for (snd_unit = 0; snd_unit < 8; snd_unit += 2) {
	  /* get left channel coeffs and range */
	  coeff_p_l  = xa_adpcm_table[((inp[snd_unit+4] >> 4) & 0x3)];
	  coeff_pp_l = xa_adpcm_table[((inp[snd_unit+4] >> 4) & 0x3) + 4];
	  range_l    = 12 - (inp[snd_unit+4] & 0xF);

	  /* get right channel coeffs and range */
	  coeff_p_r  = xa_adpcm_table[((inp[snd_unit+5] >> 4) & 0x3)];
	  coeff_pp_r = xa_adpcm_table[((inp[snd_unit+5] >> 4) & 0x3) + 4];
	  range_r    = 12 - (inp[snd_unit+5] & 0xF);

	  for (snd_data = 0; snd_data < 28; snd_data++) {
	    /* left channel */
	    samp = (inp[16 + (snd_data << 2) + (snd_unit >> 1)]) & 0xF;
	    SE_4BIT(samp);
	    samp <<= range_l;
	    samp += (coeff_p_l * p_l + coeff_pp_l * pp_l) >> 8;
	    CLAMP_S16(samp);
	    pp_l = p_l;
	    p_l = samp;
	    this->decode_buffer[j++] = (unsigned short) samp;

	    /* right channel */
	    samp = (inp[16 + (snd_data << 2) + (snd_unit >> 1)] >> 4) & 0xF;
	    SE_4BIT(samp);
	    samp <<= range_r;
	    samp += (coeff_p_r * p_r + coeff_pp_r * pp_r) >> 8;
	    CLAMP_S16(samp);
	    pp_r = p_r;
	    p_r = samp;
	    this->decode_buffer[j++] = (unsigned short) samp;
	  }
	}
      }
    }
    else {
      /* mode B or C (4 bits per sample / 8 sound units) mono
       * - other details as before
       */
      for (snd_group = 0; snd_group < 18; snd_group++, inp += 128) {
	for (snd_unit = 0; snd_unit < 8; snd_unit++) {
	  /* get coeffs and range */
	  coeff_p_l  = xa_adpcm_table[((inp[snd_unit+4] >> 4) & 0x3)];
	  coeff_pp_l = xa_adpcm_table[((inp[snd_unit+4] >> 4) & 0x3) + 4];
	  range_l    = 12 - (inp[snd_unit+4] & 0xF);

	  for (snd_data = 0; snd_data < 28; snd_data++) {
	    samp = inp[16 + (snd_data << 2) + (snd_unit >> 1)];
	    if (snd_unit & 1) samp >>= 4; samp &= 0xF;
	    SE_4BIT(samp);
	    samp <<= range_l;
	    samp += (coeff_p_l * p_l + coeff_pp_l * pp_l) >> 8;
	    CLAMP_S16(samp);
	    pp_l = p_l;
	    p_l = samp;
	    this->decode_buffer[j++] = (unsigned short) samp;
	  }
	}
      }
    }
  }

  /* store decoding history */
  this->xa_p_l = p_l; this->xa_pp_l = pp_l;
  this->xa_p_r = p_r; this->xa_pp_r = pp_r;

  /* despatch the decoded audio */
  i = 0;
  while (i < j) {
    audio_buffer_t *audio_buffer;
    int bytes_to_send;

    audio_buffer= this->stream->audio_out->get_buffer(this->stream->audio_out);
    if (audio_buffer->mem_size == 0) {
#ifdef LOG
      printf ("adpcm: Help! Allocated audio buffer with nothing in it!\n");
#endif
      return;
    }

    if (((j - i) * 2) > audio_buffer->mem_size) {
      bytes_to_send = audio_buffer->mem_size;
    }
    else {
      bytes_to_send = (j - i) * 2;
    }

    xine_fast_memcpy(audio_buffer->mem, &this->decode_buffer[i],
		     bytes_to_send);

    audio_buffer->num_frames = bytes_to_send / (2 * this->channels);
    audio_buffer->vpts = buf->pts;
    buf->pts = 0;
    this->stream->audio_out->put_buffer(this->stream->audio_out,
					audio_buffer, this->stream);

    i += bytes_to_send / 2;
  }

  /* reset input buffer */
  this->size = 0;
}

static void adpcm_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {
  adpcm_decoder_t *this = (adpcm_decoder_t *) this_gen;

  if (buf->decoder_flags & BUF_FLAG_HEADER) {
    xine_waveformatex *audio_header;

    this->rate = buf->decoder_info[1];
    this->channels = buf->decoder_info[3];
    this->ao_cap_mode =
      (this->channels == 2) ? AO_CAP_MODE_STEREO : AO_CAP_MODE_MONO;

    this->buf = xine_xmalloc(AUDIOBUFSIZE);
    this->bufsize = AUDIOBUFSIZE;
    this->size = 0;

    /* load the stream information */
    switch (buf->type & 0xFFFF0000) {

      case BUF_AUDIO_MSADPCM:
        this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] =
          strdup("Microsoft ADPCM");
        break;

      case BUF_AUDIO_MSIMAADPCM:
        this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] =
          strdup("Microsoft IMA ADPCM");
        break;

      case BUF_AUDIO_QTIMAADPCM:
        this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] =
          strdup("QT IMA ADPCM");
        break;

      case BUF_AUDIO_DK3ADPCM:
        this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] =
          strdup("Duck DK3 ADPCM");
        break;

      case BUF_AUDIO_DK4ADPCM:
        this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] =
          strdup("Duck DK4 ADPCM");
        break;

      case BUF_AUDIO_SMJPEG_IMA:
        this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] =
          strdup("SMJPEG IMA ADPCM");
        break;

      case BUF_AUDIO_VQA_IMA:
        this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] =
          strdup("VQA IMA ADPCM");
        break;

      case BUF_AUDIO_EA_ADPCM:
        this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] =
          strdup("EA ADPCM");
        break;

      case BUF_AUDIO_DIALOGIC_IMA:
        this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] =
          strdup("Dialogic IMA ADPCM");
        break;

      case BUF_AUDIO_XA_ADPCM:
        this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] =
          strdup("CD-ROM/XA ADPCM");
        break;

    }

    /* if the data was transported in an MS-type file (packet size will be
     * non-0 indicating an audio header), create a decode buffer */
    if (buf->size) {
      audio_header = (xine_waveformatex *)buf->content;
      this->in_block_size = audio_header->nBlockAlign;

      switch(buf->type) {
        case BUF_AUDIO_MSADPCM:
          this->out_block_size =
            (this->in_block_size - 
            ((MS_ADPCM_PREAMBLE_SIZE - 2) * this->channels)) * 2;
          break;

        case BUF_AUDIO_DK4ADPCM:
          /* A DK4 ADPCM block has 4 preamble bytes per channel and the
           * initial predictor is also the first output sample (hence
           * the +1) */
          this->out_block_size = 
            (this->in_block_size - (4 * this->channels)) * 2 + this->channels;
          break;

        case BUF_AUDIO_DK3ADPCM:
          /* A DK3 ADPCM block as 16 preamble bytes. A set of 3 nibbles,
           * or 1.5 bytes, decodes to 4 PCM samples, so 6 nibbles, or 3
           * bytes, decode to 8 PCM samples. */
          this->out_block_size = 
            (this->in_block_size - DK3_ADPCM_PREAMBLE_SIZE) * 8 / 3;
          break;

        case BUF_AUDIO_MSIMAADPCM:
          /* a block of IMA ADPCM stored in an MS-type file has 4
           * preamble bytes per channel. */
          this->out_block_size =
            (this->in_block_size - 
            (MS_IMA_ADPCM_PREAMBLE_SIZE * this->channels)) * 2;
          break;    
      
        default:
          this->out_block_size = 0;
      }

      /* allocate 2 bytes per sample */
      this->decode_buffer = xine_xmalloc(this->out_block_size * 2);
    }

    /* the decoder will not know the size of the output buffer until
     * an audio packet comes through */
    if ((buf->type == BUF_AUDIO_SMJPEG_IMA) ||
        (buf->type == BUF_AUDIO_EA_ADPCM) ||
        (buf->type == BUF_AUDIO_DIALOGIC_IMA)) {
      this->in_block_size = this->out_block_size = 0;
      this->decode_buffer = NULL;
    }

    /* make this decode buffer large enough to hold a second of decoded
     * audio */
    if (buf->type == BUF_AUDIO_VQA_IMA) {
      this->out_block_size = this->rate * this->channels;
      /* allocate 2 bytes per sample */
      this->decode_buffer = xine_xmalloc(this->out_block_size * 2);
    }

    /* XA blocks are always 2304 bytes of input data. For output, there
     * are 18 sound groups. These sound groups have 4 sound units (mode A)
     * or 8 sound units (mode B or mode C). The sound units have 28 sound
     * data samples. So, either 18*4*28=2016 or 18*8*28=4032 samples per
     * sector. 2 bytes per sample means 4032 or 8064 bytes per sector.
     */
    if ((buf->type & 0xFFFF0000) == BUF_AUDIO_XA_ADPCM) {
      /* initialise decoder state */
      this->xa_mode = buf->decoder_info[2];
      this->xa_p_l = this->xa_pp_l = this->xa_p_r = this->xa_pp_r = 0;
      /* allocate 2 bytes per sample */
      this->decode_buffer = xine_xmalloc((this->xa_mode) ? 4032 : 8064);
    }

    return;
  }

  if (!this->output_open) {
#ifdef LOG
      printf ("adpcm: opening audio output (%d Hz sampling rate, mode=%d)\n",
              this->rate, this->ao_cap_mode);
#endif
    this->output_open = this->stream->audio_out->open (this->stream->audio_out,
      this->stream, this->bits_per_sample, this->rate, this->ao_cap_mode);
  }

  /* if the audio still isn't open, bail */
  if (!this->output_open)
    return;

  /* accumulate compressed audio data */
  if( this->size + buf->size > this->bufsize ) {
    this->bufsize = this->size + 2 * buf->size;
#ifdef LOG
      printf("adpcm: increasing source buffer to %d to avoid overflow.\n",
        this->bufsize);
#endif
    this->buf = realloc( this->buf, this->bufsize );
  }

  xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);
  this->size += buf->size;

  /* time to decode a frame */
  if (buf->decoder_flags & BUF_FLAG_FRAME_END)  {

    switch(buf->type & 0xFFFF0000) {

      case BUF_AUDIO_MSADPCM:
        ms_adpcm_decode_block(this, buf);
        break;

      case BUF_AUDIO_MSIMAADPCM:
        ms_ima_adpcm_decode_block(this, buf);
        break;

      case BUF_AUDIO_QTIMAADPCM:
        qt_ima_adpcm_decode_block(this, buf);
        break;

      case BUF_AUDIO_DK3ADPCM:
        dk3_adpcm_decode_block(this, buf);
        break;

      case BUF_AUDIO_DK4ADPCM:
        dk4_adpcm_decode_block(this, buf);
        break;

      case BUF_AUDIO_SMJPEG_IMA:
        smjpeg_adpcm_decode_block(this, buf);
        break;

      case BUF_AUDIO_VQA_IMA:
        vqa_adpcm_decode_block(this, buf);
        break;

      case BUF_AUDIO_EA_ADPCM:
        ea_adpcm_decode_block(this, buf);
        break;

      case BUF_AUDIO_DIALOGIC_IMA:
        dialogic_ima_decode_block(this, buf);
        break;

      case BUF_AUDIO_XA_ADPCM:
	xa_adpcm_decode_block(this, buf);
	break;
    }
  }
}

static void adpcm_reset (audio_decoder_t *this_gen) {

  /* adpcm_decoder_t *this = (adpcm_decoder_t *) this_gen; */

}

static void adpcm_discontinuity (audio_decoder_t *this_gen) {

  /* adpcm_decoder_t *this = (adpcm_decoder_t *) this_gen; */

}

static void adpcm_dispose (audio_decoder_t *this_gen) {

  adpcm_decoder_t *this = (adpcm_decoder_t *) this_gen;

  if (this->output_open)
    this->stream->audio_out->close (this->stream->audio_out, this->stream);
  this->output_open = 0;

  if (this->decode_buffer)
    free(this->decode_buffer);
  if (this->buf)
    free(this->buf);

  free (this_gen);
}

/*
 * ADPCM decoder class code
 */

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {

  adpcm_decoder_t *this ;

  this = (adpcm_decoder_t *) malloc (sizeof (adpcm_decoder_t));

  this->audio_decoder.decode_data         = adpcm_decode_data;
  this->audio_decoder.reset               = adpcm_reset;
  this->audio_decoder.discontinuity       = adpcm_discontinuity;
  this->audio_decoder.dispose             = adpcm_dispose;

  this->output_open = 0;
  this->rate = 0;
  this->bits_per_sample = 16;  /* these codecs always output 16-bit PCM */
  this->channels = 0;
  this->ao_cap_mode = 0;
  this->decode_buffer = NULL;
  this->stream = stream;

  return &this->audio_decoder;
}

static char *get_identifier (audio_decoder_class_t *this) {
  return "ADPCM";
}

static char *get_description (audio_decoder_class_t *this) {
  return "Multiple ADPCM audio format decoder plugin";
}

static void dispose_class (audio_decoder_class_t *this) {
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  adpcm_class_t *this ;

  this = (adpcm_class_t *) malloc (sizeof (adpcm_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}

static uint32_t audio_types[] = { 
  BUF_AUDIO_MSADPCM, BUF_AUDIO_MSIMAADPCM,
  BUF_AUDIO_QTIMAADPCM, BUF_AUDIO_DK3ADPCM,
  BUF_AUDIO_DK4ADPCM, BUF_AUDIO_SMJPEG_IMA,
  BUF_AUDIO_VQA_IMA, BUF_AUDIO_EA_ADPCM, 
  BUF_AUDIO_DIALOGIC_IMA, BUF_AUDIO_XA_ADPCM,
  0
 };

static decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  9                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_AUDIO_DECODER, 13, "adpcm", XINE_VERSION_CODE, &dec_info_audio, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
