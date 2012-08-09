/*
 * Copyright (C) 2000-2002 the xine project
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
 * $Id: xine_decoder.c,v 1.58 2003/10/06 17:03:25 jcdutton Exp $
 *
 * stuff needed to turn liba52 into a xine decoder plugin
 */

#ifndef __sun
/* required for swab() */
#define _XOPEN_SOURCE 500
#endif


#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include "xine_internal.h"
#include "audio_out.h"
#include "a52.h"
#include "a52_internal.h"
#include "buffer.h"
#include "xineutils.h"

/*
#define LOG
#define LOG_PTS
*/

#undef DEBUG_A52
#ifdef DEBUG_A52
int a52file;
#endif

typedef struct {
  audio_decoder_class_t   decoder_class;
  config_values_t *config;
  
  float            a52_level;
  int              disable_dynrng;
  int              enable_surround_downmix;
  
} a52dec_class_t;

typedef struct a52dec_decoder_s {
  audio_decoder_t  audio_decoder;

  a52dec_class_t  *class;
  xine_stream_t   *stream;
  int64_t          pts;
  int64_t          pts_list[5];
  int32_t          pts_list_position;

  uint8_t          frame_buffer[3840];
  uint8_t         *frame_ptr;
  int              sync_state;
  int              frame_length, frame_todo;
  uint16_t         syncword;

  a52_state_t     *a52_state;
  int              a52_flags;
  int              a52_bit_rate;
  int              a52_sample_rate;
  int              have_lfe;

  int              a52_flags_map[11];
  int              ao_flags_map[11];

  int              audio_caps;
  int              bypass_mode;
  int              output_sampling_rate;
  int              output_open;
  int              output_mode;

} a52dec_decoder_t;

struct frmsize_s
{
  uint16_t bit_rate;
  uint16_t frm_size[3];
};

static const struct frmsize_s frmsizecod_tbl[64] =
{
  { 32  ,{64   ,69   ,96   } },
  { 32  ,{64   ,70   ,96   } },
  { 40  ,{80   ,87   ,120  } },
  { 40  ,{80   ,88   ,120  } },
  { 48  ,{96   ,104  ,144  } },
  { 48  ,{96   ,105  ,144  } },
  { 56  ,{112  ,121  ,168  } },
  { 56  ,{112  ,122  ,168  } },
  { 64  ,{128  ,139  ,192  } },
  { 64  ,{128  ,140  ,192  } },
  { 80  ,{160  ,174  ,240  } },
  { 80  ,{160  ,175  ,240  } },
  { 96  ,{192  ,208  ,288  } },
  { 96  ,{192  ,209  ,288  } },
  { 112 ,{224  ,243  ,336  } },
  { 112 ,{224  ,244  ,336  } },
  { 128 ,{256  ,278  ,384  } },
  { 128 ,{256  ,279  ,384  } },
  { 160 ,{320  ,348  ,480  } },
  { 160 ,{320  ,349  ,480  } },
  { 192 ,{384  ,417  ,576  } },
  { 192 ,{384  ,418  ,576  } },
  { 224 ,{448  ,487  ,672  } },
  { 224 ,{448  ,488  ,672  } },
  { 256 ,{512  ,557  ,768  } },
  { 256 ,{512  ,558  ,768  } },
  { 320 ,{640  ,696  ,960  } },
  { 320 ,{640  ,697  ,960  } },
  { 384 ,{768  ,835  ,1152 } },
  { 384 ,{768  ,836  ,1152 } },
  { 448 ,{896  ,975  ,1344 } },
  { 448 ,{896  ,976  ,1344 } },
  { 512 ,{1024 ,1114 ,1536 } },
  { 512 ,{1024 ,1115 ,1536 } },
  { 576 ,{1152 ,1253 ,1728 } },
  { 576 ,{1152 ,1254 ,1728 } },
  { 640 ,{1280 ,1393 ,1920 } },
  { 640 ,{1280 ,1394 ,1920 } }
};

static void a52dec_reset (audio_decoder_t *this_gen) {

  a52dec_decoder_t *this = (a52dec_decoder_t *) this_gen;

  this->syncword      = 0;
  this->sync_state    = 0;
  this->pts           = 0;
  this->pts_list_position = 0;
}

