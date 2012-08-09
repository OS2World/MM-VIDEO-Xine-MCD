/*
 * Copyright (C) 2000-2003 the xine project
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
 * $Id: xine_decoder.c,v 1.50 2003/10/23 20:12:33 mroi Exp $
 *
 * thin layer to use real binary-only codecs in xine
 *
 * code inspired by work from Florian Schneider for the MPlayer Project 
 */


#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>

#include "bswap.h"
#include "xine_internal.h"
#include "video_out.h"
#include "buffer.h"
#include "xineutils.h"

/*
#define LOG
*/


typedef struct {
  video_decoder_class_t   decoder_class;

  /* empty so far */
} real_class_t;

#define BUF_SIZE       65536
#define CHUNK_TAB_SIZE 128

typedef struct realdec_decoder_s {
  video_decoder_t  video_decoder;

  real_class_t    *cls;

  xine_stream_t   *stream;

  void            *rv_handle;

  unsigned long (*rvyuv_custom_message)(unsigned long*,void*);
  unsigned long (*rvyuv_free)(void*);
  unsigned long (*rvyuv_hive_message)(unsigned long,unsigned long);
  unsigned long (*rvyuv_init)(void*, void*); /* initdata,context */
  unsigned long (*rvyuv_transform)(char*, char*,unsigned long*,unsigned long*,void*);

  void            *context;

  uint32_t         width, height;
  double           ratio;

  uint8_t         *chunk_buffer;
  int              chunk_buffer_size;
  int              chunk_buffer_max;

  int              num_chunks;
  uint32_t        *chunk_tab;
  int              chunk_tab_max;

  uint8_t          chunk_id;
  
  /* keep track of timestamps, estimate framerate */
  uint64_t         pts;
  int              num_frames;
  uint64_t         last_pts;
  uint64_t         duration;

  uint8_t         *frame_buffer;
  int              frame_size;
  int              decoder_ok;

} realdec_decoder_t;

/* we need exact positions */
typedef struct {
        short unk1;
        short w;
        short h;
        short unk3;
        int unk2;
        int subformat;
        int unk5;
        int format;
} rv_init_t;


void *__builtin_vec_new(unsigned long size);
void __builtin_vec_delete(void *mem);
void __pure_virtual(void);

/*
 * real codec loader
 */

static int load_syms_linux (realdec_decoder_t *this, char *codec_name) {

  cfg_entry_t* entry = this->stream->xine->config->lookup_entry(
			 this->stream->xine->config, "codec.real_codecs_path");
  char path[1024];

  sprintf (path, "%s/%s", entry->str_value, codec_name);

#ifdef LOG
  printf ("libreal: opening shared obj '%s'\n", path);
#endif
  this->rv_handle = dlopen (path, RTLD_LAZY);

  if (!this->rv_handle) {
    printf ("libreal: error: %s\n", dlerror());
    xine_message(this->stream, XINE_MSG_LIBRARY_LOAD_ERROR,
                 codec_name, NULL);
    return 0;
  }
  
  this->rvyuv_custom_message = dlsym (this->rv_handle, "RV20toYUV420CustomMessage");
  this->rvyuv_free           = dlsym (this->rv_handle, "RV20toYUV420Free");
  this->rvyuv_hive_message   = dlsym (this->rv_handle, "RV20toYUV420HiveMessage");
  this->rvyuv_init           = dlsym (this->rv_handle, "RV20toYUV420Init");
  this->rvyuv_transform      = dlsym (this->rv_handle, "RV20toYUV420Transform");
  
  if (this->rvyuv_custom_message &&
      this->rvyuv_free &&
      this->rvyuv_hive_message &&
      this->rvyuv_init &&
      this->rvyuv_transform) 
    return 1;

  printf ("libreal: Error resolving symbols! (version incompatibility?)\n");
  return 0;
}

