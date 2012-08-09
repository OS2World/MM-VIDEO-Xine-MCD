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
 * input plugin for http network streams
 *
 * $Id: input_http.c,v 1.65 2003/09/25 13:47:17 f1rmb Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifndef WIN32
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#endif /* WIN32 */

#include <sys/time.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "input_plugin.h"
#include "net_buf_ctrl.h"
#include "io_helper.h"

/*
#define LOG
*/

#define BUFSIZE                 1024

#define DEFAULT_HTTP_PORT         80

typedef struct {
  input_plugin_t   input_plugin;

  xine_stream_t   *stream;
  
  int              fh;
  char            *mrl;

  nbc_t           *nbc; 

  off_t            curpos;
  off_t            contentlength;
    
  char             buf[BUFSIZE];
  char             mrlbuf[BUFSIZE];
  char             mrlbuf2[BUFSIZE]; /* icecast */
  char             proxybuf[BUFSIZE];

  char             auth[BUFSIZE];
  char             proxyauth[BUFSIZE];
  
  char            *user;
  char            *password;
  char            *host;
  int              port;
  char            *filename;
  
  char             preview[MAX_PREVIEW_SIZE];
  off_t            preview_size;
  
  /* ShoutCast */
  int              shoutcast_mode;
  int              shoutcast_metaint;
  off_t            shoutcast_pos;
  char            *shoutcast_songtitle;

  /* scratch buffer for forward seeking */

  char             seek_buf[BUFSIZE];

  
} http_input_plugin_t;

typedef struct {

  input_class_t     input_class;

  xine_t           *xine;
  config_values_t  *config;

  char             *proxyuser;
  char             *proxypassword;
  char             *proxyhost;
  int               proxyport;
 
} http_input_class_t;

static void proxy_user_change_cb(void *data, xine_cfg_entry_t *cfg) {
  http_input_class_t *this = (http_input_class_t *) data;

  this->proxyuser = cfg->str_value;
}

static void proxy_password_change_cb(void *data, xine_cfg_entry_t *cfg) {
  http_input_class_t *this = (http_input_class_t *) data;

  this->proxypassword = cfg->str_value;
}

static void proxy_host_change_cb(void *data, xine_cfg_entry_t *cfg) {
  http_input_class_t *this = (http_input_class_t *) data;

  this->proxyhost = cfg->str_value;
}

static void proxy_port_change_cb(void *data, xine_cfg_entry_t *cfg) {
  http_input_class_t *this = (http_input_class_t *) data;

  this->proxyport = cfg->num_value;
}

static int http_plugin_parse_url (char *urlbuf, char **user, char **password,
    char** host, int *port, char **filename) {
  char   *start = NULL;
  char   *authcolon = NULL;
  char	 *at = NULL;
  char	 *portcolon = NULL;
  char   *slash = NULL;
  
  if (user != NULL)
    *user = NULL;
  
  if (password != NULL)
    *password = NULL;
  
  if (host != NULL)
    *host = NULL;
  
  if (filename != NULL)
    *filename = NULL;
  
  if (port != NULL)
    *port = 0;
  
  start = strstr(urlbuf, "://");
  if (start != NULL)
    start += 3;
  else
    start = urlbuf;

  at = strchr(start, '@');
  slash = strchr(start, '/');
  
  if (at != NULL && slash != NULL && at > slash)
    at = NULL;
  
  if (at != NULL)
  {
    authcolon = strchr(start, ':');
    if(authcolon != NULL && authcolon > at)
      authcolon = NULL;
    
    portcolon = strchr(at, ':');
  } else
    portcolon = strchr(start, ':');
  
  if (portcolon != NULL && slash != NULL && portcolon > slash)
    portcolon = NULL;
  
  if (at != NULL)
  {
    *at = '\0';
    
    if (user != NULL)
      *user = start;
    
    if (authcolon != NULL)
    {
      *authcolon = '\0';
      
      if (password != NULL)
      	*password = authcolon + 1;
    }

    if (host != NULL)
      *host = at + 1;
  } else
    if (host != NULL)
      *host = start;

#ifdef ENABLE_IPV6
  /* Add support for RFC 2732 */
  {
      char *hostbracket, *hostendbracket;

      hostbracket = strchr(start, '[');
      if (hostbracket != NULL) {
	  
	  hostendbracket = strchr(hostbracket, ']');
	  
	  if (hostendbracket != NULL) {
	      
	      *hostendbracket = '\0';
	      *host = (hostbracket + 1);
	      
	      /* Might have a trailing port */
	      
	      if (*(hostendbracket+1) == ':') {
		  portcolon = (hostendbracket + 1);
	      }
	  }
      }
  }
#endif

  if (slash != 0)
  {
    *slash = '\0';
    
    if (filename != NULL)
      *filename = slash + 1;
  } else {
    if (filename != NULL)
      *filename = urlbuf + strlen(urlbuf);
  }
  
  if (portcolon != NULL)
  {
    *portcolon = '\0';
    
    if (port != NULL)
      *port = atoi(portcolon + 1);
  }
  
  return 0;
}

