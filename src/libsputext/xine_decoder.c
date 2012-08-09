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
 * $Id: xine_decoder.c,v 1.64 2003/10/23 20:12:34 mroi Exp $
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#include "buffer.h"
#include "xine_internal.h"
#include "xineutils.h"
#include "osd.h"

/*
#define LOG 1
*/

#define SUB_MAX_TEXT  5

#define SUB_BUFSIZE 1024


typedef enum {
  SUBTITLE_SIZE_TINY = 0,
  SUBTITLE_SIZE_SMALL,
  SUBTITLE_SIZE_NORMAL,
  SUBTITLE_SIZE_LARGE,

  SUBTITLE_SIZE_NUM        /* number of values in enum */
} subtitle_size;

#define FONTNAME_SIZE 100

typedef struct sputext_class_s {
  spu_decoder_class_t class;

  subtitle_size      subtitle_size;   /* size of subtitles */
  int                vertical_offset;
  char               font[FONTNAME_SIZE]; /* subtitle font */
  char              *src_encoding;    /* encoding of subtitle file */

  xine_t            *xine;

} sputext_class_t;


typedef struct sputext_decoder_s {
  spu_decoder_t      spu_decoder;

  sputext_class_t   *class;
  xine_stream_t     *stream;

  int                lines;
  char               text[SUB_MAX_TEXT][SUB_BUFSIZE];

  /* below 3 variables are the same from class. use to detect
   * when something changes.
   */
  subtitle_size      subtitle_size;   /* size of subtitles */
  int                vertical_offset;
  char               font[FONTNAME_SIZE]; /* subtitle font */

  int                width;          /* frame width                */
  int                height;         /* frame height               */
  int                font_size;
  int                line_height;
  int                seek_count;
  int                master_started;
  int                slave_started;

  osd_renderer_t    *renderer;
  osd_object_t      *osd;

  int64_t            img_duration;
  int64_t            last_subtitle_end; /* no new subtitle before this vpts */
} sputext_decoder_t;


static void update_font_size (sputext_decoder_t *this) {
  static int sizes[SUBTITLE_SIZE_NUM] = { 16, 20, 24, 32 };

  int  y;

  this->font_size = sizes[this->class->subtitle_size];
  
  this->line_height = this->font_size + 10;

  y = this->height - (SUB_MAX_TEXT * this->line_height) - 5;
  
  if(((y - this->class->vertical_offset) >= 0) && ((y - this->class->vertical_offset) <= this->height))
    y -= this->class->vertical_offset;
  
  if( this->osd )
    this->renderer->free_object (this->osd);

  if(this->renderer) {
    this->osd = this->renderer->new_object (this->renderer, 
					    this->width,
					    SUB_MAX_TEXT * this->line_height);
    
    this->renderer->set_font (this->osd, this->class->font, this->font_size);
    this->renderer->set_position (this->osd, 0, y);
  }
}


static void draw_subtitle(sputext_decoder_t *this, int64_t sub_start, int64_t sub_end ) {
  
  int line, y;
  int font_size;

  /* update settings from class */
  if( this->subtitle_size != this->class->subtitle_size ||
      this->vertical_offset != this->class->vertical_offset ) {
    this->subtitle_size = this->class->subtitle_size;
    this->vertical_offset = this->class->vertical_offset;
    update_font_size(this);
  }
  if( strcmp(this->font, this->class->font) ) {
    strcpy(this->font, this->class->font);
    if( this->renderer )
      this->renderer->set_font (this->osd, this->class->font, this->font_size);
  }
  
  this->renderer->filled_rect (this->osd, 0, 0, this->width-1, this->line_height * SUB_MAX_TEXT - 1, 0);
  
  y = (SUB_MAX_TEXT - this->lines) * this->line_height;
  font_size = this->font_size;

  this->renderer->set_encoding(this->osd, this->class->src_encoding);
  
  for (line=0; line<this->lines; line++) {
    int w,h,x;
          
    while(1) {
      this->renderer->get_text_size( this->osd, this->text[line], 
                                     &w, &h);
      x = (this->width - w) / 2;
            
      if( w > this->width && font_size > 16 ) {
        font_size -= 4;
        this->renderer->set_font (this->osd, this->class->font, font_size);
      } else {
        break;
      }
    }
          
    this->renderer->render_text (this->osd, x, y + line*this->line_height,
                                 this->text[line], OSD_TEXT1);
  }
         
  if( font_size != this->font_size )
    this->renderer->set_font (this->osd, this->class->font, this->font_size);
  
  if( this->last_subtitle_end && sub_start < this->last_subtitle_end ) {
    sub_start = this->last_subtitle_end;
  }
  this->last_subtitle_end = sub_end;
          
  this->renderer->set_text_palette (this->osd, -1, OSD_TEXT1);
  this->renderer->show (this->osd, sub_start);
  this->renderer->hide (this->osd, sub_end);
  
#ifdef LOG
  printf ("sputext: scheduling subtitle >%s< at %lld until %lld, current time is %lld\n",
          this->text[0], sub_start, sub_end, 
          this->stream->xine->clock->get_current_time (this->stream->xine->clock));
#endif
}