static int init_codec (realdec_decoder_t *this, buf_element_t *buf) {

  /* unsigned int* extrahdr = (unsigned int*) (buf->content+28); */
  int           result;
  rv_init_t     init_data = {11, 0, 0, 0, 0, 
			     0, 1, 0}; /* rv30 */


  switch (buf->type) {
  case BUF_VIDEO_RV20:
    if (!load_syms_linux (this, "drv2.so.6.0"))
      return 0;
    this->stream->meta_info[XINE_META_INFO_VIDEOCODEC] 
      = strdup ("Real Video 2.0");
    break;
  case BUF_VIDEO_RV30:
    if (!load_syms_linux (this, "drv3.so.6.0"))
      return 0;
    this->stream->meta_info[XINE_META_INFO_VIDEOCODEC] 
      = strdup ("Real Video 3.0");
    break;
  case BUF_VIDEO_RV40:
    if (!load_syms_linux(this, "drv4.so.6.0"))
      return 0;
    this->stream->meta_info[XINE_META_INFO_VIDEOCODEC]
      = strdup ("Real Video 4.0");
    break;
  default:
    printf ("libreal: error, i don't handle buf type 0x%08x\n",
	    buf->type);
    abort();
  }

  init_data.w = BE_16(&buf->content[12]);
  init_data.h = BE_16(&buf->content[14]);
  
  this->width  = (init_data.w + 1) & (~1);
  this->height = (init_data.h + 1) & (~1);
  this->ratio  = (double)this->width/(double)this->height;

#ifdef LOG
  printf ("libreal: init_data.w=%d(0x%x), init_data.h=%d(0x%x),"
	  "this->width=%d(0x%x), this->height=%d(0x%x)\n",
	  init_data.w, init_data.w,
	  init_data.h, init_data.h,
	  this->width, this->width, this->height, this->height);
#endif
  
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH]    = this->width;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT]   = this->height;

  init_data.subformat = BE_32(&buf->content[26]);
  init_data.format    = BE_32(&buf->content[30]);
  
#ifdef LOG
  printf ("libreal: init_data for rvyuv_init:\n");
  xine_hexdump ((char *) &init_data, sizeof (init_data));
  
  printf ("libreal: buf->content\n");
  xine_hexdump (buf->content, buf->size);

  printf ("libreal: init codec %dx%d... %x %x\n", 
	  init_data.w, init_data.h,
	  init_data.subformat, init_data.format );
#endif  

  this->context = NULL;
  
  result = this->rvyuv_init (&init_data, &this->context); 
  
#ifdef LOG
  printf ("libreal: init result: %d\n", result);
#endif

  /* setup rv30 codec (codec sub-type and image dimensions): */
  if ((init_data.format>=0x20200002) && (buf->type != BUF_VIDEO_RV40)) {
    int            i, j;
    unsigned long *cmsg24;
    unsigned long  cmsg_data[9];

    cmsg24 = xine_xmalloc((buf->size - 34 + 2) * sizeof(unsigned long));
    
    cmsg24[0]=this->width;
    cmsg24[1]=this->height;
    for(i = 2, j = 34; j < buf->size; i++, j++)
      cmsg24[i] = 4 * buf->content[j];
    
    cmsg_data[0]=0x24;
    cmsg_data[1]=1+((init_data.subformat>>16)&7);
    cmsg_data[2]=(unsigned long) cmsg24;

#ifdef LOG
    printf ("libreal: CustomMessage cmsg_data:\n");
    xine_hexdump ((char *) cmsg_data, sizeof (cmsg_data));
    printf ("libreal: cmsg24:\n");
    xine_hexdump ((char *) cmsg24, (buf->size - 34 + 2) * sizeof(unsigned long));
#endif
    
    this->rvyuv_custom_message (cmsg_data, this->context);
    
    free(cmsg24);
  }
  
  this->stream->video_out->open(this->stream->video_out, this->stream);
    
  this->frame_size   = this->width*this->height;
  this->frame_buffer = xine_xmalloc (this->width*this->height*3/2);
  
  this->chunk_buffer = xine_xmalloc (BUF_SIZE);
  this->chunk_buffer_max = BUF_SIZE;
  
  this->chunk_tab = (uint32_t *) xine_xmalloc(CHUNK_TAB_SIZE * sizeof(uint32_t));
  this->chunk_tab_max = CHUNK_TAB_SIZE;

  return 1;
}