static int http_plugin_basicauth (const char *user, const char *password,
				  char* dest, int len) {
  static char *enctable="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
  char        *tmp;
  char        *sptr;
  char        *dptr;
  int          totlen;
  int          enclen;
  int          count;
  
  totlen = strlen (user) + 1;
  if(password != NULL)
    totlen += strlen (password);
  
  enclen = ((totlen + 2) / 3 ) * 4 + 1;
  
  if (len < enclen)
    return -1;
  
  tmp = malloc (sizeof(char) * (totlen + 1));
  strcpy (tmp, user);
  strcat (tmp, ":");
  if (password != NULL)
    strcat (tmp, password);  
  
  count = strlen(tmp);
  sptr = tmp;
  dptr = dest;
  while (count >= 3) {
    dptr[0] = enctable[(sptr[0] & 0xFC) >> 2];
    dptr[1] = enctable[((sptr[0] & 0x3) << 4) | ((sptr[1] & 0xF0) >> 4)];
    dptr[2] = enctable[((sptr[1] & 0x0F) << 2) | ((sptr[2] & 0xC0) >> 6)];
    dptr[3] = enctable[sptr[2] & 0x3F];
    count -= 3;
    sptr += 3;
    dptr += 4;
  }
  
  if (count > 0) {
    dptr[0] = enctable[(sptr[0] & 0xFC) >> 2];
    dptr[1] = enctable[(sptr[0] & 0x3) << 4];
    dptr[2] = '=';
    
    if (count > 1) {
      dptr[1] = enctable[((sptr[0] & 0x3) << 4) | ((sptr[1] & 0xF0) >> 4)];
      dptr[2] = enctable[(sptr[1] & 0x0F) << 2];
    }
    
    dptr[3] = '=';
    dptr += 4;
  }
  
  dptr[0] = '\0';
  
  free(tmp);
  return 0;
}

static void http_plugin_read_metainf (input_plugin_t *this_gen) {
 
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;
  char metadata_buf[255 * 16];
  unsigned char len = 0;
  char *title_end;
  char *songtitle, *radio;
  xine_event_t uevent;
  xine_ui_data_t data;
    
  /* avoid recursion */
  this->shoutcast_mode = 0;
  
  /* get the length of the metadata */
  this_gen->read(this_gen, &len, 1);

#ifdef LOG
  printf ("input_http: http_plugin_read_metainf: len=%d\n", len);
#endif
  
  if (len > 0) {
    this_gen->read(this_gen, metadata_buf, len * 16);
  
    metadata_buf[len * 16] = '\0';
#ifdef LOG
  printf ("input_http: http_plugin_read_metainf: %s\n", metadata_buf);
#endif
    /* Extract the title of the current song */
    if ((songtitle = strstr(metadata_buf, "StreamTitle='"))) {
      songtitle += 13; /* skip "StreamTitle='" */
      if ((title_end = strchr(songtitle, '\''))) {
        *title_end = '\0';
        
        if ((!this->shoutcast_songtitle ||
             (strcmp(songtitle, this->shoutcast_songtitle))) &&
            (strlen(songtitle) > 0)) {
#ifdef LOG
          printf ("input_http: http_plugin_read_metainf: songtitle: %s\n", songtitle);
#endif
          
          if (this->shoutcast_songtitle)
            free(this->shoutcast_songtitle);
          this->shoutcast_songtitle = strdup(songtitle);

          if (this->stream->meta_info [XINE_META_INFO_TITLE])
            free(this->stream->meta_info [XINE_META_INFO_TITLE]);
          this->stream->meta_info [XINE_META_INFO_TITLE] = strdup (songtitle);

          /* prepares the event */
          radio = this->stream->meta_info [XINE_META_INFO_ALBUM];
          
          if (radio) {
            strcpy(data.str, radio); /* WARNING: the data.str is char[256] */
            strcat(data.str, " - ");
            data.str_len = strlen(radio) + 3 + strlen(songtitle) + 1;
          } else {
            data.str[0] = 0;
            data.str_len = strlen(songtitle) + 1;
          }  
          strcat(data.str, songtitle);

          /* sends the event */
          uevent.type = XINE_EVENT_UI_SET_TITLE;
          uevent.stream = this->stream;
          uevent.data = &data;
          uevent.data_length = sizeof(data);
          xine_event_send(this->stream, &uevent);
        }
      }
    }
  }

  this->shoutcast_mode = 1;
  this->shoutcast_pos  = 0;
}

