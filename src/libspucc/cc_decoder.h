/*
 * Copyright (C) 2000-2002 the xine project
 * 
 * Copyright (C) Christian Vogler 
 *               cvogler@gradient.cis.upenn.edu - December 2001
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
 * $Id: cc_decoder.h,v 1.5 2002/03/31 23:14:48 cvogler Exp $
 *
 * stuff needed to provide closed captioning decoding and display
 *
 * Some small bits and pieces of the EIA-608 captioning decoder were
 * adapted from CCDecoder 0.9.1 by Mike Baker. The latest version is
 * available at http://sourceforge.net/projects/ccdecoder/.
 */

typedef struct cc_decoder_s cc_decoder_t;
typedef struct cc_renderer_s cc_renderer_t;

#define NUM_CC_PALETTES 2
extern char *cc_schemes[NUM_CC_PALETTES + 1];

#define CC_FONT_MAX 256

typedef struct cc_config_s {
  int cc_enabled;             /* true if closed captions are enabled */
  char font[CC_FONT_MAX];     /* standard captioning font & size */
  int font_size;
  char italic_font[CC_FONT_MAX];   /* italic captioning font & size */
  int center;                 /* true if captions should be centered */
                              /* according to text width */
  int cc_scheme;              /* which captioning scheme to use */

  /* the following variables are not controlled by configuration files; they */
  /* are intrinsic to the properties of the configuration options and the */
  /* currently played video */
  int can_cc;                 /* true if captions can be displayed */
                              /* (e.g., font fits on screen) */

  cc_renderer_t *renderer;    /* closed captioning renderer */
} cc_config_t;

cc_decoder_t *cc_decoder_open(cc_config_t *cc_cfg);
void cc_decoder_close(cc_decoder_t *this_obj);
void cc_decoder_init(void);

void decode_cc(cc_decoder_t *this, uint8_t *buffer, uint32_t buf_len,
	       int64_t pts);

/* Instantiates a new closed captioning renderer. */
cc_renderer_t *cc_renderer_open(osd_renderer_t *osd_renderer,
				metronom_t *metronom, cc_config_t *cc_cfg,
				int video_width, int video_height);

/* Destroys a closed captioning renderer. */
void cc_renderer_close(cc_renderer_t *this_obj);

/* Updates the renderer configuration variables */
void cc_renderer_update_cfg(cc_renderer_t *this_obj, int video_width,
			    int video_height);