static void realdec_copy_frame (realdec_decoder_t *this, uint8_t *base[3], int pitches[3]) {
  unsigned int i, j;
  uint8_t *src, *dst;

  src = this->frame_buffer;
  dst = base[0];

  for (i=0; i < this->height; ++i) {
    memcpy (dst, src, this->width);
    src += this->width;
    dst += pitches[0];
  }

  for (i=1; i < 3; i++) {
    src = this->frame_buffer + this->frame_size; 
    dst = base[i];

    if (i == 2) {
      src += this->frame_size / 4;
    }

    for (j=0; j < (this->height / 2); ++j) {
      memcpy (dst, src, (this->width / 2));
      src += this->width / 2;
      dst += pitches[i];
    }
  }
}

static void realdec_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;

#ifdef LOG
  printf ("libreal: decode_data, flags=0x%08x, len=%d, pts=%lld ...\n", 
	  buf->decoder_flags, buf->size, buf->pts);
#endif

  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {
    /* real_find_sequence_header (&this->real, buf->content, buf->content + buf->size);*/
  } else if (buf->decoder_flags & BUF_FLAG_HEADER) {

    this->decoder_ok = init_codec (this, buf);
    if( !this->decoder_ok )
      this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HANDLED] = 0;

  } else if (this->decoder_ok && this->context) {

    if (this->chunk_buffer_size + buf->size > this->chunk_buffer_max) {
      this->chunk_buffer_max = this->chunk_buffer_size + 2 * buf->size;
      this->chunk_buffer = realloc (this->chunk_buffer, this->chunk_buffer_max);
    }
    
    if (buf->decoder_flags & BUF_FLAG_FRAME_START) {

      if (this->num_chunks>0) {

	int            result;
	vo_frame_t    *img;

	unsigned long  transform_out[5];
	unsigned long  transform_in[6];

	transform_in[0] = this->chunk_buffer_size; /* length of the packet (sub-packets appended) */
	transform_in[1] = 0; /* unknown, seems to be unused  */
	transform_in[2] = this->num_chunks-1; /* number of sub-packets - 1 */
	transform_in[3] = (unsigned long) this->chunk_tab; /* table of sub-packet offsets */
	transform_in[4] = 0; /* unknown, seems to be unused  */
	transform_in[5] = this->pts/90; /* timestamp (the integer value from the stream) */

#ifdef LOG
	printf ("libreal: got %d chunks in buffer and new frame is starting\n",
		this->num_chunks);

	printf ("libreal: decoding %d bytes:\n", this->chunk_buffer_size);
	xine_hexdump (this->chunk_buffer, this->chunk_buffer_size);

	printf ("libreal: transform_in:\n");
	xine_hexdump ((char *) transform_in, 6*4);
	
	printf ("libreal: chunk_table:\n");
	xine_hexdump ((char *) this->chunk_tab, this->num_chunks*8+8);
#endif
	
	result = this->rvyuv_transform (this->chunk_buffer, 
					this->frame_buffer, 
					transform_in,
					transform_out, 
					this->context);

#ifdef LOG
	printf ("libreal: transform result: %08x\n", result);
        
	printf ("libreal: transform_out:\n");
	xine_hexdump ((char *) transform_out, 5*4);
#endif

	/* Sometimes the stream contains video of a different size
	 * to that specified in the realmedia file */
	if(transform_out[0] && ((transform_out[3] != this->width) || 
				(transform_out[4] != this->height))) {
	  this->width  = transform_out[3];
	  this->height = transform_out[4];

	  this->frame_size = this->width * this->height;
          
	  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH]  = this->width;
	  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = this->height;
	}
        
	img = this->stream->video_out->get_frame (this->stream->video_out,
						  /* this->av_picture.linesize[0],  */
						  this->width,
						  this->height,
						  this->ratio, 
						  XINE_IMGFMT_YV12,
						  VO_BOTH_FIELDS);
	
	if ( this->last_pts && (this->pts != this->last_pts)) {
	  int64_t new_duration;

	  img->pts         = this->pts;
	  new_duration     = (this->pts - this->last_pts) / (this->num_frames+1);
	  this->duration   = (this->duration * 9 + new_duration)/10;
	  this->num_frames = 0;
	} else {
	  img->pts       = 0;
	  this->num_frames++;
	}

	if (this->pts)
	  this->last_pts = this->pts;

	img->duration  = this->duration; 
	this->stream->stream_info[XINE_STREAM_INFO_FRAME_DURATION] = this->duration;
	img->bad_frame = 0;
	
#ifdef LOG
	printf ("libreal: pts %lld %lld diff %lld # %d est. duration %lld\n", 
		this->pts, 
		buf->pts,
		buf->pts - this->pts,
		this->num_frames,
		this->duration);
#endif

	realdec_copy_frame (this, img->base, img->pitches);

	img->draw(img, this->stream);
	img->free(img);
	
      }

      /* new frame starting */

#ifdef LOG
      printf ("libreal: new frame starting (%d bytes)\n", buf->size);
#endif

      memcpy (this->chunk_buffer, buf->content, buf->size);

      this->chunk_buffer_size = buf->size;
      this->chunk_tab[0]      = 1;
      this->chunk_tab[1]      = 0;
      this->num_chunks        = 1;

      this->chunk_id          = buf->content[0];

      if (buf->pts)
	this->pts = buf->pts;
      else
	this->pts = 0;
    } else {

      /* buffer another fragment */

#ifdef LOG
      printf ("libreal: another fragment (%d chunks in buffer)\n", 
	      this->num_chunks);
#endif

      if (((buf->type != BUF_VIDEO_RV30) && (buf->type != BUF_VIDEO_RV40)) ||
          (buf->content[0] == this->chunk_id)) {

	memcpy (this->chunk_buffer+this->chunk_buffer_size, buf->content, buf->size);

	if(2*this->num_chunks+1 >= this->chunk_tab_max) {
	  this->chunk_tab_max += CHUNK_TAB_SIZE;
	  this->chunk_tab = realloc (this->chunk_tab, 
				     this->chunk_tab_max * sizeof(uint32_t));
	}

	this->chunk_tab[2*this->num_chunks]    = 1;
	this->chunk_tab[2*this->num_chunks+1]  = this->chunk_buffer_size; 
	this->num_chunks++;
	this->chunk_buffer_size               += buf->size;
	
	if (buf->pts)
	  this->pts = buf->pts;
      }

    }
  }