static off_t http_plugin_read (input_plugin_t *this_gen,
			       char *buf, off_t nlen) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;
  off_t n, num_bytes;

  num_bytes = 0;

  if (this->shoutcast_mode && (this->shoutcast_pos == this->shoutcast_metaint)) {
    http_plugin_read_metainf(this_gen);
  }

  if (this->curpos < this->preview_size) {

    n = this->preview_size - this->curpos;
    if (n > (nlen - num_bytes))
      n = nlen - num_bytes;

#ifdef LOG
    printf ("input_http: %lld bytes from preview (which has %lld bytes)\n",
            n, this->preview_size);
#endif

    if (this->shoutcast_mode) {
      if ((this->shoutcast_pos + n) >= this->shoutcast_metaint) {
        int i = this->shoutcast_metaint - this->shoutcast_pos;
        memcpy (&buf[num_bytes], &this->preview[this->curpos], i);
        this->shoutcast_pos += i;
        num_bytes += i;
        this->curpos += i;
        n -= i;
        http_plugin_read_metainf(this_gen);
      }

      this->shoutcast_pos += n;
    }

    memcpy (&buf[num_bytes], &this->preview[this->curpos], n);

    num_bytes += n;
    this->curpos += n;
  } 

  n = nlen - num_bytes;
  if( n && this->shoutcast_mode) {
    if ((this->shoutcast_pos + n) >= this->shoutcast_metaint) {
      int i = this->shoutcast_metaint - this->shoutcast_pos;
      i = xio_tcp_read (this->stream, this->fh, &buf[num_bytes], i);
      if (i < 0) {
        if (!this->stream->demux_action_pending) 
	  xine_message (this->stream, XINE_MSG_READ_ERROR, this->host, NULL);
        xine_log (this->stream->xine, XINE_LOG_MSG, _("input_http: read error %d\n"), errno);
        return 0;
      }

      this->shoutcast_pos += i;
      num_bytes += i;
      this->curpos += i;
      n -= i;
      http_plugin_read_metainf(this_gen);
    }

    this->shoutcast_pos += n;
  }

  if( n ) {
    n = xio_tcp_read (this->stream, this->fh, &buf[num_bytes], n);

    /* read errors */
    if (n < 0) {
      if (!this->stream->demux_action_pending) 
	xine_message(this->stream, XINE_MSG_READ_ERROR, this->host, NULL);
      xine_log (this->stream->xine, XINE_LOG_MSG, _("input_http: read error %d\n"), errno);
      return 0;
    }

    num_bytes += n;
    this->curpos += n;
  }

  return num_bytes;
}

