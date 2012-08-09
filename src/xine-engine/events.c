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
 * $Id: events.c,v 1.19 2003/04/02 12:28:09 hadess Exp $
 *
 * Event handling functions
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xine_internal.h"

xine_event_t *xine_event_get  (xine_event_queue_t *queue) {

  xine_event_t  *event;

  pthread_mutex_lock (&queue->lock);

  event = (xine_event_t *) xine_list_first_content (queue->events);
  if (event)
    xine_list_delete_current (queue->events);

  pthread_mutex_unlock (&queue->lock);

  return event;
}

xine_event_t *xine_event_wait (xine_event_queue_t *queue) {

  xine_event_t  *event;

  pthread_mutex_lock (&queue->lock);

  while (!(event = (xine_event_t *) xine_list_first_content (queue->events))) {
    pthread_cond_wait (&queue->new_event, &queue->lock);
  }

  xine_list_delete_current (queue->events);

  pthread_mutex_unlock (&queue->lock);

  return event;
}

void xine_event_free (xine_event_t *event) {
  if (event->data && event->data_length > 0) free (event->data);
  free (event);
}

void xine_event_send (xine_stream_t *stream, const xine_event_t *event) {

  xine_event_queue_t *queue;

  pthread_mutex_lock (&stream->event_queues_lock);

  queue = (xine_event_queue_t *)xine_list_first_content (stream->event_queues);

  while (queue) {
    
    xine_event_t *cevent;

    cevent = malloc (sizeof (xine_event_t));
    cevent->type        = event->type;
    cevent->stream      = stream;
    cevent->data_length = event->data_length;
    if ((event->data_length > 0) && (event->data) ) {
      cevent->data = malloc (event->data_length);
      memcpy (cevent->data, event->data, event->data_length);
    } else {
      cevent->data = NULL;
    }
    gettimeofday (&cevent->tv, NULL);
    
    pthread_mutex_lock (&queue->lock);
    xine_list_append_content (queue->events, cevent);
    pthread_cond_signal (&queue->new_event);
    pthread_mutex_unlock (&queue->lock);

    queue=(xine_event_queue_t *)xine_list_next_content (stream->event_queues);
  }

  pthread_mutex_unlock (&stream->event_queues_lock);
}


xine_event_queue_t *xine_event_new_queue (xine_stream_t *stream) {

  xine_event_queue_t *queue;

  queue = malloc (sizeof (xine_event_queue_t));

  pthread_mutex_init (&queue->lock, NULL);
  pthread_cond_init (&queue->new_event, NULL);
  queue->events = xine_list_new ();
  queue->stream = stream;
  queue->listener_thread = NULL;

  pthread_mutex_lock (&stream->event_queues_lock);
  xine_list_append_content (stream->event_queues, queue);
  pthread_mutex_unlock (&stream->event_queues_lock);

  return queue;
}

void xine_event_dispose_queue (xine_event_queue_t *queue) {

  xine_stream_t      *stream = queue->stream;
  xine_event_t       *event;
  xine_event_t       *qevent;
  xine_event_queue_t *q;
    
  pthread_mutex_lock (&stream->event_queues_lock);

  q = (xine_event_queue_t *) xine_list_first_content (stream->event_queues);

  while (q && (q != queue))
    q = (xine_event_queue_t *) xine_list_next_content (stream->event_queues);

  if (!q) {
    printf ("events: tried to dispose queue which is not in list\n");

    pthread_mutex_unlock (&stream->event_queues_lock);
    return;
  }

  xine_list_delete_current (stream->event_queues);
  pthread_mutex_unlock (&stream->event_queues_lock);

  /* 
   * send quit event 
   */
  qevent = (xine_event_t *)malloc(sizeof(xine_event_t));
  
  qevent->type        = XINE_EVENT_QUIT;
  qevent->stream      = stream;
  qevent->data        = NULL;
  qevent->data_length = 0;
  
  pthread_mutex_lock (&queue->lock);
  xine_list_append_content (queue->events, qevent);
  pthread_cond_signal (&queue->new_event);
  pthread_mutex_unlock (&queue->lock);

  /*
   * join listener thread, if any
   */
  
  if (queue->listener_thread) {
    void *p;
    pthread_join (*queue->listener_thread, &p);
    free (queue->listener_thread);
  }
  
  /*
   * clean up pending events 
   */

  while ( (event = xine_event_get (queue)) ) {
    xine_event_free (event);
  }

  free (queue);
}


static void *listener_loop (void *queue_gen) {

  xine_event_queue_t *queue = (xine_event_queue_t *) queue_gen;
  int running = 1;

  while (running) {

    xine_event_t *event;

    event = xine_event_wait (queue);

    if (event->type == XINE_EVENT_QUIT)
      running = 0;
    else 
      queue->callback (queue->user_data, event);

    xine_event_free (event);
  }

  return NULL;
}