#ifdef LOG
  printf ("libreal: decode_data...done\n");
#endif
}

static void realdec_flush (video_decoder_t *this_gen) {
  /* realdec_decoder_t *this = (realdec_decoder_t *) this_gen; */

#ifdef LOG
  printf ("libreal: flush\n");
#endif

}

static void realdec_reset (video_decoder_t *this_gen) {
  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;
  
  this->num_chunks        = 0;
}

static void realdec_discontinuity (video_decoder_t *this_gen) {
  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;
  
  this->pts        = 0;
  this->last_pts   = 0;
  this->num_frames = 0;
}

static void realdec_dispose (video_decoder_t *this_gen) {

  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;

#ifdef LOG
  printf ("libreal: dispose\n");
#endif

  if (this->context)
    this->stream->video_out->close(this->stream->video_out, this->stream);

  if (this->rvyuv_free && this->context)
    this->rvyuv_free (this->context);

  if (this->rv_handle) 
    dlclose (this->rv_handle);

  if (this->frame_buffer)
    free (this->frame_buffer);
    
  if (this->chunk_buffer)
    free (this->chunk_buffer);
    
  if (this->chunk_tab)
    free (this->chunk_tab);

  free (this);

#ifdef LOG
  printf ("libreal: dispose done\n");
#endif
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, 
				     xine_stream_t *stream) {

  real_class_t      *cls = (real_class_t *) class_gen;
  realdec_decoder_t *this ;

  this = (realdec_decoder_t *) xine_xmalloc (sizeof (realdec_decoder_t));
  memset(this, 0, sizeof (realdec_decoder_t));

  this->video_decoder.decode_data         = realdec_decode_data;
  this->video_decoder.flush               = realdec_flush;
  this->video_decoder.reset               = realdec_reset;
  this->video_decoder.discontinuity       = realdec_discontinuity;
  this->video_decoder.dispose             = realdec_dispose;
  this->stream                            = stream;
  this->cls                               = cls;

  this->context    = 0;
  this->num_chunks = 0;
  this->pts        = 0;
  this->last_pts   = 0;
  this->num_frames = 0;
  this->duration   = 6000;

  return &this->video_decoder;
}