static int read_shoutcast_header(http_input_plugin_t *this) {
  int done, len, linenum;

  done = 0; len = 0; linenum = 0;
  while (!done) {

    /*
    printf ("input_http: read...\n");
    */

    if (http_plugin_read ((input_plugin_t*)this, &this->buf[len], 1) == 0) {
      return 1;
    }

    if (this->buf[len] == '\012') {

      this->buf[len] = '\0';
      len--;

      if (len >= 0 && this->buf[len] == '\015') {
        this->buf[len] = '\0';
        len--;
      }

      linenum++;

#ifdef LOG
      printf ("input_http: shoutcast answer: >%s<\n", this->buf);
#endif

      if (!strncasecmp(this->buf, "icy-name:", 9)) {
        this->stream->meta_info [XINE_META_INFO_ALBUM]
          = strdup (this->buf + 9 + (*(this->buf + 9) == ' '));
        this->stream->meta_info [XINE_META_INFO_TITLE]
          = strdup (this->buf + 9 + (*(this->buf + 9) == ' '));
      }

      if (!strncasecmp(this->buf, "icy-genre:", 10)) {
        this->stream->meta_info [XINE_META_INFO_GENRE]
          = strdup (this->buf + 10 + (*(this->buf + 10) == ' '));
      }

      /* icy-notice1 is always the same */
      if (!strncasecmp(this->buf, "icy-notice2:", 12)) {
        this->stream->meta_info [XINE_META_INFO_COMMENT]
          = strdup (this->buf + 12 + (*(this->buf + 12) == ' '));
      }

      /* metadata interval (in byte) */
      if (sscanf(this->buf, "icy-metaint:%d", &this->shoutcast_metaint) == 1) {
#ifdef LOG
        printf("input_http: shoutcast_metaint: %d\n", this->shoutcast_metaint);
#endif
      }

      if (len == -1)
        done = 1;
      else
        len = 0;
    } else
      len ++;
  }
#ifdef LOG
  printf ("input_http: end of the shoutcast header\n");
#endif

  return 0;
}

static buf_element_t *http_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t todo) {

  off_t                 total_bytes;
  buf_element_t        *buf = fifo->buffer_pool_alloc (fifo);

  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;

  total_bytes = http_plugin_read (this_gen, buf->content, todo);

  if (total_bytes != todo) {
    buf->free_buffer (buf);
    buf = NULL;
  }

  if (buf != NULL)
    buf->size = total_bytes;

  return buf;
}

static off_t http_plugin_get_length (input_plugin_t *this_gen) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  return this->contentlength;
}

static uint32_t http_plugin_get_capabilities (input_plugin_t *this_gen) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;
  uint32_t caps = INPUT_CAP_PREVIEW;

  /* Nullsoft asked to not allow saving streaming nsv files */
  if (this->filename && 
      !strncmp(this->filename + strlen(this->filename) - 4, ".nsv", 4))
    caps |= INPUT_CAP_RIP_FORBIDDEN;

  return caps;
}

static uint32_t http_plugin_get_blocksize (input_plugin_t *this_gen) {

  return 0;
}

static off_t http_plugin_get_current_pos (input_plugin_t *this_gen){
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  return this->curpos;
}

static off_t http_plugin_seek(input_plugin_t *this_gen, off_t offset, int origin) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  if ((origin == SEEK_CUR) && (offset >= 0)) {

    for (;((int)offset) - BUFSIZE > 0; offset -= BUFSIZE) {
      if( !this_gen->read (this_gen, this->seek_buf, BUFSIZE) )
        return this->curpos;
    }

    this_gen->read (this_gen, this->seek_buf, offset);
  }

  if (origin == SEEK_SET) {

    if (offset < this->curpos) {

      if( this->curpos <= this->preview_size )
        this->curpos = offset;
      else
        printf ("http: cannot seek back! (%lld > %lld)\n", this->curpos, offset);

    } else {
      offset -= this->curpos;

      for (;((int)offset) - BUFSIZE > 0; offset -= BUFSIZE) {
        if( !this_gen->read (this_gen, this->seek_buf, BUFSIZE) )
          return this->curpos;
      }

      this_gen->read (this_gen, this->seek_buf, offset);
    }
  }

  return this->curpos;
}

static char* http_plugin_get_mrl (input_plugin_t *this_gen) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  return this->mrlbuf2;
}

static int http_plugin_get_optional_data (input_plugin_t *this_gen,
					  void *data, int data_type) {

  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  switch (data_type) {
  case INPUT_OPTIONAL_DATA_PREVIEW:

    memcpy (data, this->preview, this->preview_size);
    return this->preview_size;

    break;
  }

  return INPUT_OPTIONAL_UNSUPPORTED;
}

