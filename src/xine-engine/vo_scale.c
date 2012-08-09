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
 * $Id: vo_scale.c,v 1.25 2003/10/07 14:56:51 mroi Exp $
 * 
 * Contains common code to calculate video scaling parameters.
 * In short, it will map frame dimensions to screen/window size.
 * Takes into account aspect ratio correction and zooming.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "xine_internal.h"
#include "video_out.h"
#include "vo_scale.h"

/*
#define LOG
*/

/*
 * convert delivered height/width to ideal width/height
 * taking into account aspect ratio and zoom factor
 */

void vo_scale_compute_ideal_size (vo_scale_t *this) {

  double image_ratio, desired_ratio;

  if (this->scaling_disabled) {

    this->video_pixel_aspect = this->gui_pixel_aspect;

  } else {
  
    /* 
     * aspect ratio
     */
  
    image_ratio = (double) this->delivered_width / (double) this->delivered_height;
    
    switch (this->user_ratio) {
    case XINE_VO_ASPECT_AUTO:
      desired_ratio = this->delivered_ratio;
      break;
    case XINE_VO_ASPECT_ANAMORPHIC:
      desired_ratio = 16.0 / 9.0;
      break;
    case XINE_VO_ASPECT_DVB:
      desired_ratio = 2.0 / 1.0;
      break;
    case XINE_VO_ASPECT_SQUARE:
      desired_ratio = image_ratio;
      break;
    case XINE_VO_ASPECT_4_3:
    default:
      desired_ratio = 4.0 / 3.0;
    }

    this->video_pixel_aspect = desired_ratio / image_ratio;
    
    XINE_ASSERT(this->gui_pixel_aspect != 0.0, "GUI pixel aspect is not 0.0: %f",this->gui_pixel_aspect);

    if (fabs (this->video_pixel_aspect / this->gui_pixel_aspect - 1.0)
	< 0.01) {
      this->video_pixel_aspect = this->gui_pixel_aspect;
    }

#if 0
    
    /* onefield_xv divide by 2 the number of lines */
    if (this->deinterlace_enabled
	&& (this->deinterlace_method == DEINTERLACE_ONEFIELDXV)
	&& (this->cur_frame->format == XINE_IMGFMT_YV12)) {
      this->displayed_height  = this->displayed_height / 2;
      this->displayed_yoffset = this->displayed_yoffset / 2;
    }
#endif
  }
}


/*
 * make ideal width/height "fit" into the gui
 */

