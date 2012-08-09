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
 * OSD stuff (text and graphic primitives)
 */

#define __OSD_C__
 
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <zlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

#ifndef __EMX__
#ifndef _MSC_VER 
#include <iconv.h>
#endif /* _MSC_VER */
#endif

#ifdef HAVE_LANGINFO_CODESET
#include <langinfo.h>
#endif

#include "xine_internal.h"
#include "video_out/alphablend.h"
#include "xine-engine/bswap.h"
#include "xineutils.h"
#include "video_out.h"
#include "osd.h"

#ifdef HAVE_FT2
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#ifdef __EMX__
#define _MSC_VER
#endif

/*
#define LOG_DEBUG 1
*/

#define BINARY_SEARCH 1

/* unicode value of alias character,
 * used if conversion fails
 */
#define ALIAS_CHARACTER_CONV '#'

/* unicode value of alias character,
 * used if character isn't in the font
 */
#define ALIAS_CHARACTER_FONT '_'

#ifdef MAX
#undef MAX
#endif
#define MAX(a,b) ( (a) > (b) ) ? (a) : (b)

#ifdef MIN
#undef MIN
#endif
#define MIN(a,b) ( (a) < (b) ) ? (a) : (b)

typedef struct osd_fontchar_s {
  uint16_t code;
  uint16_t width;
  uint16_t height;
  uint8_t *bmp;
} osd_fontchar_t;

struct osd_font_s {
  char             name[40];
  uint16_t         version;
  uint16_t         size;
  uint16_t         num_fontchars;
  osd_fontchar_t  *fontchar;
  osd_font_t      *next;
}; 

#ifdef HAVE_FT2
struct osd_ft2context_s {
  int        useme;
  FT_Library library;
  FT_Face    face;
  int        size;
};
#endif

/*
 * open a new osd object. this will allocated an empty (all zero) drawing
 * area where graphic primitives may be used.
 * It is ok to specify big width and height values. The render will keep
 * track of the smallest changed area to not generate too big overlays.
 * A default palette is initialized (i sugest keeping color 0 as transparent
 * for the sake of simplicity)
 */

static osd_object_t *osd_new_object (osd_renderer_t *this, int width, int height) {
     
  osd_object_t *osd;
  
  pthread_mutex_lock (&this->osd_mutex);  
  
  osd = xine_xmalloc( sizeof(osd_object_t) );
  osd->renderer = this;
  osd->next = this->osds;
  this->osds = osd;
  
  osd->width = width;
  osd->height = height;
  osd->area = xine_xmalloc( width * height );
  
  osd->x1 = width;
  osd->y1 = height;
  osd->x2 = 0;
  osd->y2 = 0;

  memcpy(osd->color, textpalettes_color[0], sizeof(textpalettes_color[0])); 
  memcpy(osd->trans, textpalettes_trans[0], sizeof(textpalettes_trans[0])); 

  osd->handle = -1;

#ifndef _MSC_VER
  osd->cd       = (iconv_t)-1;
  osd->encoding = NULL;
#endif
  
  pthread_mutex_unlock (&this->osd_mutex);  

#ifdef LOG_DEBUG  
  printf("osd_open %p [%dx%d]\n", osd, width, height);
#endif
  
  return osd;
}



/*
 * send the osd to be displayed at given pts (0=now)
 * the object is not changed. there may be subsequent drawing  on it.
 */