/*
 * real plugin class
 */

static char *get_identifier (video_decoder_class_t *this) {
  return "realvdec";
}

static char *get_description (video_decoder_class_t *this) {
  return "real binary-only codec based video decoder plugin";
}

static void dispose_class (video_decoder_class_t *this) {
  free (this);
}

/*
 * some fake functions to make real codecs happy 
 */
void *__builtin_vec_new(unsigned long size) {
  return malloc(size);
}
void __builtin_vec_delete(void *mem) {
  free(mem);
}
void __pure_virtual(void) {
  printf("libreal: FATAL: __pure_virtual() called!\n");
  /*      exit(1); */
}


static void *init_class (xine_t *xine, void *data) {

  real_class_t       *this;
  config_values_t    *config = xine->config;
  char               *real_codec_path;

  this = (real_class_t *) xine_xmalloc (sizeof (real_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  real_codec_path = config->register_string (config, "codec.real_codecs_path", 
					     "unknown",
					     _("path to real player codecs, if installed"),
					     NULL, 10, NULL, NULL);
  
  if (!strcmp (real_codec_path, "unknown")) {

    struct stat s;

    /* try some auto-detection */

    if (!stat ("/usr/local/RealPlayer8/Codecs/drv3.so.6.0", &s)) 
      config->update_string (config, "codec.real_codecs_path", 
			     "/usr/local/RealPlayer8/Codecs");
    if (!stat ("/usr/RealPlayer8/Codecs/drv3.so.6.0", &s)) 
      config->update_string (config, "codec.real_codecs_path", 
			     "/usr/RealPlayer8/Codecs");
    if (!stat ("/usr/lib/RealPlayer8/Codecs/drv3.so.6.0", &s)) 
      config->update_string (config, "codec.real_codecs_path", 
			     "/usr/lib/RealPlayer8/Codecs");
    if (!stat ("/opt/RealPlayer8/Codecs/drv3.so.6.0", &s)) 
      config->update_string (config, "codec.real_codecs_path", 
			     "/opt/RealPlayer8/Codecs");
    if (!stat ("/usr/lib/RealPlayer9/users/Real/Codecs/drv3.so.6.0", &s)) 
      config->update_string (config, "codec.real_codecs_path", 
			     "/usr/lib/RealPlayer9/users/Real/Codecs");
    if (!stat ("/usr/lib/win32/drv3.so.6.0", &s)) 
      config->update_string (config, "codec.real_codecs_path", 
			     "/usr/lib/win32");
  }

#ifdef LOG
  printf ("libareal: real codec path : %s\n",  real_codec_path);
#endif

  return this;
}

/*
 * exported plugin catalog entry
 */

static uint32_t supported_types[] = { BUF_VIDEO_RV20,
                                      BUF_VIDEO_RV30,
                                      BUF_VIDEO_RV40,
                                      0 };

static decoder_info_t dec_info_real = {
  supported_types,     /* supported types */
  6                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER | PLUGIN_MUST_PRELOAD, 16, "real", XINE_VERSION_CODE, &dec_info_real, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
