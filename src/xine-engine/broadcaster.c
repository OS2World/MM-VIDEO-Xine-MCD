/*
 * Copyright (C) 2000-2003 the xine project
 * May 2003 - Miguel Freitas
 * This feature was sponsored by 1Control
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
 * $Id: broadcaster.c,v 1.2 2003/05/16 15:07:37 tchamp Exp $
 * 
 * broadcaster.c - xine network broadcaster
 *
 * how it works:
 *  - one xine instance must be set as master by using XINE_PARAM_BROADCASTER_PORT.
 *    'xine --broadcast-port <port_number>'
 *  - master will wait for connections on specified port, accepting new clients.
 *  - several xine clients may connect to the server as "slaves", using mrl:
 *    slave://master_address:port
 *  - streams played on master will appear on every slave. 
 *    if master is not meant to use video/audio devices it may be started with
 *    'xine -V none -A none'
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifndef _MSC_VER
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif /* _MSC_VER */

#include <dlfcn.h>
#include <pthread.h>

#include "xine_internal.h"
#include "buffer.h"
#include "xineutils.h"

#define QLEN 5    /* maximum connection queue length */
#define _BUFSIZ 512

struct broadcaster_s {
  xine_stream_t   *stream;        /* stream to broadcast            */
  int              port;          /* server port                    */
  int              msock;         /* master network socket          */
  xine_list_t     *connections;   /* active connections             */
  
  int              running;
  pthread_t        manager_thread;
  pthread_mutex_t  lock;
};


/* network functions */

static int sock_check_opened(int socket) {
  fd_set   readfds, writefds, exceptfds;
  int      retval;
  struct   timeval timeout;
  
  for(;;) {
    FD_ZERO(&readfds); 
    FD_ZERO(&writefds); 
    FD_ZERO(&exceptfds);
    FD_SET(socket, &exceptfds);
    
    timeout.tv_sec  = 0; 
    timeout.tv_usec = 0;
    
    retval = select(socket + 1, &readfds, &writefds, &exceptfds, &timeout);
    
    if(retval == -1 && (errno != EAGAIN && errno != EINTR))
      return 0;
    
    if (retval != -1)
      return 1;
  }

  return 0;
}

/*
 * Write to socket.
 */
static int sock_data_write(int socket, char *buf, int len) {
  ssize_t  size;
  int      wlen = 0;
  
  if((socket < 0) || (buf == NULL))
    return -1;
  
  if(!sock_check_opened(socket))
    return -1;
  
  while(len) {
    size = write(socket, buf, len);
    
    if(size <= 0) {
      printf("broadcaster: error writing to socket %d\n",socket);
      return -1;
    }
    
    len -= size;
    wlen += size;
    buf += size;
  }

  return wlen;
}

static int sock_string_write(int socket, char *msg, ...) {
  char     buf[_BUFSIZ];
  va_list  args;
  
  va_start(args, msg);
  vsnprintf(buf, _BUFSIZ, msg, args);
  va_end(args);
  
  /* Each line sent is '\n' terminated */
  if((buf[strlen(buf)] == '\0') && (buf[strlen(buf) - 1] != '\n'))
      sprintf(buf, "%s%c", buf, '\n');
 
  return sock_data_write(socket, buf, strlen(buf));
}

/*
 * this is the most important broadcaster function. 
 * it sends data to every connected client (slaves).
 */
static void broadcaster_data_write(broadcaster_t *this, char *buf, int len) {
  int *psock;

  psock = xine_list_first_content (this->connections);
  while (psock) {
    
    /* in case of failure remove from list */
    if( sock_data_write(*psock, buf, len) < 0 ) {

      if (this->stream->xine->verbosity >= XINE_VERBOSITY_LOG)
        printf("broadcaster: closing socket %d\n", *psock);
      close(*psock);
      free(psock);
      if( this->connections->cur->next )
        psock = this->connections->cur->next->content;
      else
        psock = NULL;
      xine_list_delete_current (this->connections);
    } else
      psock = xine_list_next_content (this->connections);
  }
}

static void broadcaster_string_write(broadcaster_t *this, char *msg, ...) {
  char     buf[_BUFSIZ];
  va_list  args;
  
  va_start(args, msg);
  vsnprintf(buf, _BUFSIZ, msg, args);
  va_end(args);
  
  /* Each line sent is '\n' terminated */
  if((buf[strlen(buf)] == '\0') && (buf[strlen(buf) - 1] != '\n'))
      sprintf(buf, "%s%c", buf, '\n');
 
  broadcaster_data_write(this, buf, strlen(buf));
}


/*
 * this thread takes care of accepting new connections.
 */