static int osd_show (osd_object_t *osd, int64_t vpts ) {
     
  osd_renderer_t *this = osd->renderer;
  rle_elem_t rle, *rle_p=0;
  int x, y, spare;
  uint8_t *c;

#ifdef LOG_DEBUG  
  printf("osd_show %p vpts=%lld\n", osd, vpts);
#endif
      
  if( osd->handle < 0 ) {
    if( (osd->handle = this->video_overlay->get_handle(this->video_overlay,0)) == -1 ) {
      return 0;
    }
  }
  
  pthread_mutex_lock (&this->osd_mutex);  
  
  /* check if osd is valid (something drawn on it) */
  if( osd->x2 >= osd->x1 ) {
 
    this->event.object.handle = osd->handle;

    if(osd->x1 > osd->width)
      osd->x1 = osd->width;
    if(osd->x2 > osd->width)
      osd->x2 = osd->width;
    if(osd->y1 > osd->height)
      osd->y1 = osd->height;
    if(osd->y2 > osd->height)
      osd->y2 = osd->height;
    
    memset( this->event.object.overlay, 0, sizeof(*this->event.object.overlay) );
    this->event.object.overlay->x = osd->display_x + osd->x1;
    this->event.object.overlay->y = osd->display_y + osd->y1;
    this->event.object.overlay->width = osd->x2 - osd->x1 + 1;
    this->event.object.overlay->height = osd->y2 - osd->y1 + 1;
 
    this->event.object.overlay->clip_top    = -1;
    this->event.object.overlay->clip_bottom = this->event.object.overlay->height +
                                              osd->display_y;
    this->event.object.overlay->clip_left   = 0;
    this->event.object.overlay->clip_right  = this->event.object.overlay->width +
                                              osd->display_x;
   
    spare = osd->y2 - osd->y1;
    this->event.object.overlay->num_rle = 0;
    this->event.object.overlay->data_size = 1024;
    rle_p = this->event.object.overlay->rle = 
       malloc(this->event.object.overlay->data_size * sizeof(rle_elem_t) );
    
    for( y = osd->y1; y <= osd->y2; y++ ) {
      rle.len = 0;
      rle.color = 0;
      c = osd->area + y * osd->width + osd->x1;                                       
      for( x = osd->x1; x <= osd->x2; x++, c++ ) {
        if( rle.color != *c ) {
          if( rle.len ) {
            if( (this->event.object.overlay->num_rle + spare) > 
                this->event.object.overlay->data_size ) {
                this->event.object.overlay->data_size += 1024;
                rle_p = this->event.object.overlay->rle = 
                  realloc( this->event.object.overlay->rle,
                           this->event.object.overlay->data_size * sizeof(rle_elem_t) );
                rle_p += this->event.object.overlay->num_rle;
            }
            *rle_p++ = rle;
            this->event.object.overlay->num_rle++;            
          }
          rle.color = *c;
          rle.len = 1;
        } else {
          rle.len++;
        }  
      }
      *rle_p++ = rle;
      this->event.object.overlay->num_rle++;            
    }
  
#ifdef LOG_DEBUG  
    printf("osd_show num_rle = %d\n", this->event.object.overlay->num_rle);
#endif
  
    memcpy(this->event.object.overlay->clip_color, osd->color, sizeof(osd->color)); 
    memcpy(this->event.object.overlay->clip_trans, osd->trans, sizeof(osd->trans)); 
    memcpy(this->event.object.overlay->color, osd->color, sizeof(osd->color)); 
    memcpy(this->event.object.overlay->trans, osd->trans, sizeof(osd->trans)); 
  
    this->event.event_type = OVERLAY_EVENT_SHOW;
    this->event.vpts = vpts;
    this->video_overlay->add_event(this->video_overlay,(void *)&this->event);
  }
  pthread_mutex_unlock (&this->osd_mutex);  
  
  return 1;
}

/*
 * send event to hide osd at given pts (0=now)
 * the object is not changed. there may be subsequent drawing  on it.
 */
static int osd_hide (osd_object_t *osd, int64_t vpts) {     

  osd_renderer_t *this = osd->renderer;
  
#ifdef LOG_DEBUG  
  printf("osd_hide %p vpts=%lld\n",osd, vpts);
#endif
    
  if( osd->handle < 0 )
    return 0;
      
  pthread_mutex_lock (&this->osd_mutex);  
  
  this->event.object.handle = osd->handle;
  
  /* not really needed this, but good pratice to clean it up */
  memset( this->event.object.overlay, 0, sizeof(this->event.object.overlay) );
   
  this->event.event_type = OVERLAY_EVENT_HIDE;
  this->event.vpts = vpts;
  this->video_overlay->add_event(this->video_overlay,(void *)&this->event);

  pthread_mutex_unlock (&this->osd_mutex);  
  
  return 1;
}


/*
 * clear an osd object, so that it can be used for rendering a new image
 */

static void osd_clear (osd_object_t *osd) {
#ifdef LOG_DEBUG
  printf("osd_clear\n");
#endif

  memset(osd->area, 0, osd->width * osd->height);
  osd->x1 = osd->width;
  osd->y1 = osd->height;
  osd->x2 = 0;
  osd->y2 = 0;
}

/*
 * Draw a point.
 */

static void osd_point (osd_object_t *osd, int x, int y, int color) {
  uint8_t *c;
  
#ifdef LOG_DEBUG
  printf("osd_point %p (%d x %d)\n", osd, x, y);
#endif
  
  /* update clipping area */
  osd->x1 = MIN(osd->x1, x);
  osd->x2 = MAX(osd->x2, (x + 1));
  osd->y1 = MIN(osd->y1, y);
  osd->y2 = MAX(osd->y2, (y + 1));

  c = osd->area + y * osd->width + x;
  if(c <= (osd->area + (osd->width * osd->height)))
    *c = color;
}

/*
 * Bresenham line implementation on osd object
 */