static void spudec_decode_data (spu_decoder_t *this_gen, buf_element_t *buf) {

  sputext_decoder_t *this = (sputext_decoder_t *) this_gen;
  int uses_time;
  int32_t start, end, diff;
  int64_t start_vpts, end_vpts;
  int64_t spu_offset;
  int i;
  uint32_t *val;
  char *str;
  extra_info_t extra_info;
  int status;
  
  /* filter unwanted streams */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;
  if ((this->stream->spu_channel & 0x1f) != (buf->type & 0x1f))
    return;

  val = (uint32_t * )buf->content;
  this->lines = *val++;
  uses_time = *val++;
  start = *val++;
  end = *val++;
  str = (char *)val;
  for (i = 0; i < this->lines; i++, str+=strlen(str)+1) {
    strcpy( this->text[i], str );
  }
  
#ifdef LOG
  printf("libsputext: decoder data [%s]\n", this->text[0]);
  printf("libsputext: mode %d timing %d->%d\n", uses_time, start, end);
#endif

  if( end <= start ) {
#ifdef LOG
    printf("libsputext: discarding subtitle with invalid timing\n");
#endif
  }
  
  spu_offset = this->stream->master->metronom->get_option (this->stream->master->metronom,
                                                           METRONOM_SPU_OFFSET);
  start += (spu_offset / 90);
  end += (spu_offset / 90);
   
  xine_get_current_info (this->stream->master, &extra_info, sizeof(extra_info) );
  
  if( !this->seek_count ) {
    this->seek_count = extra_info.seek_count;
  }
   
  while(this->seek_count == extra_info.seek_count) {
  
    /* initialize decoder if needed */
    if( !this->width || !this->height || !this->img_duration || !this->osd ) {
      
      if( this->stream->video_out->status(this->stream->video_out, NULL,
                                           &this->width, &this->height, &this->img_duration )) {
                                             
        if( this->width && this->height && this->img_duration ) {
          this->renderer = this->stream->osd_renderer;
        
          update_font_size (this);
        }
      }
    }
    
    if( this->osd ) {
      
      /* try to use frame number mode */
      if( !uses_time && extra_info.frame_number ) {
        
        diff = end - extra_info.frame_number;
        
        /* discard old subtitles */
        if( diff < 0 ) {
#ifdef LOG
          printf("libsputext: discarding old\n");
#endif
          return;
        }
          
        diff = start - extra_info.frame_number;
        
        /* draw it if less than 1/2 second left */
        if( diff < 90000/2 / this->img_duration ) {
          start_vpts = extra_info.vpts + diff * this->img_duration;
          end_vpts = start_vpts + (end-start) * this->img_duration;
     
          draw_subtitle(this, start_vpts, end_vpts);
          return;     
        }
        
      } else {
        
        if( !uses_time ) {
          start = start * this->img_duration / 90;
          end = end * this->img_duration / 90;
          uses_time = 1;
        }
        
        diff = end - extra_info.input_time;
        
        /* discard old subtitles */
        if( diff < 0 ) {
#ifdef LOG
          printf("libsputext: discarding old\n");
#endif
          return;
        }
          
        diff = start - extra_info.input_time;
        
        /* draw it if less than 1/2 second left */
        if( diff < 500 ) {
          start_vpts = extra_info.vpts + diff * 90;
          end_vpts = start_vpts + (end-start) * 90;
          
          draw_subtitle(this, start_vpts, end_vpts);
          return;     
        }
      }
    }
    
    status = xine_get_status (this->stream->master);
   
    if( this->master_started && (status == XINE_STATUS_QUIT || 
                                 status == XINE_STATUS_STOP) ) {
#ifdef LOG
      printf("libsputext: master stopped\n");
#endif
      this->width = this->height = 0;
      return;
    }
    if( status == XINE_STATUS_PLAY )
      this->master_started = 1;
    
    status = xine_get_status (this->stream);
   
    if( this->slave_started && (status == XINE_STATUS_QUIT || 
                                status == XINE_STATUS_STOP) ) {
#ifdef LOG
      printf("libsputext: slave stopped\n");
#endif
      this->width = this->height = 0;
      return;
    }
    if( status == XINE_STATUS_PLAY )
      this->slave_started = 1;

    xine_usec_sleep (50000);
            
    xine_get_current_info (this->stream->master, &extra_info, sizeof(extra_info) );
  }
#ifdef LOG
  printf("libsputext: seek_count mismatch\n");
#endif
}  