static void a52dec_discontinuity (audio_decoder_t *this_gen) {

  a52dec_decoder_t *this = (a52dec_decoder_t *) this_gen;

  this->pts           = 0;
}

static inline int16_t blah (int32_t i) {

  if (i > 0x43c07fff)
    return 32767;
  else if (i < 0x43bf8000)
    return -32768;
  else
    return i - 0x43c00000;
}

static inline void float_to_int (float * _f, int16_t * s16, int num_channels) {
  int i;
  int32_t * f = (int32_t *) _f;       /* XXX assumes IEEE float format */

  for (i = 0; i < 256; i++) {
    s16[num_channels*i] = blah (f[i]);
  }
}

static inline void mute_channel (int16_t * s16, int num_channels) {
  int i;

  for (i = 0; i < 256; i++) {
    s16[num_channels*i] = 0;
  }
}

static void a52dec_decode_frame (a52dec_decoder_t *this, int64_t pts, int preview_mode) {

  int output_mode = AO_CAP_MODE_STEREO;

  /*
   * do we want to decode this frame in software?
   */
#ifdef LOG_PTS
  printf("a52dec:decode_frame:pts=%lld\n",pts);
#endif 
  if (!this->bypass_mode) {

    int              a52_output_flags, i,n;
    sample_t         level = this->class->a52_level;
    audio_buffer_t  *buf;
    int16_t         *int_samples;
    sample_t        *samples = a52_samples(this->a52_state);

    /*
     * oki, decode this frame in software
     */

    /* determine output mode */

    a52_output_flags = this->a52_flags_map[this->a52_flags & A52_CHANNEL_MASK];

    if (a52_frame (this->a52_state,
		   this->frame_buffer,
		   &a52_output_flags,
		   &level, 384)) {
      printf ("liba52: a52_frame error\n");
      return;
    }

    if (this->class->disable_dynrng)
      a52_dynrng (this->a52_state, NULL, NULL);

    this->have_lfe = a52_output_flags & A52_LFE;
    if (this->have_lfe)
      if (this->audio_caps & AO_CAP_MODE_5_1CHANNEL) {
        output_mode = AO_CAP_MODE_5_1CHANNEL;
      } else if (this->audio_caps & AO_CAP_MODE_4_1CHANNEL) {
        output_mode = AO_CAP_MODE_4_1CHANNEL;
      } else {
        printf("liba52: WHAT DO I DO!!!\n");
        output_mode = this->ao_flags_map[a52_output_flags];
      }
    else
      output_mode = this->ao_flags_map[a52_output_flags];
    /*
     * (re-)open output device
     */

    if (!this->output_open
	|| (this->a52_sample_rate != this->output_sampling_rate)
	|| (output_mode != this->output_mode)) {

      if (this->output_open)
	this->stream->audio_out->close (this->stream->audio_out, this->stream);


      this->output_open = this->stream->audio_out->open (this->stream->audio_out, 
							 this->stream, 16,
							 this->a52_sample_rate,
							 output_mode) ;
      this->output_sampling_rate = this->a52_sample_rate;
      this->output_mode = output_mode;
    }


    if (!this->output_open || preview_mode)
      return;


    /*
     * decode a52 and convert/interleave samples
     */

    buf = this->stream->audio_out->get_buffer (this->stream->audio_out);
    int_samples = buf->mem;
    buf->num_frames = 256*6;

    for (i = 0; i < 6; i++) {
      if (a52_block (this->a52_state)) {
	printf ("liba52: a52_block error on audio channel %d\n", i);
#if 0	
	for(n=0;n<2000;n++) {
	  printf("%02x ",this->frame_buffer[n]);
	  if ((n % 32) == 0) printf("\n");
	}
	printf("\n");
#endif	
	buf->num_frames = 0;
	break;
      }

      switch (output_mode) {
      case AO_CAP_MODE_MONO:
	float_to_int (&samples[0], int_samples+(i*256), 1);
	break;
      case AO_CAP_MODE_STEREO:
	float_to_int (&samples[0*256], int_samples+(i*256*2), 2);
	float_to_int (&samples[1*256], int_samples+(i*256*2)+1, 2);
	break;
      case AO_CAP_MODE_4CHANNEL:
	float_to_int (&samples[0*256], int_samples+(i*256*4),   4); /*  L */
	float_to_int (&samples[1*256], int_samples+(i*256*4)+1, 4); /*  R */
	float_to_int (&samples[2*256], int_samples+(i*256*4)+2, 4); /* RL */
	float_to_int (&samples[3*256], int_samples+(i*256*4)+3, 4); /* RR */
	break;
      case AO_CAP_MODE_4_1CHANNEL:
	float_to_int (&samples[0*256], int_samples+(i*256*6)+5, 6); /* LFE */
	float_to_int (&samples[1*256], int_samples+(i*256*6)+0, 6); /* L   */
        float_to_int (&samples[2*256], int_samples+(i*256*6)+1, 6); /* R   */
	float_to_int (&samples[3*256], int_samples+(i*256*6)+2, 6); /* RL */
	float_to_int (&samples[4*256], int_samples+(i*256*6)+3, 6); /* RR */
	mute_channel ( int_samples+(i*256*6)+4, 6); /* C */
	break;
      case AO_CAP_MODE_5CHANNEL:
	float_to_int (&samples[0*256], int_samples+(i*256*6)+0, 6); /*  L */
        float_to_int (&samples[1*256], int_samples+(i*256*6)+4, 6); /*  C */
	float_to_int (&samples[2*256], int_samples+(i*256*6)+1, 6); /*  R */
	float_to_int (&samples[3*256], int_samples+(i*256*6)+2, 6); /* RL */
	float_to_int (&samples[4*256], int_samples+(i*256*6)+3, 6); /* RR */
	mute_channel ( int_samples+(i*256*6)+5, 6); /* LFE */
	break;
      case AO_CAP_MODE_5_1CHANNEL:
	float_to_int (&samples[0*256], int_samples+(i*256*6)+5, 6); /* lfe */
	float_to_int (&samples[1*256], int_samples+(i*256*6)+0, 6); /*   L */
	float_to_int (&samples[2*256], int_samples+(i*256*6)+4, 6); /*   C */
	float_to_int (&samples[3*256], int_samples+(i*256*6)+1, 6); /*   R */
	float_to_int (&samples[4*256], int_samples+(i*256*6)+2, 6); /*  RL */
	float_to_int (&samples[5*256], int_samples+(i*256*6)+3, 6); /*  RR */
	break;
      default:
	printf ("liba52: help - unsupported mode %08x\n", output_mode);
      }
    }
#ifdef LOG
    printf ("liba52: %d frames output\n", buf->num_frames);
#endif

    /*  output decoded samples */

    buf->vpts       = pts;

    this->stream->audio_out->put_buffer (this->stream->audio_out, buf, this->stream);

  } else {

    /*
     * loop through a52 data
     */

    if (!this->output_open) {

      int sample_rate, bit_rate, flags;

      a52_syncinfo (this->frame_buffer, &flags, &sample_rate, &bit_rate);

      this->output_open = this->stream->audio_out->open (this->stream->audio_out,
						 this->stream, 16,
						 sample_rate,
						 AO_CAP_MODE_A52) ;
      this->output_mode = AO_CAP_MODE_A52;
    }

    if (this->output_open && !preview_mode) {
      /* SPDIF Passthrough
       * Build SPDIF Header and encaps the A52 audio data in it.
       */
      uint32_t syncword, crc1, fscod,frmsizecod,bsid,bsmod,frame_size;
      uint8_t *data_out,*data_in;
      audio_buffer_t *buf = this->stream->audio_out->get_buffer (this->stream->audio_out);
      data_in=(uint8_t *) this->frame_buffer;
      data_out=(uint8_t *) buf->mem;
      syncword = data_in[0] | (data_in[1] << 8);
      crc1 = data_in[2] | (data_in[3] << 8);
      fscod = (data_in[4] >> 6) & 0x3;
      frmsizecod = data_in[4] & 0x3f;
      bsid = (data_in[5] >> 3) & 0x1f;
      bsmod = data_in[5] & 0x7;		/* bsmod, stream = 0 */
      frame_size = frmsizecod_tbl[frmsizecod].frm_size[fscod] ;

      data_out[0] = 0x72; data_out[1] = 0xf8;	/* spdif syncword    */
      data_out[2] = 0x1f; data_out[3] = 0x4e;	/* ..............    */
      data_out[4] = 0x01;			/* AC3 data          */
      data_out[5] = bsmod;			/* bsmod, stream = 0 */
      data_out[6] = (frame_size << 4) & 0xff;   /* frame_size * 16   */
      data_out[7] = ((frame_size ) >> 4) & 0xff;
      swab(data_in, &data_out[8], frame_size * 2 );

      buf->num_frames = 1536;
      buf->vpts       = pts;

      this->stream->audio_out->put_buffer (this->stream->audio_out, buf, this->stream);

    }
  }
}