static void osd_line (osd_object_t *osd,
		      int x1, int y1, int x2, int y2, int color) {
     
  uint8_t *c;
  int dx, dy, t, inc, d, inc1, inc2;

#ifdef LOG_DEBUG  
  printf("osd_line %p (%d,%d)-(%d,%d)\n",osd, x1,y1, x2,y2 );
#endif
     
  /* update clipping area */
  t = MIN( x1, x2 );
  osd->x1 = MIN( osd->x1, t );
  t = MAX( x1, x2 );
  osd->x2 = MAX( osd->x2, t );
  t = MIN( y1, y2 );
  osd->y1 = MIN( osd->y1, t );
  t = MAX( y1, y2 );
  osd->y2 = MAX( osd->y2, t );

  dx = abs(x1-x2);
  dy = abs(y1-y2);

  if( dx>=dy ) {
    if( x1>x2 )
    {
      t = x2; x2 = x1; x1 = t;
      t = y2; y2 = y1; y1 = t;
    }
  
    if( y2 > y1 ) inc = 1; else inc = -1;

    inc1 = 2*dy;
    d = inc1 - dx;
    inc2 = 2*(dy-dx);

    c = osd->area + y1 * osd->width + x1;
    
    while(x1<x2)
    {

      if(c <= (osd->area + (osd->width * osd->height)))
	*c = color;

      c++;
      
      x1++;
      if( d<0 ) {
        d+=inc1;
      } else {
        y1+=inc;
        d+=inc2;
        c = osd->area + y1 * osd->width + x1;
      }
    }
  } else {
    if( y1>y2 ) {
      t = x2; x2 = x1; x1 = t;
      t = y2; y2 = y1; y1 = t;
    }

    if( x2 > x1 ) inc = 1; else inc = -1;

    inc1 = 2*dx;
    d = inc1-dy;
    inc2 = 2*(dx-dy);

    c = osd->area + y1 * osd->width + x1;

    while(y1<y2) {

      if(c <= (osd->area + (osd->width * osd->height)))
	*c = color;
      
      c += osd->width;
      y1++;
      if( d<0 ) {
	d+=inc1;
      } else {
	x1+=inc;
	d+=inc2;
	c = osd->area + y1 * osd->width + x1;
      }
    }
  }
}


/*
 * filled retangle
 */

static void osd_filled_rect (osd_object_t *osd,
			     int x1, int y1, int x2, int y2, int color) {

  int x, y, dx, dy;

#ifdef LOG_DEBUG  
  printf("osd_filled_rect %p (%d,%d)-(%d,%d)\n",osd, x1,y1, x2,y2 );
#endif

  /* update clipping area */
  x = MIN( x1, x2 );
  osd->x1 = MIN( osd->x1, x );
  dx = MAX( x1, x2 );
  osd->x2 = MAX( osd->x2, dx );
  y = MIN( y1, y2 );
  osd->y1 = MIN( osd->y1, y );
  dy = MAX( y1, y2 );
  osd->y2 = MAX( osd->y2, dy );

  dx -= x;
  dy -= y;

  for( ; dy--; y++ ) {
    memset(osd->area + y * osd->width + x,color,dx);
  }
}

/*
 * set palette (color and transparency)
 */

static void osd_set_palette(osd_object_t *osd, const uint32_t *color, const uint8_t *trans ) {

  memcpy(osd->color, color, sizeof(osd->color));
  memcpy(osd->trans, trans, sizeof(osd->trans));
}

/*
 * set on existing text palette 
 * (-1 to set user specified palette)
 */

static void osd_set_text_palette(osd_object_t *osd, int palette_number,
				 int color_base) {

  if( palette_number < 0 )
    palette_number = osd->renderer->textpalette;

  /* some sanity checks for the color indices */
  if( color_base < 0 )
    color_base = 0;
  else if( color_base > OVL_PALETTE_SIZE - TEXT_PALETTE_SIZE )
    color_base = OVL_PALETTE_SIZE - TEXT_PALETTE_SIZE;

  memcpy(&osd->color[color_base], textpalettes_color[palette_number],
	 sizeof(textpalettes_color[palette_number]));
  memcpy(&osd->trans[color_base], textpalettes_trans[palette_number],
	 sizeof(textpalettes_trans[palette_number]));    
}


/*
 * get palette (color and transparency)
 */

static void osd_get_palette (osd_object_t *osd, uint32_t *color, uint8_t *trans) {

  memcpy(color, osd->color, sizeof(osd->color));
  memcpy(trans, osd->trans, sizeof(osd->trans));
}

/*
 * set position were overlay will be blended
 */

