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
 * $Id: lrb.c,v 1.4 2003/02/28 02:51:51 storri Exp $
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lrb.h"
#include "xineutils.h"

lrb_t *lrb_new (int max_num_entries,
		fifo_buffer_t *fifo) {

  lrb_t *this;
  
  this = xine_xmalloc (sizeof (lrb_t));

  this->max_num_entries = max_num_entries;
  this->cur_num_entries = 0;
  this->fifo            = fifo;
  this->newest          = NULL;
  this->oldest          = NULL;

  return this;
}

void lrb_drop (lrb_t *this) {
  
  buf_element_t *buf = this->oldest;

  XINE_ASSERT(buf, "Oldest buffer element is NULL");

  this->oldest = buf->next;

  buf->free_buffer (buf);

  this->cur_num_entries--;

}

void lrb_add (lrb_t *this, buf_element_t *buf) {

  if (!this->newest) {

    this->newest  = buf;
    this->oldest  = buf;
    buf->next     = NULL;

    this->cur_num_entries = 1;

  } else {

    if (this->cur_num_entries >= this->max_num_entries) 
      lrb_drop (this);

    buf->next = NULL;
    this->newest->next = buf;
    this->newest = buf;
    this->cur_num_entries++;
  }

  printf ("lrb: %d elements in buffer\n", this->cur_num_entries);

}

void lrb_feedback (lrb_t *this, fifo_buffer_t *fifo) {

  pthread_mutex_lock (&fifo->mutex);

  while (this->cur_num_entries) {

    buf_element_t *buf = this->oldest;

    buf->next = fifo->first;

    fifo->first = buf;

    if (!fifo->last)
      fifo->last = buf;
    
    fifo->fifo_size++;

    pthread_cond_signal (&fifo->not_empty);

    this->oldest = buf->next;

    this->cur_num_entries--;

    printf ("lrb: feedback\n");

  }

  if (!this->oldest)
    this->newest = NULL;

  pthread_mutex_unlock (&fifo->mutex);
}

void lrb_flush (lrb_t *this) {
  while (this->cur_num_entries) 
    lrb_drop (this);
}