static void a52dec_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  a52dec_decoder_t *this = (a52dec_decoder_t *) this_gen;
  uint8_t          *current = buf->content;
  uint8_t          *sync_start=current + 1;
  uint8_t          *end = buf->content + buf->size;
  uint8_t           byte;
  int32_t	n;
  uint16_t          crc16;
  uint16_t          crc16_result;

#ifdef LOG
  printf ("liba52: decode data %d bytes of type %08x, pts=%lld\n",
	  buf->size, buf->type, buf->pts);
  printf ("liba52: decode data decoder_info=%d, %d\n",buf->decoder_info[1],buf->decoder_info[2]);
#endif

  if (buf->decoder_flags & BUF_FLAG_HEADER)
    return;

  /* swap byte pairs if this is RealAudio DNET data */
  if (buf->type == BUF_AUDIO_DNET) {

#ifdef LOG
    printf ("liba52: byte-swapping dnet\n");
#endif

    while (current != end) {
      byte = *current++;
      *(current - 1) = *current;
      *current++ = byte;
    }

    /* reset */
    current = buf->content;
    end = buf->content + buf->size;
  }

  if (buf->pts) {
    int32_t info;
    info = buf->decoder_info[1];
    this->pts = buf->pts;
    this->pts_list[this->pts_list_position]=buf->pts;
    this->pts_list_position++;
    if( this->pts_list_position > 3 )
      this->pts_list_position = 3;
    if (info == 2) {
      this->pts_list[this->pts_list_position]=0;
      this->pts_list_position++;
      if( this->pts_list_position > 3 )
        this->pts_list_position = 3;
    }
  }