static void osd_set_position (osd_object_t *osd, int x, int y) {

  if( x < 0 || x > 0x10000 )
    x = 0;
  if( y < 0 || y > 0x10000 )
    y = 0;
  osd->display_x = x;
  osd->display_y = y;
}

static uint16_t gzread_i16(gzFile *fp) {
  uint16_t ret;
  ret = gzgetc(fp);
  ret |= (gzgetc(fp)<<8);
  return ret;
}

/*
   load bitmap font into osd engine 
*/

static int osd_renderer_load_font(osd_renderer_t *this, char *filename) {

  gzFile      *fp;
  osd_font_t  *font = NULL;
  int          i, ret = 0;
  
#ifdef LOG_DEBUG  
  printf("osd: renderer_load_font %p name=%s\n", this, filename );
#endif

  pthread_mutex_lock (&this->osd_mutex);

  /* load quick & dirt font format */
  /* fixme: check read errors... */
  if( (fp = gzopen(filename,"rb")) != NULL ) {

    font = xine_xmalloc( sizeof(osd_font_t) );

    gzread(fp, font->name, sizeof(font->name) );
    font->version = gzread_i16(fp);
    font->size = gzread_i16(fp);
    font->num_fontchars = gzread_i16(fp);

    font->fontchar = malloc( sizeof(osd_fontchar_t) * font->num_fontchars );

#ifdef LOG_DEBUG  
    printf("osd: font %s %d\n", font->name, font->num_fontchars);
#endif
    for( i = 0; i < font->num_fontchars; i++ ) {
      font->fontchar[i].code = gzread_i16(fp);
      font->fontchar[i].width = gzread_i16(fp);
      font->fontchar[i].height = gzread_i16(fp);
      font->fontchar[i].bmp = malloc(font->fontchar[i].width*font->fontchar[i].height);
      if( gzread(fp, font->fontchar[i].bmp, 
            font->fontchar[i].width*font->fontchar[i].height) <= 0 )
        break;
    }
    
    if( i == font->num_fontchars ) {
      ret = 1;

#ifdef LOG_DEBUG  
    printf("osd: font %s loading ok\n",font->name);
#endif

      font->next = this->fonts;
      this->fonts = font;
    } else {

#ifdef LOG_DEBUG  
      printf("osd: font %s loading failed (%d < %d)\n",font->name,
	     i, font->num_fontchars);
#endif

      while( --i >= 0 ) {
        free(font->fontchar[i].bmp);
      }
      free(font->fontchar);
      free(font);
    }

    gzclose(fp);
  }

  pthread_mutex_unlock (&this->osd_mutex);
  return ret;
}

/*
 * unload font
 */
static int osd_renderer_unload_font(osd_renderer_t *this, char *fontname ) {

  osd_font_t *font, *last;
  osd_object_t *osd;
  int i, ret = 0;
  
#ifdef LOG_DEBUG  
  printf("osd_renderer_unload_font %p name=%s\n", this, fontname);
#endif

  pthread_mutex_lock (&this->osd_mutex);

  osd = this->osds;
  while( osd ) {  
    if( !strcmp(osd->font->name, fontname) )
      osd->font = NULL;
    osd = osd->next;
  }

  last = NULL;
  font = this->fonts;
  while( font ) {
    if ( !strcmp(font->name,fontname) ) {

      for( i = 0; i < font->num_fontchars; i++ ) {
        free( font->fontchar[i].bmp );
      }
      free( font->fontchar );

      if( last )
        last->next = font->next;
      else
        this->fonts = font->next;
      free( font );
      ret = 1;
      break;
    }
    last = font;
    font = font->next;
  }

  pthread_mutex_unlock (&this->osd_mutex);
  return ret;
}


/*
  set the font of osd object
*/