static void http_plugin_dispose (input_plugin_t *this_gen ) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  if (this->fh != -1) {
    close(this->fh);
    this->fh = -1;
  }
  
  if (this->nbc) {
    nbc_close (this->nbc);
    this->nbc = NULL;
  }

  free (this);
}

static void report_progress (xine_stream_t *stream, int p) {

  xine_event_t             event;
  xine_progress_data_t     prg;
  
  prg.description = _("Connecting HTTP server...");
  prg.percent = p;

  event.type = XINE_EVENT_PROGRESS;
  event.data = &prg;
  event.data_length = sizeof (xine_progress_data_t);

  xine_event_send (stream, &event);
}

static int http_plugin_open (input_plugin_t *this_gen ) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;
  http_input_class_t  *this_klass = (http_input_class_t *) this->input_plugin.input_class;
  int                  done,len,linenum;
  int                  shoutcast = 0, httpcode;
  int                  length;
  int                  res, progress;
  
  this->shoutcast_pos = 0;
  
  if (this_klass->proxyhost != NULL && strcmp (this_klass->proxyhost, "")) {
    if (this_klass->proxyport == 0)
      this_klass->proxyport = DEFAULT_HTTP_PORT;

    if ( (this_klass->proxyuser != NULL) && strcmp (this_klass->proxyuser, "") )
      if (http_plugin_basicauth (this_klass->proxyuser,
			         this_klass->proxypassword,
				 this->proxyauth, BUFSIZE)) {
	xine_message(this->stream, XINE_MSG_CONNECTION_REFUSED, "proxy error", NULL);
	return 0;
      }
  }
  
  if (http_plugin_parse_url (this->mrlbuf, &this->user, &this->password,
			     &this->host, &this->port, &this->filename)) {
    return 0;
  }

  if (this->port == 0)
    this->port = DEFAULT_HTTP_PORT;

  if ( (this->user != NULL) && strcmp (this_klass->proxyhost, ""))
    if (http_plugin_basicauth (this->user, this->password, this->auth, BUFSIZE)) {
      xine_message(this->stream, XINE_MSG_CONNECTION_REFUSED, "basic auth error", NULL);
      return 0;
    }

#ifdef LOG
  {
    printf ("input_http: opening >/%s< on host >%s<", 
	    this->filename, this->host);

    if (this_klass->proxyhost != NULL)
      printf (" via proxy >%s<", this_klass->proxyhost);
    
    printf ("\n");
  }
#endif
  
  if ( (this_klass->proxyhost != NULL) && strcmp (this_klass->proxyhost, "") )
    this->fh = xio_tcp_connect (this->stream, this_klass->proxyhost, this_klass->proxyport);
  else
    this->fh = xio_tcp_connect (this->stream, this->host, this->port);

  this->curpos = 0;

  if (this->fh == -1) {
    return 0;
  }

  /* connection timeout 20s */
  progress = 0;
  do {
    report_progress(this->stream, progress);
    res = xio_select (this->stream, this->fh, XIO_WRITE_READY, 500);
    progress += 2;
  } while ((res == XIO_TIMEOUT) && (progress < 100));
  if (res != XIO_READY)
    return 0;
  
  if ( (this_klass->proxyhost != NULL) && strcmp (this_klass->proxyhost, "") ) {
    if (this->port != DEFAULT_HTTP_PORT) {
      sprintf (this->buf, "GET http://%s:%d/%s HTTP/1.0\015\012",
	       this->host, this->port, this->filename);
    } else {
      sprintf (this->buf, "GET http://%s/%s HTTP/1.0\015\012",
	       this->host, this->filename);
    }
  } else {
    sprintf (this->buf, "GET /%s HTTP/1.0\015\012", this->filename);
  }
  
  if (this->port != DEFAULT_HTTP_PORT)
    sprintf (this->buf + strlen(this->buf), "Host: %s:%d\015\012",
	     this->host, this->port);
  else
    sprintf (this->buf + strlen(this->buf), "Host: %s\015\012",
	     this->host);
  
  if ( (this_klass->proxyuser != NULL) && strcmp (this_klass->proxyuser, "") )
    sprintf (this->buf + strlen(this->buf), "Proxy-Authorization: Basic %s\015\012",
	     this->proxyauth);

  if ( (this->user != NULL) && strcmp (this->user, "") ) 
    sprintf (this->buf + strlen(this->buf), "Authorization: Basic %s\015\012",
	     this->auth);
 
  sprintf (this->buf + strlen(this->buf), "User-Agent: xine/%s\015\012",
           VERSION);
  strcat (this->buf, "Accept: */*\015\012");
  strcat (this->buf, "Icy-MetaData: 1\015\012");
  
  strcat (this->buf, "\015\012");

  length = strlen(this->buf);
  if (xio_tcp_write (this->stream, this->fh, this->buf, length) != length) {
    xine_message(this->stream, XINE_MSG_CONNECTION_REFUSED, "couldn't send request", NULL);
    printf ("input_http: couldn't send request\n");
    return 0;
  }

