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
 * $Id: switch.c,v 1.8 2003/10/23 20:12:34 mroi Exp $
 */
 
/*
 * simple switch video post plugin
 */

#include "xine_internal.h"
#include "post.h"

#define SWVERSION (5)

/*
#define LOG
*/

/* plugin class initialization function */
static void *switch_init_plugin(xine_t *xine, void *);

/* plugin catalog information */
post_info_t switch_special_info = { XINE_POST_TYPE_VIDEO_COMPOSE };

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_POST, 6, "switch", SWVERSION, &switch_special_info, &switch_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

/* plugin structure */
typedef struct post_switch_out_s post_switch_out_t;
struct post_switch_out_s {
  xine_post_out_t  xine_out;
  /* keep the stream for open/close when rewiring */
  xine_stream_t   *stream; 
  pthread_mutex_t mut1;
  unsigned int pip;
  unsigned int selected_source;
};

typedef struct post_class_switch_s post_class_switch_t;
struct post_class_switch_s {
  post_class_t class;
  xine_t *xine;
  post_switch_out_t *ip;
};

/* plugin class functions */
static post_plugin_t *switch_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target);
static char          *switch_get_identifier(post_class_t *class_gen);
static char          *switch_get_description(post_class_t *class_gen);
static void           switch_class_dispose(post_class_t *class_gen);

/* plugin instance functions */
static void           switch_dispose(post_plugin_t *this_gen);

/* rewire function */
static int            switch_rewire(xine_post_out_t *output, void *data);

/* replaced video_port functions */
static void           switch_open(xine_video_port_t *port_gen, xine_stream_t *stream);
static vo_frame_t    *switch_get_frame(xine_video_port_t *port_gen, uint32_t width, 
				       uint32_t height, double ratio, 
				       int format, int flags);
static vo_frame_t    *switch_get_frame_2(xine_video_port_t *port_gen, uint32_t width, 
				       uint32_t height, double ratio, 
				       int format, int flags);
static void           switch_close(xine_video_port_t *port_gen, xine_stream_t *stream);

/* replaced vo_frame functions */
static int            switch_draw(vo_frame_t *frame, xine_stream_t *stream);
static int            switch_draw_2(vo_frame_t *frame, xine_stream_t *stream);

static void source_changed_cb(void *data, xine_cfg_entry_t *cfg) {
  post_class_switch_t *class = (post_class_switch_t *)data; 

  if(class->ip) {
    post_switch_out_t *this = class->ip;
    pthread_mutex_lock(&this->mut1); 

    this->selected_source = cfg->num_value;
    pthread_mutex_unlock(&this->mut1); 
  } 
}

static void *switch_init_plugin(xine_t *xine, void *data)
{
  post_class_switch_t *this = (post_class_switch_t *)malloc(sizeof(post_class_switch_t));
  config_values_t *cfg;
  char string[255];

  if (!this)
    return NULL;
  
  this->class.open_plugin     = switch_open_plugin;
  this->class.get_identifier  = switch_get_identifier;
  this->class.get_description = switch_get_description;
  this->class.dispose         = switch_class_dispose;
  this->xine                  = xine;
  this->ip                    = NULL;
  cfg = xine->config;

  sprintf(string, "post.switch_active");
  cfg->register_num (cfg, string, 0, _("Default active stream"), NULL, 10, source_changed_cb, this);

  return &this->class;
}

static post_plugin_t *switch_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_t     *this   = (post_plugin_t *)malloc(sizeof(post_plugin_t));
  xine_post_in_t    *input1  = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  xine_post_in_t    *input2;
  post_switch_out_t *output = (post_switch_out_t *)malloc(sizeof(post_switch_out_t));
  post_class_switch_t *class = (post_class_switch_t *) class_gen;
  post_video_port_t *port = NULL;/*, *port2;*/
  /* int i; */
  char string[255];
  xine_cfg_entry_t    entry;
 
    
  if(inputs < 2) return NULL;

#ifdef LOG
  printf("switch open\n");