static int osd_set_font( osd_object_t *osd, const char *fontname, int size) { 

  osd_renderer_t *this = osd->renderer;
  osd_font_t *font;
  int best = 0;
  int ret = 0;
#ifdef HAVE_FT2
  char pathname[1024];
  int error_flag = 0;
#endif

#ifdef LOG_DEBUG  
  printf("osd_set_font %p name=%s\n", osd, fontname);
#endif
 
  pthread_mutex_lock (&this->osd_mutex);

  osd->font = NULL;

  font = this->fonts;
  while( font ) {

    if( !strcmp(font->name, fontname) && (size>=font->size) 
	&& (best<font->size)) {
      ret = 1;
      osd->font = font;
      best = font->size;
#ifdef LOG_DEBUG  
      printf ("osd_set_font: font->name=%s, size=%d\n", font->name, font->size);
#endif

    }
    font = font->next;
  }
  
#ifdef HAVE_FT2

  if (osd->ft2) {
    osd->ft2->useme = 0;
  }

  if (!ret) { /* trying to load a font file with ft2 */
    if (!osd->ft2) {
      osd->ft2 = xine_xmalloc(sizeof(osd_ft2context_t));
      if(FT_Init_FreeType( &osd->ft2->library )) {
        printf("osd: cannot initialize ft2 library\n");
	free(osd->ft2);
	osd->ft2 = NULL;
      }
    }
    if (osd->ft2) {
      /* try load font from current directory */
      if (FT_New_Face(osd->ft2->library, fontname, 0, &osd->ft2->face)) {
        /* try load font from home directory */
        sprintf(pathname, "%s/.xine/fonts/%s", xine_get_homedir(), fontname);
        if (FT_New_Face(osd->ft2->library, pathname, 0, &osd->ft2->face)) {
          /* try load font from xine font directory */
          sprintf(pathname, "%s/%s", XINE_FONTDIR, fontname);
          if (FT_New_Face(osd->ft2->library, pathname, 0, &osd->ft2->face)) {
            error_flag = 1;
	    printf("osd: error loading font %s with ft2\n", fontname);
	  }
        }
      }
      if (!error_flag) {
	if (FT_Set_Pixel_Sizes(osd->ft2->face, 0, size)) {
	  printf("osd: error setting font size (no scalable font?)\n");
	} else {
	  ret = 1;
	  osd->ft2->useme = 1;
	  osd->ft2->size = size;
	}
      }
    }	
  }

#endif

  pthread_mutex_unlock (&this->osd_mutex);
  return ret;
}


/*
 * search the character in the sorted array,
 *
 * returns ALIAS_CHARACTER_FONT if character 'code' isn't found,
 * returns 'n' on error
 */
static int osd_search(osd_fontchar_t *array, size_t n, uint16_t code) {
#ifdef BINARY_SEARCH
  size_t i, left, right;

  if (!n) return 0;

  left = 0;
  right = n - 1;
  while (left < right) {
    i = (left + right) >> 1;
    if (code <= array[i].code) right = i;
    else left = i + 1;
  }

  if (array[right].code == code)
    return right;
  else 
    return ALIAS_CHARACTER_FONT < n ? ALIAS_CHARACTER_FONT : n;
#else
  size_t i;
  
  for( i = 0; i < n; i++ ) {
    if( font->fontchar[i].code == unicode )
      break;
  }

  if (i < n) 
    return i;
  else 
    return ALIAS_CHARACTER_FONT < n ? ALIAS_CHARACTER_FONT : n;
#endif
}


#ifndef _MSC_VER
/* 
 * get next unicode value 
 */
static uint16_t osd_iconv_getunicode(iconv_t *cd, const char *encoding, char **inbuf, size_t *inbytesleft) {
  uint16_t unicode;
  char *outbuf = (char*)&unicode;
  size_t outbytesleft = 2;
  size_t count;

  if (cd != (iconv_t)-1) {
    /* get unicode value from iconv */
    count = iconv(cd, inbuf, inbytesleft, &outbuf, &outbytesleft);
    if (count == (size_t)-1 && errno != E2BIG) {
      /* unknown character or character wider than 16 bits, try skip one byte */
      printf(_("osd: unknown sequence starting with byte 0x%02X"
             " in encoding \"%s\", skipping\n"), (*inbuf)[0] & 0xFF, encoding);
      if (*inbytesleft) {
        (*inbytesleft)--;
        (*inbuf)++;
      }
      return ALIAS_CHARACTER_CONV;
    }
  } else {
    /* direct mapping without iconv */
    unicode = (*inbuf)[0];
    (*inbuf)++;
    (*inbytesleft)--;
  }

  return unicode;
}
#endif


/*
 * free iconv encoding
 */
static void osd_free_encoding(osd_object_t *osd) {
#ifndef _MSC_VER
  if (osd->cd != (iconv_t)-1) {
    iconv_close(osd->cd);
    osd->cd = (iconv_t)-1;
  }
  if (osd->encoding) {
    free(osd->encoding);
    osd->encoding = NULL;
  }
#endif
}


/*
 * set encoding of text 
 *
 * NULL ... no conversion (iso-8859-1)
 * ""   ... locale encoding
 */
static int osd_set_encoding (osd_object_t *osd, const char *encoding) {
#ifndef _MSC_VER
  osd_free_encoding(osd);

  if (!encoding) return 1;
  if (!encoding[0]) {
#ifdef HAVE_LANGINFO_CODESET
    if ((encoding = nl_langinfo(CODESET)) == NULL) {
      printf(_("osd: can't find out current locale character set\n"));
      return 0;
    }
#else
    return 0;
#endif
  }

  /* prepare conversion to UCS-2 */
  if ((osd->cd = iconv_open("UCS-2", encoding)) == (iconv_t)-1) {
    printf(_("osd: unsupported conversion %s -> UCS-2, "
             "no conversion performed\n"), encoding);
    return 0;
  }

  osd->encoding = strdup(encoding);  
  return 1;
#else
  return encoding == NULL;
#endif /* _MSC_VER */
}