#if 0
  for(n=0;n < buf->size;n++) {
    if ((n % 32) == 0) printf("\n");
    printf("%x ", current[n]);
  }
  printf("\n");
#endif

#ifdef LOG
    printf ("liba52: processing...state %d\n", this->sync_state);
#endif
  while (current < end) {
    switch (this->sync_state) {
    case 0:  /* Looking for sync header */
	  this->syncword = (this->syncword << 8) | *current++;
	  if (this->syncword == 0x0b77) {

	    this->frame_buffer[0] = 0x0b;
	    this->frame_buffer[1] = 0x77;

	    this->sync_state = 1;
	    this->frame_ptr = this->frame_buffer+2;
	  }
          break;

    case 1:  /* Looking for enough bytes for sync_info. */
          sync_start = current - 1;
	  *this->frame_ptr++ = *current++;
          if ((this->frame_ptr - this->frame_buffer) > 16) {
	    int a52_flags_old       = this->a52_flags;
	    int a52_sample_rate_old = this->a52_sample_rate;
	    int a52_bit_rate_old    = this->a52_bit_rate;

	    this->frame_length = a52_syncinfo (this->frame_buffer,
					       &this->a52_flags,
					       &this->a52_sample_rate,
					       &this->a52_bit_rate);

            if (this->frame_length < 80) { /* Invalid a52 frame_length */
	      this->syncword = 0;
	      current = sync_start;
	      this->sync_state = 0;
	      break;
	    }
#ifdef LOG
            printf("Frame length = %d\n",this->frame_length);
#endif
	    this->frame_todo = this->frame_length - 17;
	    this->sync_state = 2;
	    if (!this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] ||
	        a52_flags_old       != this->a52_flags ||
                a52_sample_rate_old != this->a52_sample_rate ||
		a52_bit_rate_old    != this->a52_bit_rate) {

              if (((this->a52_flags & A52_CHANNEL_MASK) == A52_3F2R) && (this->a52_flags & A52_LFE))
                this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] = strdup ("A/52 5.1");
              else if ((((this->a52_flags & A52_CHANNEL_MASK) == A52_2F2R) && (this->a52_flags & A52_LFE)) ||
                       (((this->a52_flags & A52_CHANNEL_MASK) == A52_3F1R) && (this->a52_flags & A52_LFE)))
                this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] = strdup ("A/52 4.1");
              else if ((this->a52_flags & A52_CHANNEL_MASK) == A52_3F2R) 
                this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] = strdup ("A/52 5.0");
              else if (((this->a52_flags & A52_CHANNEL_MASK) == A52_2F2R) ||
                       ((this->a52_flags & A52_CHANNEL_MASK) == A52_3F1R))
                this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] = strdup ("A/52 4.0");
              else if (((this->a52_flags & A52_CHANNEL_MASK) == A52_2F1R) ||
                       ((this->a52_flags & A52_CHANNEL_MASK) == A52_3F))
                this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] = strdup ("A/52 3.0");
              else if ((this->a52_flags & A52_CHANNEL_MASK) == A52_STEREO)
                this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] = strdup ("A/52 2.0 (stereo)");
              else if ((this->a52_flags & A52_CHANNEL_MASK) == A52_DOLBY)
                this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] = strdup ("A/52 2.0 (dolby)");
              else if ((this->a52_flags & A52_CHANNEL_MASK) == A52_MONO)
                this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] = strdup ("A/52 1.0");
              else
                this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] = strdup ("A/52");

              this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITRATE] = this->a52_bit_rate;
              this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] = this->a52_sample_rate;
            }
          }
          break;
            
    case 2:  /* Filling frame_buffer with sync_info bytes */
	  *this->frame_ptr++ = *current++;
	  this->frame_todo--;
	  if (this->frame_todo < 1) {
	    this->sync_state = 3;
          } else break;
      
    case 3:  /* Ready for decode */
	  crc16 = (uint16_t) ((this->frame_buffer[2] << 8) |  this->frame_buffer[3]) ;
	  crc16_result = crc16_block(&this->frame_buffer[2], this->frame_length - 2) ; /* frame_length */
	  if (crc16_result != 0) { /* CRC16 failed */
	    printf("liba52:a52 frame failed crc16 checksum.\n");
	    current = sync_start;
            this->pts = 0;
	    this->syncword = 0;
	    this->sync_state = 0;
	    break;
	  }
          a52dec_decode_frame (this, this->pts_list[0], buf->decoder_flags & BUF_FLAG_PREVIEW);
          for(n=0;n<4;n++) {
            this->pts_list[n] = this->pts_list[n+1];
          }
          this->pts_list_position--;
          if( this->pts_list_position < 0 )
            this->pts_list_position = 0;
