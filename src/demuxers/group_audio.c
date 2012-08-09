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
 * This file contains plugin entries for several demuxers used in games
 *
 * $Id: group_audio.c,v 1.8 2003/08/25 21:51:39 f1rmb Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xine_internal.h"
#include "demux.h"

#include "group_audio.h"

/*
 * exported plugin catalog entries
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_DEMUX, 22, "ac3", XINE_VERSION_CODE, NULL, demux_ac3_init_plugin },
  { PLUGIN_DEMUX, 22, "aud", XINE_VERSION_CODE, NULL, demux_aud_init_plugin },
  { PLUGIN_DEMUX, 22, "aiff", XINE_VERSION_CODE, NULL, demux_aiff_init_plugin },
  { PLUGIN_DEMUX, 22, "cdda", XINE_VERSION_CODE, NULL, demux_cdda_init_plugin },
  { PLUGIN_DEMUX, 22, "mp3", XINE_VERSION_CODE, NULL, demux_mpgaudio_init_class },
  { PLUGIN_DEMUX, 22, "nsf", XINE_VERSION_CODE, NULL, demux_nsf_init_plugin },
  { PLUGIN_DEMUX, 22, "realaudio", XINE_VERSION_CODE, NULL, demux_realaudio_init_plugin },
  { PLUGIN_DEMUX, 22, "snd", XINE_VERSION_CODE, NULL, demux_snd_init_plugin },
  { PLUGIN_DEMUX, 22, "voc", XINE_VERSION_CODE, NULL, demux_voc_init_plugin },
  { PLUGIN_DEMUX, 22, "vox", XINE_VERSION_CODE, NULL, demux_vox_init_plugin },
  { PLUGIN_DEMUX, 22, "wav", XINE_VERSION_CODE, NULL, demux_wav_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