/*
 * render text in current encoding on x,y position
 *  no \n yet
 */
static int osd_render_text (osd_object_t *osd, int x1, int y1,
                            const char *text, int color_base) {

  osd_renderer_t *this = osd->renderer;
  osd_font_t *font;
  int i, y;
  uint8_t *dst, *src;
  const char *inbuf;
  uint16_t unicode;
  size_t inbytesleft;

#ifdef LOG_DEBUG  
  printf("osd_render_text %p (%d,%d) \"%s\"\n", osd, x1, y1, text);
#endif
 
  /* some sanity checks for the color indices */
  if( color_base < 0 )
    color_base = 0;
  else if( color_base > OVL_PALETTE_SIZE - TEXT_PALETTE_SIZE )
    color_base = OVL_PALETTE_SIZE - TEXT_PALETTE_SIZE;

  pthread_mutex_lock (&this->osd_mutex);

  {
    int proceed = 0;

    if ((font = osd->font)) proceed = 1;
#ifdef HAVE_FT2
    if (osd->ft2 && osd->ft2->useme) proceed = 1;
#endif
    
    if (proceed == 0) {
      printf(_("osd: font isn't defined\n"));
      pthread_mutex_unlock(&this->osd_mutex);
      return 0;
    }
  }

  if( x1 < osd->x1 ) osd->x1 = x1;
  if( y1 < osd->y1 ) osd->y1 = y1;

  inbuf = text;
  inbytesleft = strlen(text);
  
  while( inbytesleft ) {
#ifndef _MSC_VER
    unicode = osd_iconv_getunicode(osd->cd, osd->encoding, 
                                   (char **)&inbuf, &inbytesleft);
#else
    unicode = inbuf[0];
    inbuf++;
    inbytesleft--;
#endif

#ifdef HAVE_FT2
    if (osd->ft2 && osd->ft2->useme) {
      i = FT_Get_Char_Index( osd->ft2->face, unicode );
    } else {
#endif
    
    i = osd_search(font->fontchar, font->num_fontchars, unicode);

#ifdef LOG_DEBUG  
    printf("font %s [%d, U+%04X == U+%04X] %dx%d -> %d,%d\n", font->name, i, 
           unicode, font->fontchar[i].code, font->fontchar[i].width, 
           font->fontchar[i].height, x1, y1);
#endif

#ifdef HAVE_FT2
    } /* !(osd->ft2 && osd->ft2->useme) */
#endif

#ifdef HAVE_FT2
    if (osd->ft2 && osd->ft2->useme) {
      int gheight, gwidth;
      FT_GlyphSlot  slot = osd->ft2->face->glyph;
      
      if (FT_Load_Glyph(osd->ft2->face, i, FT_LOAD_DEFAULT)) {
        printf("osd: error loading glyph\n");
	continue;
      }

      if (slot->format != ft_glyph_format_bitmap) {
	if (FT_Render_Glyph(osd->ft2->face->glyph, ft_render_mode_normal))
	  printf("osd: error in rendering glyph\n");
      }

      dst = osd->area + y1 * osd->width + x1;
      src = (uint8_t*) slot->bitmap.buffer;
      gheight = slot->bitmap.rows;
      gwidth  = slot->bitmap.width;

      for( y = 0; y < gheight; y++ ) {
        uint8_t *s = src;
	uint8_t *d = dst 
	  - slot->bitmap_top * osd->width
	  + slot->bitmap_left;

	while (s < src + gwidth) {
	  if(d <= (osd->area + (osd->width * osd->height)))
	    *d = (uint8_t)(*s/26+1) + (uint8_t) color_base;
	  
	  d++;
	  s++;
	}
        src += slot->bitmap.pitch;
        dst += osd->width;
      }
      x1 += slot->advance.x >> 6;
      if( x1 > osd->x2 ) osd->x2 = x1;
      if( y1 > osd->y2 ) osd->y2 = y1;

    } else {
#endif

    if ( i != font->num_fontchars ) {
      dst = osd->area + y1 * osd->width + x1;
      src = font->fontchar[i].bmp;
      
      for( y = 0; y < font->fontchar[i].height; y++ ) {
	int width = font->fontchar[i].width;
	uint8_t *s = src, *d = dst;

	while (s < src + width) {
	  if(d <= (osd->area + (osd->width * osd->height)))
	    *d = *s + (uint8_t) color_base;
	  
	  d++;
	  s++;
	}
        src += font->fontchar[i].width;
        dst += osd->width;
      }
      x1 += font->fontchar[i].width;
    
      if( x1 > osd->x2 ) osd->x2 = x1;
      if( y1 + font->fontchar[i].height > osd->y2 ) 
        osd->y2 = y1 + font->fontchar[i].height;
    }
    
#ifdef HAVE_FT2
    } /* !(osd->ft2 && osd->ft2->useme) */
#endif

  }

  pthread_mutex_unlock (&this->osd_mutex);

  return 1;
}