void vo_scale_compute_output_size (vo_scale_t *this) {
  
  double x_factor, y_factor, aspect;
    
  aspect   = this->video_pixel_aspect / this->gui_pixel_aspect;
  x_factor = (double) this->gui_width  / (double) (this->delivered_width * aspect);
  y_factor = (double) (this->gui_height * aspect) / (double)  this->delivered_height;

  if (this->scaling_disabled) {

    this->output_width   = this->delivered_width;
    this->output_height  = this->delivered_height;
    this->displayed_width = this->delivered_width;
    this->displayed_height = this->delivered_height;

  } else {

    if ( this->support_zoom ) {
    
      /* zoom behaviour: 
       * - window size never changes due zooming
       * - output image shall be increased whenever there are
       *   black borders to use.
       * - exceding zoom shall be accounted by reducing displayed image.
       */
      if (((double)this->gui_width - (double)this->delivered_width * y_factor) < ((double)this->gui_height - (double)this->delivered_height * x_factor)) {
        this->output_width = this->gui_width;
        this->displayed_width = (double)this->delivered_width / this->zoom_factor_x + 0.5;
        
        this->output_height = (double)this->delivered_height * x_factor + 0.5;
        if( this->output_height * this->zoom_factor_y <= this->gui_height ) {
          this->displayed_height = this->delivered_height;
          this->output_height = (double)this->output_height * this->zoom_factor_y + 0.5;
        } else {
          this->displayed_height = (double) this->delivered_height *
            this->gui_height / this->output_height / this->zoom_factor_y + 0.5;
          this->output_height = this->gui_height;
        }
      } else {
        this->output_height = this->gui_height;
        this->displayed_height = (double)this->delivered_height / this->zoom_factor_y + 0.5;
        
        this->output_width = (double)this->delivered_width * y_factor + 0.5;
        if( this->output_width * this->zoom_factor_x <= this->gui_width ) {
          this->displayed_width = this->delivered_width;
          this->output_width = (double)this->output_width * this->zoom_factor_x + 0.5;
        } else {
          this->displayed_width = (double) this->delivered_width *
            this->gui_width / this->output_width / this->zoom_factor_x + 0.5;
          this->output_width = this->gui_width;
        }
      }
    
    } else {
      if (((double)this->gui_width - (double)this->delivered_width * y_factor) < ((double)this->gui_height - (double)this->delivered_height * x_factor)) {
        this->output_width   = (double) this->gui_width;
        this->output_height  = (double) this->delivered_height * x_factor + 0.5;
      } else {
        this->output_width   = (double) this->delivered_width  * y_factor + 0.5;
        this->output_height  = (double) this->gui_height;
      }
      this->displayed_width = this->delivered_width;
      this->displayed_height = this->delivered_height;
    }
  }  
  this->output_xoffset =
    (this->gui_width - this->output_width) * this->output_horizontal_position + this->gui_x;
  this->output_yoffset =
    (this->gui_height - this->output_height) * this->output_vertical_position + this->gui_y;
  
  this->displayed_xoffset = (this->delivered_width  - this->displayed_width) / 2;
  this->displayed_yoffset = (this->delivered_height - this->displayed_height) / 2;

#ifdef LOG
  printf ("vo_scale: frame source %d x %d (%d x %d) => screen output %d x %d\n",
	  this->delivered_width, this->delivered_height,
	  this->displayed_width, this->displayed_height,
	  this->output_width, this->output_height);
#endif


  /* calculate borders */
  if (this->output_height < this->gui_height) {
    /* top */
    this->border[0].x = 0;
    this->border[0].y = 0;
    this->border[0].w = this->gui_width;
    this->border[0].h = this->output_yoffset;
    /* bottom */
    this->border[1].x = 0;
    this->border[1].y = this->output_yoffset + this->output_height;
    this->border[1].w = this->gui_width;
    this->border[1].h = this->gui_height - this->border[1].y;
  } else {
    /* no top/bottom borders */
    this->border[0].w = this->border[0].h = 0;
    this->border[1].w = this->border[1].h = 0;
  }  
    
  if (this->output_width < this->gui_width) {
    /* left */
    this->border[2].x = 0;
    this->border[2].y = 0;
    this->border[2].w = this->output_xoffset;
    this->border[2].h = this->gui_height;
    /* right */
    this->border[3].x = this->output_xoffset + this->output_width;;
    this->border[3].y = 0;
    this->border[3].w = this->gui_width - this->border[3].x;
    this->border[3].h = this->gui_height;
  } else {
    /* no left/right borders */
    this->border[2].w = this->border[2].h = 0;
    this->border[3].w = this->border[3].h = 0;
  }
}

/*
 * return true if a redraw is needed due resizing, zooming,
 * aspect ratio changing, etc.
 */

