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
 * $Id: audio_decoder.h,v 1.9 2003/05/20 13:50:56 mroi Exp $
 *
 * xine audio decoder plugin interface
 *
 */

#ifndef HAVE_AUDIO_DECODER_H
#define HAVE_AUDIO_DECODER_H

#include <inttypes.h>

#ifdef XINE_COMPILE
#  include "buffer.h"
#else
#  include <xine/buffer.h>
#endif

#define AUDIO_DECODER_IFACE_VERSION 13

/*
 * generic xine audio decoder plugin interface
 */

typedef struct audio_decoder_class_s audio_decoder_class_t;
typedef struct audio_decoder_s audio_decoder_t;

struct audio_decoder_class_s {

  /*
   * open a new instance of this plugin class
   */
  audio_decoder_t* (*open_plugin) (audio_decoder_class_t *this, xine_stream_t *stream);
  
  /*
   * return short, human readable identifier for this plugin class
   */
  char* (*get_identifier) (audio_decoder_class_t *this);

  /*
   * return human readable (verbose = 1 line) description for 
   * this plugin class
   */
  char* (*get_description) (audio_decoder_class_t *this);

  /*
   * free all class-related resources
   */

  void (*dispose) (audio_decoder_class_t *this);
};


struct audio_decoder_s {

  /*
   * decode data from buf and feed decoded samples to 
   * audio output 
   */
  void (*decode_data) (audio_decoder_t *this, buf_element_t *buf);

  /*
   * reset decoder after engine flush (prepare for new
   * audio data not related to recently decoded data)
   */
  void (*reset) (audio_decoder_t *this);
  
  /*
   * inform decoder that a time reference discontinuity has happened.
   * that is, it must forget any currently held pts value
   */
  void (*discontinuity) (audio_decoder_t *this);  
  
  /*
   * close down, free all resources
   */
  void (*dispose) (audio_decoder_t *this);

  void *node; /* used by plugin loader */

};

#endif