/*
  get width and height of how text will be renderized
*/
static int osd_get_text_size(osd_object_t *osd, const char *text, int *width, int *height) {

  osd_renderer_t *this = osd->renderer;
  osd_font_t *font;
  int i;
  const char *inbuf;
  uint16_t unicode;
  size_t inbytesleft;

#ifdef LOG_DEBUG  
  printf("osd_get_text_size %p \"%s\"\n", osd, text);
#endif
  
  pthread_mutex_lock (&this->osd_mutex);

  {
    int proceed = 0;

    if ((font = osd->font)) proceed = 1;
#ifdef HAVE_FT2
    if (osd->ft2 && osd->ft2->useme) proceed = 1;
#endif
    
    if (proceed == 0) {
      printf(_("osd: font isn't defined\n"));
      pthread_mutex_unlock(&this->osd_mutex);
      return 0;
    }
  }

  *width = 0;
  *height = 0;

  inbuf = text;
  inbytesleft = strlen(text);
  
  while( inbytesleft ) {
#ifndef _MSC_VER
    unicode = osd_iconv_getunicode(osd->cd, osd->encoding, 
                                   (char **)&inbuf, &inbytesleft);
#else
    unicode = inbuf[0];
    inbuf++;
    inbytesleft--;
#endif

#ifdef HAVE_FT2
    if (osd->ft2 && osd->ft2->useme) {
      int first = 1;
      FT_GlyphSlot  slot = osd->ft2->face->glyph;

      i = FT_Get_Char_Index( osd->ft2->face, unicode);

      if (FT_Load_Glyph(osd->ft2->face, i, FT_LOAD_DEFAULT)) {
        printf("osd: error loading glyph %i\n", i);
        text++;
        continue;
      }

      if (slot->format != ft_glyph_format_bitmap) {
        if (FT_Render_Glyph(osd->ft2->face->glyph, ft_render_mode_normal))
	  printf("osd: error in rendering\n");
      }
      if (first) *width += slot->bitmap_left;
      first = 0;
      *width += slot->advance.x >> 6;
      /* font height from baseline to top */
      *height = MAX(*height, slot->bitmap_top);
      text++;
    } else {
#endif
      i = osd_search(font->fontchar, font->num_fontchars, unicode);

      if ( i != font->num_fontchars ) {
        if( font->fontchar[i].height > *height )
          *height = font->fontchar[i].height;
        *width += font->fontchar[i].width;
      }
#ifdef HAVE_FT2
    } /* !(osd->ft2 && osd->ft2->useme) */
#endif
  }

  pthread_mutex_unlock (&this->osd_mutex);

  return 1;
}

static void osd_load_fonts (osd_renderer_t *this, char *path) {
  DIR *dir;
  char pathname [1024];

#ifdef LOG_DEBUG
  printf ("osd: load_fonts, path=%s\n", path);
#endif

  dir = opendir (path) ;

  if (dir) {

    struct dirent *entry;

#ifdef LOG_DEBUG
    printf ("osd: load_fonts, %s opened\n", path);
#endif

    while ((entry = readdir (dir)) != NULL) {
      int len;

      len = strlen (entry->d_name);

      if ( (len>12) && !strncmp (&entry->d_name[len-12], ".xinefont.gz", 12)) {
	
#ifdef LOG_DEBUG
	printf ("osd: trying to load font >%s< (ending >%s<)\n",
		entry->d_name,&entry->d_name[len-12]);
#endif

	sprintf (pathname, "%s/%s", path, entry->d_name);
	
	osd_renderer_load_font (this, pathname);

      }
    }

    closedir (dir);

  }
}

/*
 * free osd object
 */