#endif

  if (!this || !input1 || !output || !video_target || !video_target[0]) {
    free(this);
    free(input1);
    free(output);
    return NULL;
  }

  class->ip = output;  

  this->input = xine_list_new();
  this->output = xine_list_new();

  this->xine_post.audio_input    = (xine_audio_port_t **)malloc(sizeof(xine_audio_port_t *));
  this->xine_post.audio_input[0] = NULL;
  this->xine_post.video_input    = (xine_video_port_t **)malloc(sizeof(xine_video_port_t *) * (inputs+1));

  /* for(i=0; i<inputs; i++) { */

  /* first input */
  input2 = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));

  port = post_intercept_video_port(this, video_target[0]);
  /* replace with our own get_frame function */
  port->port.open = switch_open;
  port->port.get_frame = switch_get_frame;
  port->port.close = switch_close;

  sprintf(string, "video in 0");
  input2->name = strdup(string);
  input2->type = XINE_POST_DATA_VIDEO;
  input2->data = (xine_video_port_t *)&port->port;
    
  this->xine_post.video_input[0] = &port->port;
  xine_list_append_content(this->input, input2);

  /* second input */
  input2 = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));

  port = post_intercept_video_port(this, video_target[1]);
  /* replace with our own get_frame function */
  port->port.open = switch_open;
  port->port.get_frame = switch_get_frame_2;
  port->port.close = switch_close;

  sprintf(string, "video in 1");
  input2->name = strdup(string);
  input2->type = XINE_POST_DATA_VIDEO;
  input2->data = (xine_video_port_t *)&port->port;
    
  this->xine_post.video_input[1] = &port->port;
  xine_list_append_content(this->input, input2);
  
  /* output */
  output->xine_out.name   = "video out";
  output->xine_out.type   = XINE_POST_DATA_VIDEO;
  output->xine_out.data   = (xine_video_port_t **)&port->original_port;
  output->xine_out.rewire = switch_rewire;
  output->stream          = NULL;
  xine_list_append_content(this->output, output);
 
  pthread_mutex_init(&output->mut1, NULL); 

  if(xine_config_lookup_entry(class->xine, "post.switch_active", &entry)) 
    source_changed_cb(class, &entry);

  this->xine_post.video_input[2] = NULL;
  this->dispose = switch_dispose;

  return this;
}

static char *switch_get_identifier(post_class_t *class_gen)
{
  return "switch";
}

static char *switch_get_description(post_class_t *class_gen)
{
  return "Switch is a post plugin able to switch at any time from different streams";
}

static void switch_class_dispose(post_class_t *class_gen)
{
  free(class_gen);
}


static void switch_dispose(post_plugin_t *this)
{
  post_switch_out_t *output = (post_switch_out_t *)xine_list_first_content(this->output);
  xine_video_port_t *port = *(xine_video_port_t **)output->xine_out.data;
  
  if (output->stream)
    port->close(port, output->stream);

  free(this->xine_post.audio_input);
  free(this->xine_post.video_input);
  free(xine_list_first_content(this->input));
  free(xine_list_first_content(this->output));
  xine_list_free(this->input);
  xine_list_free(this->output);
  free(this);
}


static int switch_rewire(xine_post_out_t *output_gen, void *data)
{
  post_switch_out_t *output = (post_switch_out_t *)output_gen;
  xine_video_port_t *old_port = *(xine_video_port_t **)output_gen->data;
  /*xine_post_in_t *input = (xine_post_in_t *) data;*/
  xine_video_port_t *new_port = (xine_video_port_t *)data;  

  if (!data)
    return 0;
  if (output->stream) {   
    /* register our stream at the new output port */
    old_port->close(old_port, output->stream);
    new_port->open(new_port, output->stream);
  }
  /* reconnect ourselves */
  *(xine_video_port_t **)output_gen->data = new_port;
  return 1;
}

static void switch_open(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_switch_out_t *output = (post_switch_out_t *)xine_list_first_content(port->post->output);
 
  output->stream = stream;
  port->original_port->open(port->original_port, stream);
   
}

static vo_frame_t *switch_get_frame(xine_video_port_t *port_gen, uint32_t width, 
				    uint32_t height, double ratio, 
				    int format, int flags)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  vo_frame_t        *frame;
  post_switch_out_t *output = (post_switch_out_t *)xine_list_first_content(port->post->output);

  pthread_mutex_lock(&output->mut1); 
  frame = port->original_port->get_frame(port->original_port,
					 width, height , ratio, format, flags);

  post_intercept_video_frame(frame, port);
  /* replace with our own draw function */
  frame->draw = switch_draw;
  /* decoders should not copy the frames, since they won't be displayed */
  frame->proc_slice = NULL;
  frame->proc_frame = NULL;
  pthread_mutex_unlock(&output->mut1); 

  return frame;
}