static void *manager_loop (void *this_gen) {
  broadcaster_t *this = (broadcaster_t *) this_gen;
  struct sockaddr_in fsin; /* the from address of a client */
  int  alen;               /* from-address length */
  fd_set rfds;             /* read file descriptor set */
  fd_set efds;             /* exception descriptor set */
  
  while( this->running ) {
    FD_ZERO(&rfds);
    FD_SET(this->msock, &rfds);
    FD_ZERO(&efds);
    FD_SET(this->msock, &efds);
  
    if (select(this->msock+1, &rfds, (fd_set *)0, &efds, (struct timeval *)0) > 0) {
  
      pthread_mutex_lock( &this->lock );
  
      if (FD_ISSET(this->msock, &rfds))
      {
        int   ssock;
        alen = sizeof(fsin);
  
        ssock = accept(this->msock, (struct sockaddr *)&fsin, &alen);
        if (ssock >= 0) {
          /* identification string, helps demuxer probing */
          if( sock_string_write(ssock,"master xine v1") > 0 ) {
            int *psock = malloc(sizeof(int));
            *psock = ssock;
       
            if (this->stream->xine->verbosity >= XINE_VERBOSITY_LOG)
              printf("broadcaster: new connection socket %d\n", *psock);
            xine_list_append_content(this->connections, psock);
          }
        }
      }
      
      pthread_mutex_unlock( &this->lock );
    }
  }
  
  return NULL;
}



/*
 * receive xine buffers and send them through the broadcaster
 */
static void send_buf (broadcaster_t *this, char *from, buf_element_t *buf) {
  int i;
    
  /* ignore END buffers since they would stop the slavery */
  if( buf->type == BUF_CONTROL_END )
    return;

  /* assume RESET_DECODER is result of a xine_flush_engine */
  if( buf->type == BUF_CONTROL_RESET_DECODER && !strcmp(from,"video") ) {
    broadcaster_string_write(this, "flush_engine");
  }

  /* send decoder information if any */
  for( i = 0; i < BUF_NUM_DEC_INFO; i++ ) {
    if( buf->decoder_info[i] ) {
      broadcaster_string_write(this, "decoder_info index=%d decoder_info=%u has_data=%d",
                               i, buf->decoder_info[i], (buf->decoder_info_ptr[i]) ? 1 : 0);
      if( buf->decoder_info_ptr[i] )
        broadcaster_data_write(this, buf->decoder_info_ptr[i], buf->decoder_info[i]);
    }
  }
      
  broadcaster_string_write(this, "buffer fifo=%s size=%ld type=%lu pts=%lld disc=%lld flags=%lu",
                           from, buf->size, buf->type, buf->pts, buf->disc_off, buf->decoder_flags );

  if( buf->size )
    broadcaster_data_write(this, buf->content, buf->size);
}


/* buffer callbacks */
static void video_put_cb (fifo_buffer_t *fifo, buf_element_t *buf, void *this_gen) {
  broadcaster_t *this = (broadcaster_t *) this_gen;

  pthread_mutex_lock( &this->lock );
  send_buf(this, "video", buf);
  pthread_mutex_unlock( &this->lock );
}

static void audio_put_cb (fifo_buffer_t *fifo, buf_element_t *buf, void *this_gen) {
  broadcaster_t *this = (broadcaster_t *) this_gen;

  pthread_mutex_lock( &this->lock );
  send_buf(this, "audio", buf);
  pthread_mutex_unlock( &this->lock );
}

broadcaster_t *init_broadcaster(xine_stream_t *stream, int port)
{
  broadcaster_t *this;
  struct sockaddr_in servAddr;
  int msock, err;
  
  msock = socket(PF_INET, SOCK_STREAM, 0);
  if( msock < 0 )
  {
    printf("broadcaster: error opening master socket.\n");
    return NULL;
  }
  servAddr.sin_family = AF_INET;
  servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servAddr.sin_port = htons(port);

  if(bind(msock, (struct sockaddr *) &servAddr, sizeof(servAddr))<0)
  {
    printf("broadcaster: error binding to port %d\n", port);
    return NULL;
  }

  listen(msock,QLEN);
   
  signal( SIGPIPE, SIG_IGN );

  this = xine_xmalloc(sizeof(broadcaster_t));
  this->port = port;
  this->stream = stream;
  this->msock = msock;
  this->connections = xine_list_new();
  
  pthread_mutex_init (&this->lock, NULL);
  
  stream->video_fifo->register_put_cb(stream->video_fifo, video_put_cb, this);

  if(stream->audio_fifo)
    stream->audio_fifo->register_put_cb(stream->audio_fifo, audio_put_cb, this);
    
  this->running = 1;
  if ((err = pthread_create (&this->manager_thread,
                             NULL, manager_loop, (void *)this)) != 0) {
      printf ("broadcaster: can't create new thread (%s)\n",
              strerror(err));
      abort();
  }
  
  return this;  
}

void close_broadcaster(broadcaster_t *this)
{
  int *psock;
  
  psock = xine_list_first_content (this->connections);
  while (psock) {
    if (this->stream->xine->verbosity >= XINE_VERBOSITY_LOG)
      printf("broadcaster: closing socket %d\n", *psock);
    close(*psock);
    free(psock);
    xine_list_delete_current (this->connections);
    if( this->connections->cur )
      psock = this->connections->cur->content;
    else
      psock = NULL;
  }
  xine_list_free(this->connections);
  
  this->running = 0;  
  close(this->msock);
  pthread_mutex_lock( &this->lock );
  pthread_cancel(this->manager_thread);
  pthread_join(this->manager_thread,NULL);
    
  this->stream->video_fifo->unregister_put_cb(this->stream->video_fifo, video_put_cb);

  if(this->stream->audio_fifo)
    this->stream->audio_fifo->unregister_put_cb(this->stream->audio_fifo, audio_put_cb);
  
  free(this);
}

int get_broadcaster_port(broadcaster_t *this)
{
  return this->port;  
}
