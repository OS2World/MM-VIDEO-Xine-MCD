/*
 * Copyright (C) 2002 the xine project
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
 * $Id: mmsh.h,v 1.2 2003/01/31 14:06:17 miguelfreitas Exp $
 *
 * libmmsh public header
 */

#ifndef HAVE_MMSH_H
#define HAVE_MMSH_H

#include <inttypes.h>
#include "xine_internal.h"

typedef struct mmsh_s mmsh_t;

char*    mmsh_connect_common(int *s ,int *port, char *url, char **host, char **path, char **file);
mmsh_t*   mmsh_connect (xine_stream_t *stream, const char *url_, int bandwidth);

int      mmsh_read (mmsh_t *this, char *data, int len);
uint32_t mmsh_get_length (mmsh_t *this);
void     mmsh_close (mmsh_t *this);

int      mmsh_peek_header (mmsh_t *this, char *data, int maxsize);

#endif