#if 0
          printf("liba52: pts_list = %lld, %lld, %lld\n",
            this->pts_list[0],
            this->pts_list[1],
            this->pts_list[2]);
#endif
    case 4:  /* Clear up ready for next frame */
          this->pts = 0;
	  this->syncword = 0;
	  this->sync_state = 0;
          break;
    default: /* No come here */ 
          break;
    }
  }

#ifdef DEBUG_A52
      write (a52file, this->frame_buffer, this->frame_length);
#endif
}

static void a52dec_dispose (audio_decoder_t *this_gen) {

  a52dec_decoder_t *this = (a52dec_decoder_t *) this_gen;

  if (this->output_open)
    this->stream->audio_out->close (this->stream->audio_out, this->stream);

  this->output_open = 0;

  a52_free(this->a52_state);
  this->a52_state = NULL;

#ifdef DEBUG_A52
  close (a52file);
#endif
  free (this_gen);
}

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {

  a52dec_decoder_t *this ;
#ifdef LOG
  printf ("liba52:open_plugin called\n");
#endif

  this = (a52dec_decoder_t *) malloc (sizeof (a52dec_decoder_t));
  memset(this, 0, sizeof (a52dec_decoder_t));

  this->audio_decoder.decode_data         = a52dec_decode_data;
  this->audio_decoder.reset               = a52dec_reset;
  this->audio_decoder.discontinuity       = a52dec_discontinuity;
  this->audio_decoder.dispose             = a52dec_dispose;
  this->stream                            = stream;
  this->class                             = (a52dec_class_t *) class_gen;

  /* int i; */

  this->audio_caps    = stream->audio_out->get_capabilities(stream->audio_out);
  this->syncword      = 0;
  this->sync_state    = 0;
  this->output_open   = 0;
  this->pts           = 0;
  this->pts_list[0] = this->pts_list[1] = this->pts_list[2] = 0;
  this->pts_list_position = 0;

  if( !this->a52_state )
    this->a52_state = a52_init (xine_mm_accel());

  /*
   * find out if this driver supports a52 output
   * or, if not, how many channels we've got
   */

  if (this->audio_caps & AO_CAP_MODE_A52)
    this->bypass_mode = 1;
  else {
    this->bypass_mode = 0;

    this->a52_flags_map[A52_MONO]   = A52_MONO;
    this->a52_flags_map[A52_STEREO] = ((this->class->enable_surround_downmix ? A52_DOLBY : A52_STEREO));
    this->a52_flags_map[A52_3F]     = ((this->class->enable_surround_downmix ? A52_DOLBY : A52_STEREO));
    this->a52_flags_map[A52_2F1R]   = ((this->class->enable_surround_downmix ? A52_DOLBY : A52_STEREO));
    this->a52_flags_map[A52_3F1R]   = ((this->class->enable_surround_downmix ? A52_DOLBY : A52_STEREO));
    this->a52_flags_map[A52_2F2R]   = ((this->class->enable_surround_downmix ? A52_DOLBY : A52_STEREO));
    this->a52_flags_map[A52_3F2R]   = ((this->class->enable_surround_downmix ? A52_DOLBY : A52_STEREO));
    this->a52_flags_map[A52_DOLBY]  = ((this->class->enable_surround_downmix ? A52_DOLBY : A52_STEREO));

    this->ao_flags_map[A52_MONO]    = AO_CAP_MODE_MONO;
    this->ao_flags_map[A52_STEREO]  = AO_CAP_MODE_STEREO;
    this->ao_flags_map[A52_3F]      = AO_CAP_MODE_STEREO;
    this->ao_flags_map[A52_2F1R]    = AO_CAP_MODE_STEREO;
    this->ao_flags_map[A52_3F1R]    = AO_CAP_MODE_STEREO;
    this->ao_flags_map[A52_2F2R]    = AO_CAP_MODE_STEREO;
    this->ao_flags_map[A52_3F2R]    = AO_CAP_MODE_STEREO;
    this->ao_flags_map[A52_DOLBY]   = AO_CAP_MODE_STEREO;

    /* find best mode */
    if (this->audio_caps & AO_CAP_MODE_5_1CHANNEL) {

      this->a52_flags_map[A52_2F2R]   = A52_2F2R;
      this->a52_flags_map[A52_3F2R]   = A52_3F2R | A52_LFE;
      this->ao_flags_map[A52_2F2R]    = AO_CAP_MODE_4CHANNEL;
      this->ao_flags_map[A52_3F2R]    = AO_CAP_MODE_5CHANNEL;

    } else if (this->audio_caps & AO_CAP_MODE_5CHANNEL) {

      this->a52_flags_map[A52_2F2R]   = A52_2F2R;
      this->a52_flags_map[A52_3F2R]   = A52_3F2R;
      this->ao_flags_map[A52_2F2R]    = AO_CAP_MODE_4CHANNEL;
      this->ao_flags_map[A52_3F2R]    = AO_CAP_MODE_5CHANNEL;

    } else if (this->audio_caps & AO_CAP_MODE_4_1CHANNEL) {

      this->a52_flags_map[A52_2F2R]   = A52_2F2R;
      this->a52_flags_map[A52_3F2R]   = A52_2F2R | A52_LFE;
      this->ao_flags_map[A52_2F2R]    = AO_CAP_MODE_4CHANNEL;
      this->ao_flags_map[A52_3F2R]    = AO_CAP_MODE_4CHANNEL;

    } else if (this->audio_caps & AO_CAP_MODE_4CHANNEL) {

      this->a52_flags_map[A52_2F2R]   = A52_2F2R;
      this->a52_flags_map[A52_3F2R]   = A52_2F2R;

      this->ao_flags_map[A52_2F2R]    = AO_CAP_MODE_4CHANNEL;
      this->ao_flags_map[A52_3F2R]    = AO_CAP_MODE_4CHANNEL;

      /* else if (this->audio_caps & AO_CAP_MODE_STEREO)
	 defaults are ok */
    } else if (!(this->audio_caps & AO_CAP_MODE_STEREO)) {
      printf ("HELP! a mono-only audio driver?!\n");

      this->a52_flags_map[A52_MONO]   = A52_MONO;
      this->a52_flags_map[A52_STEREO] = A52_MONO;
      this->a52_flags_map[A52_3F]     = A52_MONO;
      this->a52_flags_map[A52_2F1R]   = A52_MONO;
      this->a52_flags_map[A52_3F1R]   = A52_MONO;
      this->a52_flags_map[A52_2F2R]   = A52_MONO;
      this->a52_flags_map[A52_3F2R]   = A52_MONO;
      this->a52_flags_map[A52_DOLBY]  = A52_MONO;

      this->ao_flags_map[A52_MONO]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[A52_STEREO]  = AO_CAP_MODE_MONO;
      this->ao_flags_map[A52_3F]      = AO_CAP_MODE_MONO;
      this->ao_flags_map[A52_2F1R]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[A52_3F1R]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[A52_2F2R]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[A52_3F2R]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[A52_DOLBY]   = AO_CAP_MODE_MONO;
    }
  }

  /*
    for (i = 0; i<8; i++)
    this->a52_flags_map[i] |= A52_ADJUST_LEVEL;
  */
#ifdef DEBUG_A52
  a52file = open ("test.a52", O_CREAT | O_WRONLY | O_TRUNC, 0644);
#endif
  return &this->audio_decoder;
}