static vo_frame_t *switch_get_frame_2(xine_video_port_t *port_gen, uint32_t width, 
				    uint32_t height, double ratio, 
				    int format, int flags)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  vo_frame_t        *frame;
  post_switch_out_t *output = (post_switch_out_t *)xine_list_first_content(port->post->output);

  pthread_mutex_lock(&output->mut1); 

  frame = port->original_port->get_frame(port->original_port,
					 width, height , ratio, format, flags);

  post_intercept_video_frame(frame, port);
  /* replace with our own draw function */
  frame->draw = switch_draw_2;
  /* decoders should not copy the frames, since they won't be displayed */
  frame->proc_slice = NULL;
  frame->proc_frame = NULL;
  pthread_mutex_unlock(&output->mut1); 

  return frame;
}

static void switch_close(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_switch_out_t *output = (post_switch_out_t *)xine_list_first_content(port->post->output);
  output->stream = NULL;
  port->original_port->close(port->original_port, stream);
}

static void frame_copy_content(vo_frame_t *to, vo_frame_t *from) {
  int size;

  if((to == NULL)||(from == NULL)) {
#ifdef LOG
    printf("Something wrong in frame_copy_content\n");
#endif
    return;
  }

  if(to->format != from->format) {
#ifdef LOG
    printf("frame_copy_content : buffers have different format\n");
#endif
    return;
  }

  switch (from->format) {
  case XINE_IMGFMT_YUY2:
    size = to->pitches[0] * to->height;   
    xine_fast_memcpy(to->base[0], from->base[0], size);
    break;     
  
  case XINE_IMGFMT_YV12:
    /* Y */
    size = to->pitches[0] * to->height;   
    xine_fast_memcpy(to->base[0], from->base[0], size);

    /* U */
    size = to->pitches[1] * ((to->height + 1) / 2);
    xine_fast_memcpy(to->base[1], from->base[1], size);

    /* V */
    size = to->pitches[2] * ((to->height + 1) / 2);
    xine_fast_memcpy(to->base[2], from->base[2], size);
    
  }
}

static int switch_draw_2(vo_frame_t *frame, xine_stream_t *stream)
{
  int skip;
  post_video_port_t *port = (post_video_port_t *)frame->port;
  post_switch_out_t *output = (post_switch_out_t *)xine_list_first_content(port->post->output);
  vo_frame_t *res_frame;

  pthread_mutex_lock(&output->mut1); 

  if(!output->selected_source) {
    /* printf("draw_2 quitting\n"); */
    post_restore_video_frame(frame, port); 
    pthread_mutex_unlock(&output->mut1); 
    return 0;
  }
  /* printf("draw_2\n"); */
 
  res_frame = port->original_port->get_frame(port->original_port,
    frame->width, frame->height, frame->ratio, frame->format, frame->flags | VO_BOTH_FIELDS);
  res_frame->pts = frame->pts;
  res_frame->duration = frame->duration;
  res_frame->bad_frame = frame->bad_frame;
  extra_info_merge(res_frame->extra_info, frame->extra_info);
 

  frame_copy_content(res_frame, frame);

  skip = res_frame->draw(res_frame, stream);
  
  res_frame->free(res_frame);
  frame->vpts = res_frame->vpts;

  post_restore_video_frame(frame, port);

  pthread_mutex_unlock(&output->mut1); 

  return skip;
}

static int switch_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  int skip;
  post_video_port_t *port = (post_video_port_t *)frame->port;
  post_switch_out_t *output = (post_switch_out_t *)xine_list_first_content(port->post->output);
  vo_frame_t *res_frame;

  pthread_mutex_lock(&output->mut1); 

  if(output->selected_source) {
    /* printf("draw_1 quitting\n"); */
    post_restore_video_frame(frame, port); 
    pthread_mutex_unlock(&output->mut1); 
    return 0;
  }
  /* printf("draw_1\n"); */

  res_frame = port->original_port->get_frame(port->original_port,
    frame->width, frame->height, frame->ratio, frame->format, frame->flags | VO_BOTH_FIELDS);
  res_frame->pts = frame->pts;
  res_frame->duration = frame->duration;
  res_frame->bad_frame = frame->bad_frame;
  extra_info_merge(res_frame->extra_info, frame->extra_info);
 

  frame_copy_content(res_frame, frame);

  skip = res_frame->draw(res_frame, stream);
  
  res_frame->free(res_frame);
  frame->vpts = res_frame->vpts;

  post_restore_video_frame(frame, port);

  pthread_mutex_unlock(&output->mut1); 

  return skip;
}
