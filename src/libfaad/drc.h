/*
** FAAD - Freeware Advanced Audio Decoder
** Copyright (C) 2002 M. Bakker
**  
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
** 
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software 
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
**
** $Id: drc.h,v 1.2 2002/12/16 19:00:00 miguelfreitas Exp $
**/

#ifndef __DRC_H__
#define __DRC_H__

#ifdef __cplusplus
extern "C" {
#endif

#define DRC_REF_LEVEL 20*4 /* -20 dB */


drc_info *drc_init(real_t cut, real_t boost);
void drc_end(drc_info *drc);
void drc_decode(drc_info *drc, real_t *spec);


#ifdef __cplusplus
}
#endif
#endif