static void spudec_reset (spu_decoder_t *this_gen) {
  sputext_decoder_t *this = (sputext_decoder_t *) this_gen;
  
  this->width = this->height = 0;
  this->seek_count = 0;
}

static void spudec_discontinuity (spu_decoder_t *this_gen) {
  /* sputext_decoder_t *this = (sputext_decoder_t *) this_gen; */

}

static void spudec_dispose (spu_decoder_t *this_gen) {
  sputext_decoder_t *this = (sputext_decoder_t *) this_gen;

  if (this->osd) {
    this->renderer->free_object (this->osd);
    this->osd = NULL;
  }
  free(this);
}

static void update_vertical_offset(void *class_gen, xine_cfg_entry_t *entry)
{
  sputext_class_t *class = (sputext_class_t *)class_gen;

  class->vertical_offset = entry->num_value;
}

static void update_osd_font(void *class_gen, xine_cfg_entry_t *entry)
{
  sputext_class_t *class = (sputext_class_t *)class_gen;

  strcpy(class->font, entry->str_value);
  
  printf("libsputext: spu_font = %s\n", class->font );
}

static void update_subtitle_size(void *class_gen, xine_cfg_entry_t *entry)
{
  sputext_class_t *class = (sputext_class_t *)class_gen;

  class->subtitle_size = entry->num_value;
}

static spu_decoder_t *sputext_class_open_plugin (spu_decoder_class_t *class_gen, xine_stream_t *stream) {

  sputext_class_t *class = (sputext_class_t *)class_gen;
  sputext_decoder_t *this ;

  this = (sputext_decoder_t *) xine_xmalloc (sizeof (sputext_decoder_t));

  this->spu_decoder.decode_data         = spudec_decode_data;
  this->spu_decoder.reset               = spudec_reset;
  this->spu_decoder.discontinuity       = spudec_discontinuity;
  this->spu_decoder.dispose             = spudec_dispose;
  this->spu_decoder.get_interact_info   = NULL;
  this->spu_decoder.set_button          = NULL;
  this->spu_decoder.dispose             = spudec_dispose;

  this->class  = class;
  this->stream = stream;

  return (spu_decoder_t *) this;
}

static void sputext_class_dispose (spu_decoder_class_t *this) {
  free (this);
}

static char *sputext_class_get_identifier (spu_decoder_class_t *this) {
  return "sputext";
}

static char *sputext_class_get_description (spu_decoder_class_t *this) {
  return "external subtitle decoder plugin";
}

static void update_src_encoding(void *class_gen, xine_cfg_entry_t *entry)
{
  sputext_class_t *class = (sputext_class_t *)class_gen;

  class->src_encoding = entry->str_value;
  printf("libsputext: spu_src_encoding = %s\n", class->src_encoding );
}

static void *init_spu_decoder_plugin (xine_t *xine, void *data) {

  static char *subtitle_size_strings[] = { 
    "tiny", "small", "normal", "large", NULL 
  };
  sputext_class_t *this ;

#ifdef LOG
  printf("libsputext: init class\n");
#endif
  
  this = (sputext_class_t *) xine_xmalloc (sizeof (sputext_class_t));

  this->class.open_plugin      = sputext_class_open_plugin;
  this->class.get_identifier   = sputext_class_get_identifier;
  this->class.get_description  = sputext_class_get_description;
  this->class.dispose          = sputext_class_dispose;

  this->xine                   = xine;

  this->subtitle_size  = xine->config->register_enum(xine->config, 
			      "misc.spu_subtitle_size", 
			       1,
			       subtitle_size_strings,
			       _("Subtitle size (relative window size)"), 
			       NULL, 0, update_subtitle_size, this);
  this->vertical_offset  = xine->config->register_num(xine->config,
			      "misc.spu_vertical_offset", 
			      0,
			      _("Subtitle vertical offset (relative window size)"), 
			      NULL, 0, update_vertical_offset, this);
  strcpy(this->font,       xine->config->register_string(xine->config, 
				"misc.spu_font", 
				"sans", 
				_("Font for external subtitles"), 
				NULL, 0, update_osd_font, this));
  this->src_encoding  = xine->config->register_string(xine->config, 
				"misc.spu_src_encoding", 
				"iso-8859-1", 
				_("Encoding of subtitles"), 
				NULL, 10, update_src_encoding, this);

  return &this->class;
}


/* plugin catalog information */
static uint32_t supported_types[] = { BUF_SPU_TEXT, 0 };

static decoder_info_t spudec_info = {
  supported_types,     /* supported types */
  1                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_SPU_DECODER | PLUGIN_MUST_PRELOAD, 15, "sputext", XINE_VERSION_CODE, &spudec_info, &init_spu_decoder_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