static void osd_free_object (osd_object_t *osd_to_close) {
     
  osd_renderer_t *this = osd_to_close->renderer;
  osd_object_t *osd, *last;

  if( osd_to_close->handle >= 0 ) {
    osd_hide(osd_to_close,0);
    
    this->event.object.handle = osd_to_close->handle;
  
    /* not really needed this, but good pratice to clean it up */
    memset( this->event.object.overlay, 0, sizeof(this->event.object.overlay) );
    this->event.event_type = OVERLAY_EVENT_FREE_HANDLE;
    this->event.vpts = 0;
    this->video_overlay->add_event(this->video_overlay,(void *)&this->event);
  
    osd_to_close->handle = -1; /* handle will be freed */
  }
  
  pthread_mutex_lock (&this->osd_mutex);  

  last = NULL;
  osd = this->osds;
  while( osd ) {
    if ( osd == osd_to_close ) {
      free( osd->area );
      if( osd->ft2 ) free( osd->ft2 );
      osd_free_encoding(osd);
      
      if( last )
        last->next = osd->next;
      else
        this->osds = osd->next;

      free( osd );
      break;
    }
    last = osd;
    osd = osd->next;
  }
  pthread_mutex_unlock (&this->osd_mutex);  
}

static void osd_renderer_close (osd_renderer_t *this) {

  while( this->osds )
    osd_free_object ( this->osds );
  
  while( this->fonts )
    osd_renderer_unload_font( this, this->fonts->name );

  pthread_mutex_destroy (&this->osd_mutex);

  free(this->event.object.overlay);
  free(this);
}


static void update_text_palette(void *this_gen, xine_cfg_entry_t *entry)
{
  osd_renderer_t *this = (osd_renderer_t *)this_gen;

  this->textpalette = entry->num_value;
  printf("osd: text palette will be %s\n", textpalettes_str[this->textpalette] );
}

static void osd_draw_bitmap(osd_object_t *osd, uint8_t *bitmap,
			    int x1, int y1, int width, int height,
			    uint8_t *palette_map)
{
  int y, x;

#ifdef LOG_DEBUG  
  printf("osd_draw_bitmap %p at (%d,%d) %dx%d\n",osd, x1,y1, width,height );
#endif

  /* update clipping area */
  osd->x1 = MIN( osd->x1, x1 );
  osd->x2 = MAX( osd->x2, x1+width-1 );
  osd->y1 = MIN( osd->y1, y1 );
  osd->y2 = MAX( osd->y2, y1+height-1 );

  for( y=0; y<height; y++ ) {
    if ( palette_map ) {
      int src_offset = y * width;
      int dst_offset = (y1+y) * osd->width + x1;
      /* Slow copy with palette translation, the map describes how to
         convert color indexes in the source bitmap to indexes in the
         osd palette */
      for ( x=0; x<width; x++ ) {
	osd->area[dst_offset+x] = palette_map[bitmap[src_offset+x]];
      }
    } else {
      /* Fast copy with direct mapping */
      memcpy(osd->area + (y1+y) * osd->width + x1, bitmap + y * width, width);
    }
  }
}

/*
 * initialize the osd rendering engine
 */

osd_renderer_t *osd_renderer_init( video_overlay_manager_t *video_overlay, config_values_t *config ) {

  osd_renderer_t *this;
  char str[1024];

  this = xine_xmalloc(sizeof(osd_renderer_t)); 
  this->video_overlay = video_overlay;
  this->config = config;
  this->event.object.overlay = xine_xmalloc( sizeof(vo_overlay_t) );

  pthread_mutex_init (&this->osd_mutex, NULL);

#ifdef LOG_DEBUG  
  printf("osd: osd_renderer_init %p\n", this);
#endif
  
  /*
   * load available fonts
   */

  osd_load_fonts (this, XINE_FONTDIR);

  sprintf (str, "%s/.xine/fonts", xine_get_homedir ());

  osd_load_fonts (this, str);

  this->textpalette = config->register_enum (config, "misc.osd_text_palette", 0,
                                             textpalettes_str, 
                                             _("Palette (foreground-border-background) to use on subtitles"),
                                             NULL, 10, update_text_palette, this);
  
  /*
   * set up function pointer
   */

  this->new_object         = osd_new_object;
  this->free_object        = osd_free_object;
  this->show               = osd_show;
  this->hide               = osd_hide;
  this->set_palette        = osd_set_palette;
  this->set_text_palette   = osd_set_text_palette;
  this->get_palette        = osd_get_palette;
  this->set_position       = osd_set_position;
  this->set_font           = osd_set_font;
  this->clear              = osd_clear;
  this->point              = osd_point;
  this->line               = osd_line;
  this->filled_rect        = osd_filled_rect;
  this->set_encoding       = osd_set_encoding;
  this->render_text        = osd_render_text;
  this->get_text_size      = osd_get_text_size;
  this->close              = osd_renderer_close;
  this->draw_bitmap        = osd_draw_bitmap;

  return this;
}