int vo_scale_redraw_needed (vo_scale_t *this) {
  int gui_x, gui_y, gui_width, gui_height, gui_win_x, gui_win_y;
  double gui_pixel_aspect;
  int ret = 0;
  
  if( this->frame_output_cb ) {
    this->frame_output_cb (this->user_data,
			   this->delivered_width, this->delivered_height, 
			   this->video_pixel_aspect,
			   &gui_x, &gui_y, &gui_width, &gui_height,
			   &gui_pixel_aspect, &gui_win_x, &gui_win_y );
  } else {
    printf ("vo_scale: error! frame_output_cb must be set!\n");
  }

  if ( (gui_x != this->gui_x) || (gui_y != this->gui_y)
      || (gui_width != this->gui_width) || (gui_height != this->gui_height)
      || (gui_pixel_aspect != this->gui_pixel_aspect)
      || (gui_win_x != this->gui_win_x) || (gui_win_y != this->gui_win_y) ) {

    this->gui_x      = gui_x;
    this->gui_y      = gui_y;
    this->gui_width  = gui_width;
    this->gui_height = gui_height;
    this->gui_win_x  = gui_win_x;
    this->gui_win_y  = gui_win_y;
    this->gui_pixel_aspect = gui_pixel_aspect;

    ret = 1;
  }
  else
    ret = this->force_redraw;
  
  this->force_redraw = 0;
  return ret;
}

/*
 *
 */

void vo_scale_translate_gui2video(vo_scale_t *this,
				 int x, int y,
				 int *vid_x, int *vid_y) {

  if (this->output_width > 0 && this->output_height > 0) {
    /*
     * 1.
     * the driver may center a small output area inside a larger
     * gui area.  This is the case in fullscreen mode, where we often
     * have black borders on the top/bottom/left/right side.
     */
    x -= this->output_xoffset;
    y -= this->output_yoffset;

    /*
     * 2.
     * the driver scales the delivered area into an output area.
     * translate output area coordianates into the delivered area
     * coordiantes.
     */
    
    x = x * this->displayed_width  / this->output_width  + this->displayed_xoffset;
    y = y * this->displayed_height / this->output_height + this->displayed_yoffset;
  }

  *vid_x = x;
  *vid_y = y;
}

/*
 * Returns description of a given ratio code
 */

char *vo_scale_aspect_ratio_name(int a) {

  switch (a) {
  case XINE_VO_ASPECT_AUTO:
    return "auto";
  case XINE_VO_ASPECT_SQUARE:
    return "square";
  case XINE_VO_ASPECT_4_3:
    return "4:3";
  case XINE_VO_ASPECT_ANAMORPHIC:
    return "16:9";
  case XINE_VO_ASPECT_DVB:
    return "2:1";
  default:
    return "unknown";
  }
}


/*
 * config callbacks
 */
static void vo_scale_horizontal_pos_changed(void *data, xine_cfg_entry_t *entry) {
  vo_scale_t *this = (vo_scale_t *)data;
  
  this->output_horizontal_position = entry->num_value / 100.0;
  this->force_redraw = 1;
}

static void vo_scale_vertical_pos_changed(void *data, xine_cfg_entry_t *entry) {
  vo_scale_t *this = (vo_scale_t *)data;
  
  this->output_vertical_position = entry->num_value / 100.0;
  this->force_redraw = 1;
}


/* 
 * initialize rescaling struct
 */
 
void vo_scale_init(vo_scale_t *this, int support_zoom, int scaling_disabled,
                   config_values_t *config ) {
  
  memset( this, 0, sizeof(vo_scale_t) );
  this->support_zoom = support_zoom;
  this->scaling_disabled = scaling_disabled;
  this->force_redraw = 1;
  this->zoom_factor_x = 1.0;
  this->zoom_factor_y = 1.0;
  this->gui_pixel_aspect = 1.0;
  this->user_ratio = XINE_VO_ASPECT_AUTO;
  this->delivered_ratio = 0.0;
  
  this->output_horizontal_position = 
    config->register_range(config, "video.horizontal_position", 50, 0, 100,
      _("horizontal image position in the output window"), NULL, 10,
      vo_scale_horizontal_pos_changed, this) / 100.0;
  this->output_vertical_position =
    config->register_range(config, "video.vertical_position", 50, 0, 100,
      _("vertical image position in the output window"), NULL, 10,
      vo_scale_vertical_pos_changed, this) / 100.0;
}                  