#ifdef LOG
  printf ("input_http: request sent: >%s<\n", this->buf);
#endif

  /* read and parse reply */
  done = 0; len = 0; linenum = 0;
  this->contentlength = 0;

  while (!done) {

    /* 
       printf ("input_http: read...\n");
    */

    if (xio_tcp_read (this->stream, this->fh, &this->buf[len], 1) <= 0) {
      return 0;
    }

    if (this->buf[len] == '\012') {

      this->buf[len] = '\0';
      len--;
      
      if (len >= 0 && this->buf[len] == '\015') {
	this->buf[len] = '\0';
	len--;
      }

      linenum++;
      
#ifdef LOG
      printf ("input_http: answer: >%s<\n", this->buf);
#endif

      if (linenum == 1) {
        int httpver, httpsub;
	char httpstatus[BUFSIZE];

	if (sscanf(this->buf, "HTTP/%d.%d %d %[^\015\012]", &httpver, &httpsub,
		   &httpcode, httpstatus) != 4)	{
	  
	  /* icecast ? */
	  if (sscanf(this->buf, "ICY %d OK", &httpcode) != 1)	{
	    xine_message(this->stream, XINE_MSG_CONNECTION_REFUSED, "invalid http answer", NULL);
	    xine_log (this->stream->xine, XINE_LOG_MSG, 
		      _("input_http: invalid http answer\n"));
	    return 0;
	  } else {
	    shoutcast = 1;
	    done = 1;
	  }
	}

	if (httpcode >= 300 && httpcode < 400) {
      	  xine_log (this->stream->xine, XINE_LOG_MSG, 
		    _("input_http: 3xx redirection: >%d %s<\n"),
		    httpcode, httpstatus);
	} else if (httpcode < 200 || httpcode >= 300) {
	  xine_message(this->stream, XINE_MSG_CONNECTION_REFUSED, "http status not 2xx: ",
	               httpstatus, NULL);
      	  xine_log (this->stream->xine, XINE_LOG_MSG,
		    _("input_http: http status not 2xx: >%d %s<\n"),
		    httpcode, httpstatus);
	  return 0;
	}
      } else {
	if (this->contentlength == 0) {
	  off_t contentlength;
	  
	  if (sscanf(this->buf, "Content-Length: %Ld", &contentlength) == 1) {
      	    xine_log (this->stream->xine, XINE_LOG_MSG, 
		      _("input_http: content length = %Ld bytes\n"), contentlength);
	    this->contentlength = contentlength;
	  }
        }
	
	if (!strncasecmp(this->buf, "Location: ", 10)) {
	  char *href = strdup (this->buf + 10);

#ifdef LOG
	  printf ("input_http: trying to open target of redirection: >%s<\n",
            href);
#endif
          strncpy (this->mrlbuf, href, BUFSIZE);
          strncpy (this->mrlbuf2, href, BUFSIZE);
          return http_plugin_open(this_gen);
        }
      }
 
      if (len == -1)
	done = 1;
      else
	len = 0;
    } else
      len ++;
  }

#ifdef LOG
  printf ("input_http: end of headers\n");