void xine_event_create_listener_thread (xine_event_queue_t *queue, 
					xine_event_listener_cb_t callback,
					void *user_data) {
  int err;

  queue->listener_thread = malloc (sizeof (pthread_t));
  queue->callback        = callback;
  queue->user_data       = user_data;

  if ((err = pthread_create (queue->listener_thread,
			     NULL, listener_loop, queue)) != 0) {
    fprintf (stderr, "events: can't create new thread (%s)\n",
	     strerror(err));
    abort();
  }
}

void xine_event_create_cue_point(xine_stream_t *stream, int cuetime, 
                   int frequency, void *user_cue_data) {
  xine_cue_point_data_t *cue_point;
  
  cue_point=malloc(sizeof(xine_cue_point_data_t));
  cue_point->cuetime=cuetime;
  cue_point->frequency=frequency;
  cue_point->user_cue_data=user_cue_data;
  
  pthread_mutex_lock (&stream->cue_points_lock);
  xine_list_append_content (stream->cue_points, cue_point);
  pthread_mutex_unlock (&stream->cue_points_lock);

}

void xine_event_dispose_cue_point(xine_stream_t *stream, int cuetime, 
                   int frequency) {
   xine_cue_point_data_t *cue_point;

   pthread_mutex_lock (&stream->cue_points_lock);

   cue_point = (xine_cue_point_data_t *) xine_list_first_content (stream->cue_points);

   while (cue_point && ((cue_point->cuetime!=cuetime) ||(cue_point->frequency!=frequency)))
      cue_point = (xine_cue_point_data_t *) xine_list_next_content (stream->cue_points);

   xine_list_delete_current (stream->cue_points);
   pthread_mutex_unlock (&stream->cue_points_lock);

   free(cue_point);
}

void xine_recalculate_next_cue_point(xine_stream_t *stream, int64_t cur_time) {
   xine_cue_point_data_t *cue_point;
   int next_time=0x7FFFFFFF;

   if (cur_time==0) {
      cur_time=stream->xine->clock->get_current_time(stream->xine->clock);
   } 
   cur_time=(cur_time-stream->metronom->vpts_offset)/90;
   
   pthread_mutex_lock (&stream->cue_points_lock);
   cue_point = (xine_cue_point_data_t *) xine_list_first_content (stream->cue_points);

   while (cue_point) {
      if (cue_point->cuetime>cur_time && cue_point->cuetime<next_time) {
         next_time=cue_point->cuetime;
         stream->next_cue_data=cue_point;
      } else {
         if (cue_point->frequency!=0 && cue_point->cuetime<cur_time && (cue_point->frequency-((cur_time-cue_point->cuetime) % cue_point->frequency))<(next_time-cur_time)) {
            next_time=(cue_point->frequency-((cur_time-cue_point->cuetime) % cue_point->frequency))+cur_time;
            stream->next_cue_data=cue_point;
         }
      } /* endif */
      cue_point = (xine_cue_point_data_t *) xine_list_next_content (stream->cue_points);
   }
   pthread_mutex_unlock (&stream->cue_points_lock);
   stream->next_cue_time=(next_time*90)+stream->metronom->vpts_offset;
/*   printf("Now: %Ld Next Cue: %d\n",cur_time,next_time);*/
}

void xine_cue_point_event(xine_stream_t *stream, int64_t in_cur_time) {
   xine_event_t event;
   int next_time;
   int cur_time;
   int next_freq;
   xine_cue_point_data_t *cue_point;

   event.type=XINE_EVENT_CUE_POINT;
   event.data_length=sizeof(xine_cue_point_data_t);
   cur_time=(in_cur_time-stream->metronom->vpts_offset)/90;
   next_time=stream->org_next_cue_time;
   if (cur_time<next_time) return;
   pthread_mutex_lock (&stream->cue_points_lock);
   cue_point = (xine_cue_point_data_t *) xine_list_first_content(stream->cue_points);
   while (cue_point) {
      if (cue_point->cuetime<=cur_time && cue_point->cuetime>=next_time) {
         event.data=cue_point;
         cue_point->currtime=cur_time;
         xine_event_send(stream,&event);
      } else {
         next_freq=cue_point->frequency-((next_time-cue_point->cuetime) % cue_point->frequency);
         if (next_freq==cue_point->frequency) next_freq=0;
/*         printf("%d, %d, %d\n",next_time,next_freq,(cur_time-next_time));*/
         if (cue_point->frequency!=0 && cue_point->cuetime<cur_time && (next_freq<=(cur_time-next_time))) {
            event.data=cue_point;
            cue_point->currtime=cur_time;
            xine_event_send(stream,&event);
         }
      }
      cue_point = (xine_cue_point_data_t *) xine_list_next_content(stream->cue_points);
   }
   pthread_mutex_unlock (&stream->cue_points_lock);
   xine_recalculate_next_cue_point(stream,in_cur_time);
}