static char *get_identifier (audio_decoder_class_t *this) {
#ifdef LOG
  printf ("liba52:get_identifier called\n");
#endif
  return "a/52dec";
}

static char *get_description (audio_decoder_class_t *this) {
#ifdef LOG
  printf ("liba52:get_description called\n");
#endif
  return "liba52 based a52 audio decoder plugin";
}

static void dispose_class (audio_decoder_class_t *this) {
#ifdef LOG
  printf ("liba52:dispose_class called\n");
#endif
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  a52dec_class_t *this;
  config_values_t *cfg;

  this = (a52dec_class_t *) malloc (sizeof (a52dec_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  cfg = this->config = xine->config;

  this->a52_level = (float) cfg->register_range (cfg, "codec.a52_level", 100,
						 0, 200,
						 _("a/52 volume control"),
						 NULL, 0, NULL, NULL) / 100.0;
  this->disable_dynrng = !cfg->register_bool (cfg, "codec.a52_dynrng", 0,
					      _("enable a/52 dynamic range compensation"),
					      NULL, 0, NULL, NULL);
  this->enable_surround_downmix = cfg->register_bool (cfg, "codec.a52_surround_downmix", 0,
						      _("enable audio downmixing to 2.0 surround stereo"),
						      NULL, 0, NULL, NULL);
  
  
#ifdef LOG
  printf ("liba52:init_plugin called\n");
#endif
  return this;
}


static uint32_t audio_types[] = {
  BUF_AUDIO_A52,
  BUF_AUDIO_DNET,
  0
 };

static decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  2                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_AUDIO_DECODER | PLUGIN_MUST_PRELOAD, 13, "a/52", XINE_VERSION_CODE, &dec_info_audio, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