#endif

  /*
   * fill preview buffer
   */

  this->preview_size = http_plugin_read (&this->input_plugin, this->preview,
					 MAX_PREVIEW_SIZE);

  this->curpos  = 0;
  
  /* Trivial shoutcast detection */
  this->shoutcast_metaint = 0;
  this->shoutcast_songtitle = NULL;
  if (shoutcast ||
      !strncasecmp(this->preview, "ICY", 3)) {
    this->mrlbuf2[0] = 'i';
    this->mrlbuf2[1] = 'c';
    this->mrlbuf2[2] = 'e';
    this->mrlbuf2[3] = ' ';
    if (read_shoutcast_header(this)) {
      /* problem when reading shoutcast header */
      xine_message(this->stream, XINE_MSG_CONNECTION_REFUSED, "can't read shoutcast header", NULL);
      printf ("can't read shoutcast header\n");
      return 0;
    }
    this->shoutcast_mode = 1;
    this->shoutcast_pos = 0;
  } else {
    this->shoutcast_mode = 0;
  }

  return 1;
}

/*
 * http input plugin class
 */
static input_plugin_t *http_class_get_instance (input_class_t *cls_gen, xine_stream_t *stream,
				    const char *mrl) {

  /* http_input_class_t  *cls = (http_input_class_t *) cls_gen;*/
  http_input_plugin_t *this;
  
  if (strncasecmp (mrl, "http://", 7)) {
    return NULL;
  }
  this = (http_input_plugin_t *) xine_xmalloc(sizeof(http_input_plugin_t));

  strncpy (this->mrlbuf, mrl, BUFSIZE);
  strncpy (this->mrlbuf2, mrl, BUFSIZE);
  this->mrl     = this->mrlbuf2;
  this->stream  = stream;
  this->fh      = -1;
  this->nbc     = nbc_init (this->stream);
  
  this->input_plugin.open              = http_plugin_open;
  this->input_plugin.get_capabilities  = http_plugin_get_capabilities;
  this->input_plugin.read              = http_plugin_read;
  this->input_plugin.read_block        = http_plugin_read_block;
  this->input_plugin.seek              = http_plugin_seek;
  this->input_plugin.get_current_pos   = http_plugin_get_current_pos;
  this->input_plugin.get_length        = http_plugin_get_length;
  this->input_plugin.get_blocksize     = http_plugin_get_blocksize;
  this->input_plugin.get_mrl           = http_plugin_get_mrl;
  this->input_plugin.get_optional_data = http_plugin_get_optional_data;
  this->input_plugin.dispose           = http_plugin_dispose;
  this->input_plugin.input_class       = cls_gen;

  return &this->input_plugin;
}

static char *http_class_get_description (input_class_t *this_gen) {
  return _("http input plugin");
}

static char *http_class_get_identifier (input_class_t *this_gen) {
  return "http";
}

static void http_class_dispose (input_class_t *this_gen) {
  http_input_class_t  *this = (http_input_class_t *) this_gen;

  free (this);
}

static void *init_class (xine_t *xine, void *data) {

  http_input_class_t  *this;
  config_values_t     *config;

  this = (http_input_class_t *) xine_xmalloc (sizeof (http_input_class_t));

  this->xine   = xine;
  this->config = xine->config;
  config       = xine->config;

  this->input_class.get_instance       = http_class_get_instance;
  this->input_class.get_identifier     = http_class_get_identifier;
  this->input_class.get_description    = http_class_get_description;
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = NULL;
  this->input_class.dispose            = http_class_dispose;
  this->input_class.eject_media        = NULL;

  this->proxyuser = config->register_string(config, "input.http_proxy_user",
		  "", _("http proxy username"),
		  NULL, 0, proxy_user_change_cb, (void *) this);
  this->proxypassword = config->register_string(config,
		  "input.http_proxy_password", "", _("http proxy password"),
		  NULL, 0, proxy_password_change_cb, (void *) this);
  this->proxyhost = config->register_string(config,
		  "input.http_proxy_host", "", _("http proxy host"),
		  NULL, 0, proxy_host_change_cb, (void *) this);
  this->proxyport = config->register_num(config,
		  "input.http_proxy_port", DEFAULT_HTTP_PORT,
		  _("http proxy port"),
		  NULL, 0, proxy_port_change_cb, (void *) this);

  return this;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_INPUT, 13, "http", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

