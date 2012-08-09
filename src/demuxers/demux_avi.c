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
 */

/*
 * $Id: demux_avi.c,v 1.169 2003/10/07 19:42:00 hadess Exp $
 *
 * demultiplexer for avi streams
 *
 * part of the code is taken from
 * avilib (C) 1999 Rainer Johanni <Rainer@Johanni.de>
 */

/*
 * Ian Goldberg <ian@cypherpunks.ca> modified this code so that it can
 * handle "streaming" AVI files.  By that I mean real seekable files, but
 * ones that are growing as we're displaying them.  Examples include
 * AVI's you're downloading, or ones you're writing in real time using
 * xawtv streamer, or whatever.  This latter is really useful, for
 * example,  for doing the PVR trick of starting the streamer to record
 * TV, starting to watch it ~10 minutes later, and skipping the
 * commercials to catch up to real time.  If you accidentally hit the
 * end of the stream, just hit your "back 15 seconds" key, and all is
 * good.
 *
 * Theory of operation: the video and audio indices have been separated
 * out of the main avi_t and avi_audio_t structures into separate
 * structures that can grow during playback.  We use the idx_grow_t
 * structure to keep track of the offset into the AVI file where we
 * expect to find the next A/V frame.  We periodically check if we can
 * read data from the file at that offset.  If we can, we append index
 * data for as many frames as we can read at the time.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

/********** logging **********/
#define LOG_MODULE "demux_avi"
#define LOG_VERBOSE
/*
#define LOG 
*/
#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"

/*
 * stolen from wine headers
 */

#define AVIIF_KEYFRAME      0x00000010L

#define MAX_AUDIO_STREAMS 8

#define NUM_PREVIEW_BUFFERS 10

/* The following variable indicates the kind of error */

typedef struct{
  off_t  pos;
  long   len;
  long   flags;
} video_index_entry_t;

typedef struct{
  off_t  pos;
  long   len;
  off_t  tot;
  long   block_no;          /* block number, used compute pts in audio VBR streams */
} audio_index_entry_t;

/* These next three are the video and audio structures that can grow
 * during the playback of a streaming file. */

typedef struct{
  long   video_frames;      /* Number of video frames */
  long   alloc_frames;      /* Allocated number of frames */
  video_index_entry_t   *vindex;
} video_index_t;

typedef struct{
  long   audio_chunks;      /* Chunks of audio data in the file */
  long   alloc_chunks;      /* Allocated number of chunks */
  audio_index_entry_t   *aindex;
} audio_index_t;

typedef struct{
  off_t  nexttagoffset;     /* The offset into the AVI file where we expect */
                            /* to find the next A/V frame */
} idx_grow_t;


typedef struct{
  long   dwInitialFrames;
  long   dwScale;
  long   dwRate;
  long   dwStart;
  long   dwSampleSize;

  long   block_no;

  uint32_t audio_type;      /* BUF_AUDIO_xxx type */

  long   audio_strn;        /* Audio stream number */
  char   audio_tag[4];      /* Tag of audio data */
  long   audio_posc;        /* Audio position: chunk */
  long   audio_posb;        /* Audio position: byte within chunk */


  xine_waveformatex *wavex;
  int    wavex_len;

  audio_index_t  audio_idx;

  off_t   audio_tot;         /* Total number of audio bytes */

} avi_audio_t;

typedef struct{
  long   width;             /* Width  of a video frame */
  long   height;            /* Height of a video frame */
  long   dwInitialFrames;
  long   dwScale;
  long   dwRate;
  long   dwStart;
  double fps;               /* Frames per second */

  uint32_t compressor;      /* Type of compressor */
  long   video_strn;        /* Video stream number */
  char   video_tag[4];      /* Tag of video data */
  long   video_posf;        /* Number of next frame to be read
			       (if index present) */
  long   video_posb;        /* Video position: byte within frame */


  avi_audio_t		*audio[MAX_AUDIO_STREAMS];
  int			n_audio;

  uint32_t video_type;      /* BUF_VIDEO_xxx type */

  long                   n_idx;    /* number of index entries actually filled */
  long                   max_idx;  /* number of index entries actually allocated */
  unsigned char        (*idx)[16]; /* index entries (AVI idx1 tag) */
  video_index_t          video_idx;
  xine_bmiheader        *bih;
  off_t                  movi_start;

  int                    palette_count;
  palette_entry_t        palette[256];
} avi_t;

typedef struct demux_avi_s {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *audio_fifo;
  fifo_buffer_t       *video_fifo;
  input_plugin_t      *input;
  int                  status;

  avi_t               *avi;

  int                  no_audio;

  uint32_t             video_step;
  uint32_t             AVI_errno;

  idx_grow_t           idx_grow;

  int                  streaming;
  int                  last_index_entry_type;
  int                  has_index;
} demux_avi_t ;

typedef struct {
  demux_class_t     demux_class;
} demux_avi_class_t;

#define AVI_ERR_SIZELIM      1     /* The write of the data would exceed
                                      the maximum size of the AVI file.
                                      This is more a warning than an error
                                      since the file may be closed safely */

#define AVI_ERR_OPEN         2     /* Error opening the AVI file - wrong path
                                      name or file nor readable/writable */

#define AVI_ERR_READ         3     /* Error reading from AVI File */

#define AVI_ERR_WRITE        4     /* Error writing to AVI File,
                                      disk full ??? */

#define AVI_ERR_WRITE_INDEX  5     /* Could not write index to AVI file
                                      during close, file may still be
                                      usable */

#define AVI_ERR_CLOSE        6     /* Could not write header to AVI file
                                      or not truncate the file during close,
                                      file is most probably corrupted */

#define AVI_ERR_NOT_PERM     7     /* Operation not permitted:
                                      trying to read from a file open
                                      for writing or vice versa */

#define AVI_ERR_NO_MEM       8     /* malloc failed */

#define AVI_ERR_NO_AVI       9     /* Not an AVI file */

#define AVI_ERR_NO_HDRL     10     /* AVI file has no header list,
                                      corrupted ??? */

#define AVI_ERR_NO_MOVI     11     /* AVI file has no MOVI list,
                                      corrupted ??? */

#define AVI_ERR_NO_VIDS     12     /* AVI file contains no video data */

#define AVI_ERR_NO_IDX      13     /* The file has been opened with
                                      getIndex==0, but an operation has been
                                      performed that needs an index */

#define AVI_HEADER_UNKNOWN  -1
#define AVI_HEADER_AUDIO     0
#define AVI_HEADER_VIDEO     1


/* Append an index entry for a newly-found video frame */
static int video_index_append(avi_t *AVI, off_t pos, long len, long flags) {
  video_index_t *vit = &(AVI->video_idx);

  /* Make sure there's room */
  if (vit->video_frames == vit->alloc_frames) {
    long newalloc = vit->alloc_frames + 4096;
    video_index_entry_t *newindex =
      realloc(vit->vindex, newalloc*sizeof(video_index_entry_t));
    if (!newindex) return -1;
    vit->vindex = newindex;
    vit->alloc_frames = newalloc;
  }

  /* Set the new index entry */
  vit->vindex[vit->video_frames].pos = pos;
  vit->vindex[vit->video_frames].len = len;
  vit->vindex[vit->video_frames].flags = flags;
  vit->video_frames += 1;

  return 0;
}

/* Append an index entry for a newly-found audio frame */
static int audio_index_append(avi_t *AVI, int stream, off_t pos, long len,
                              off_t tot, long block_no) {
  audio_index_t *ait = &(AVI->audio[stream]->audio_idx);

  /* Make sure there's room */
  if (ait->audio_chunks == ait->alloc_chunks) {
    long newalloc = ait->alloc_chunks + 4096;
    audio_index_entry_t *newindex =
      realloc(ait->aindex, newalloc*sizeof(audio_index_entry_t));
    if (!newindex) return -1;
    ait->aindex = newindex;
    ait->alloc_chunks = newalloc;
  }

  /* Set the new index entry */
  ait->aindex[ait->audio_chunks].pos      = pos;
  ait->aindex[ait->audio_chunks].len      = len;
  ait->aindex[ait->audio_chunks].tot      = tot;
  ait->aindex[ait->audio_chunks].block_no = block_no;
  ait->audio_chunks += 1;

  return 0;
}

static unsigned long str2ulong(unsigned char *str) {
  return ( str[0] | (str[1]<<8) | (str[2]<<16) | (str[3]<<24) );
}

#if 0
static unsigned long str2ushort(unsigned char *str) {
  return ( str[0] | (str[1]<<8) );
}

static void long2str(unsigned char *dst, int n) {
  dst[0] = (n    )&0xff;
  dst[1] = (n>> 8)&0xff;
  dst[2] = (n>>16)&0xff;
  dst[3] = (n>>24)&0xff;
}
#endif

#define PAD_EVEN(x) ( ((x)+1) & ~1 )

static int64_t get_audio_pts (demux_avi_t *this, int track, long posc,
			      off_t postot, long posb) {

  avi_audio_t *at = this->avi->audio[track];

  lprintf("get_audio_pts: track=%d, posc=%ld, postot=%lld, posb=%ld\n", track, posc, postot, posb);

  if ((at->dwSampleSize == 0) && (at->dwScale > 1)) {
    /* variable bitrate */
    lprintf("get_audio_pts: VBR: dwScale=%ld, dwRate=%ld\n", at->dwScale, at->dwRate);
    return (int64_t)(90000.0 * (double)(posc) *
      (double)at->dwScale / (double)at->dwRate);
  } else {
    /* constant bitrate */
    lprintf("get_audio_pts: CBR: nBlockAlign=%d, dwSampleSize=%ld\n",
            at->wavex->nBlockAlign, at->dwSampleSize);
    if( at->wavex && at->wavex->nBlockAlign ) {
      return (int64_t)((double)(postot + posb) / (double)at->wavex->nBlockAlign *
        (double)at->dwScale / (double)at->dwRate * 90000.0);
    } else {
      return (int64_t)((double)(postot + posb) / (double)at->dwSampleSize *
        (double)at->dwScale / (double)at->dwRate * 90000.0);
    }
  }
}

static int64_t get_video_pts (demux_avi_t *this, long pos) {
  lprintf("get_video_pts: dwScale=%ld, dwRate=%ld, pos=%ld\n",
         this->avi->dwScale, this->avi->dwRate, pos);
  return (int64_t)(90000.0 * (double)pos *
    (double)this->avi->dwScale / (double)this->avi->dwRate);
}

/* Some handy stopper tests for idx_grow, below. */

/* Use this one to ensure the current video frame is in the index. */
static long video_pos_stopper(demux_avi_t *this, void *data){
  if (this->avi->video_posf >= this->avi->video_idx.video_frames) {
    return -1;
  }
  return 1;
}

/* Use this one to ensure the current audio chunk is in the index. */
static long audio_pos_stopper(demux_avi_t *this, void *data) {
  avi_audio_t *AVI_A = (avi_audio_t *)data;

  if (AVI_A->audio_posc >= AVI_A->audio_idx.audio_chunks) {
    return -1;
  }
  return 1;
}

/* Use this one to ensure that a video frame with the given position
 * is in the index. */
static long start_pos_stopper(demux_avi_t *this, void *data) {
  off_t start_pos = *(off_t *)data;
  long maxframe = this->avi->video_idx.video_frames - 1;

  while( maxframe >= 0 && this->avi->video_idx.vindex[maxframe].pos >= start_pos ) {
    if ( this->avi->video_idx.vindex[maxframe].flags & AVIIF_KEYFRAME )
      return 1;
    maxframe--;
  }
  return -1;
}

/* Use this one to ensure that a video frame with the given timestamp
 * is in the index. */
static long start_time_stopper(demux_avi_t *this, void *data) {
  int64_t video_pts = *(int64_t *)data;
  long maxframe = this->avi->video_idx.video_frames - 1;

  while( maxframe >= 0 && get_video_pts(this,maxframe) >= video_pts ) {
    if ( this->avi->video_idx.vindex[maxframe].flags & AVIIF_KEYFRAME )
      return 1;
    maxframe--;
  }

  return -1;
}

/* This is called periodically to check if there's more file now than
 * there was before.  If there is, we constuct the index for (just) the
 * new part, and append it to the index we've got so far.  We stop
 * slurping in the new part when stopper(this, stopdata) returns a
 * non-negative value, or there's no more file to read.  If we're taking
 * a long time slurping in the new part, use the on-screen display to
 * notify the user.  Returns -1 if EOF was reached, the non-negative
 * return value of stopper otherwise. */
static long idx_grow(demux_avi_t *this, long (*stopper)(demux_avi_t *, void *),
        void *stopdata) {

  unsigned long n;
  long          i;
  long          retval = -1;
  long          num_read = 0;
  off_t         ioff = 8;
  uint8_t       data[256];
  uint8_t       data2[4];
  off_t         savepos = this->input->seek(this->input, 0, SEEK_CUR);
  off_t         curtagoffset;
  int           sent_event = 0;

  this->input->seek(this->input, this->idx_grow.nexttagoffset, SEEK_SET);
  curtagoffset = this->idx_grow.nexttagoffset;

  while ((retval = stopper(this, stopdata)) < 0) {

    num_read += 1;

    if (num_read % 1000 == 0) {
      /* send event to frontend about index generation progress */

      xine_event_t             event;
      xine_progress_data_t     prg;
      off_t                    file_len;

      file_len = this->input->get_length (this->input);

      prg.description = _("Restoring index...");
      prg.percent = 100 * this->idx_grow.nexttagoffset / file_len;

      event.type = XINE_EVENT_PROGRESS;
      event.data = &prg;
      event.data_length = sizeof (xine_progress_data_t);

      xine_event_send (this->stream, &event);

      sent_event = 1;
    }

    if (this->input->read(this->input, data, 8) != 8)
      break;

    /* Dive into RIFF and LIST entries */
    if(strncasecmp(data, "LIST", 4) == 0 ||
        strncasecmp(data, "RIFF", 4) == 0) {
      this->idx_grow.nexttagoffset =
        this->input->seek(this->input, 4,SEEK_CUR);
      continue;
    }

    n = str2ulong(data+4);
    this->idx_grow.nexttagoffset += PAD_EVEN(n + 8);

    /* Check if we got a tag ##db, ##dc or ##wb */

    if ((data[0] == this->avi->video_tag[0]) &&
        (data[1] == this->avi->video_tag[1])) {
      long flags = AVIIF_KEYFRAME;
      off_t pos = curtagoffset + ioff;
      long len = n;
      uint32_t tmp;

      /* FIXME:
       *   UGLY hack to detect a keyframe parsing decoder data
       *   AVI chuncks doesn't provide this info and we need it during
       *   index building
       *   this hack comes from mplayer (aviheader.c)
       *   i've added XVID which looks like iso mpeg 4
       */

      if (this->input->read(this->input, data2, 4) != 4)
        break;
      tmp = data2[3] | (data2[2]<<8) | (data2[1]<<16) | (data2[0]<<24);
      switch(this->avi->video_type) {
        case BUF_VIDEO_MSMPEG4_V1:
          this->input->read(this->input, data2, 4);
          tmp = data2[3] | (data2[2]<<8) | (data2[1]<<16) | (data2[0]<<24);
          tmp = tmp << 5;
        case BUF_VIDEO_MSMPEG4_V2:
        case BUF_VIDEO_MSMPEG4_V3:
          if (tmp & 0x40000000) flags = 0;
          break;
        case BUF_VIDEO_DIVX5:
        case BUF_VIDEO_MPEG4:
        case BUF_VIDEO_XVID:
          if (tmp == 0x000001B6) flags = 0;
          break;
      }

      if (video_index_append(this->avi, pos, len, flags) == -1) {
        /* If we're out of memory, we just don't grow the index, but
         * nothing really bad happens. */
      }
    }
    for(i=0; i < this->avi->n_audio; ++i) {
      avi_audio_t *audio = this->avi->audio[i];

      if ((data[0] == audio->audio_tag[0]) &&
          (data[1] == audio->audio_tag[1])) {
        off_t pos = curtagoffset + ioff;
        long len = n;

        /* VBR streams (hack from mplayer) */
        if (audio->wavex && audio->wavex->nBlockAlign) {
          audio->block_no += (len + audio->wavex->nBlockAlign - 1) / audio->wavex->nBlockAlign;
        } else {
          audio->block_no += 1;
        }

        if (audio_index_append(this->avi, i, pos, len, audio->audio_tot,
                               audio->block_no) == -1) {
          /* As above. */
        }
        this->avi->audio[i]->audio_tot += len;
      }
    }

    curtagoffset = this->input->seek(this->input, this->idx_grow.nexttagoffset, SEEK_SET);
    if (curtagoffset != this->idx_grow.nexttagoffset)
      break;
  }

  if (sent_event == 1) {
    /* send event to frontend about index generation progress */
    xine_event_t             event;
    xine_progress_data_t     prg;

    prg.description = _("Restoring index...");
    prg.percent = 100;

    event.type = XINE_EVENT_PROGRESS;
    event.data = &prg;
    event.data_length = sizeof (xine_progress_data_t);

    xine_event_send (this->stream, &event);
  }

  this->input->seek (this->input, savepos, SEEK_SET);

  if (retval < 0) retval = -1;
  return retval;
}

/* Fetch the current video index entry, growing the index if necessary. */
static video_index_entry_t *video_cur_index_entry(demux_avi_t *this) {
  avi_t *AVI = this->avi;

  if (AVI->video_posf >= AVI->video_idx.video_frames) {
    /* We don't have enough frames; see if the file's bigger yet. */
    if (idx_grow(this, video_pos_stopper, NULL) < 0) {
      /* We still don't have enough frames.  Oh, well. */
      return NULL;
    }
  }
  return &(AVI->video_idx.vindex[AVI->video_posf]);
}

/* Fetch the current audio index entry, growing the index if necessary. */
static audio_index_entry_t *audio_cur_index_entry(demux_avi_t *this,
    avi_audio_t *AVI_A) {

  if (AVI_A->audio_posc >= AVI_A->audio_idx.audio_chunks) {
    /* We don't have enough chunks; see if the file's bigger yet. */
    if (idx_grow(this, audio_pos_stopper, AVI_A) < 0) {
      /* We still don't have enough chunks.  Oh, well. */
      return NULL;
    }
  }
  return &(AVI_A->audio_idx.aindex[AVI_A->audio_posc]);
}

static void AVI_close(avi_t *AVI){
  int i;

  if(AVI->idx) free(AVI->idx);
  if(AVI->video_idx.vindex) free(AVI->video_idx.vindex);
  if(AVI->bih) free(AVI->bih);

  for(i=0; i<AVI->n_audio; i++) {
    if(AVI->audio[i]->audio_idx.aindex) free(AVI->audio[i]->audio_idx.aindex);
    if(AVI->audio[i]->wavex) free(AVI->audio[i]->wavex);
    free(AVI->audio[i]);
  }
  free(AVI);
}

#define ERR_EXIT(x)	\
do {			\
   this->AVI_errno = x; \
   free (AVI);  \
   return 0;		\
} while(0)

#if 0
static int avi_sampsize(avi_t *AVI, int track) {
  int s;
  s = ((AVI->audio[track]->wavex->wBitsPerSample+7)/8)*
        AVI->audio[track]->wavex->nChannels;
  if (s==0)
    s=1; /* avoid possible zero divisions */
  return s;
}

static int avi_add_index_entry(demux_avi_t *this, avi_t *AVI, unsigned char *tag,
                               long flags, long pos, long len) {
  void *ptr;

  if(AVI->n_idx>=AVI->max_idx) {
    ptr = realloc((void *)AVI->idx,(AVI->max_idx+4096)*16);
    if(ptr == 0) {
      this->AVI_errno = AVI_ERR_NO_MEM;
      return -1;
    }
    AVI->max_idx += 4096;
    AVI->idx = (unsigned char((*)[16]) ) ptr;
  }

  /* Add index entry */

  memcpy(AVI->idx[AVI->n_idx],tag,4);
  long2str(AVI->idx[AVI->n_idx]+ 4,flags);
  long2str(AVI->idx[AVI->n_idx]+ 8,pos);
  long2str(AVI->idx[AVI->n_idx]+12,len);

  /* Update counter */

  AVI->n_idx++;

  return 0;
}
#endif

static avi_t *AVI_init(demux_avi_t *this) {

  avi_t *AVI;
  long i, j, n, idx_type;
  uint8_t *hdrl_data;
  long hdrl_len=0;
  off_t ioff;
  int lasttag = 0;
  int vids_strh_seen = 0;
  int vids_strf_seen = 0;
  int auds_strh_seen = 0;
  int auds_strf_seen = 0;
  int num_stream = 0;
  uint8_t data[256];

  /* Create avi_t structure */
  lprintf("start\n");

  AVI = (avi_t *) xine_xmalloc(sizeof(avi_t));
  if(AVI==NULL) {
    this->AVI_errno = AVI_ERR_NO_MEM;
    return 0;
  }
  memset((void *)AVI,0,sizeof(avi_t));

  /* Read first 12 bytes and check that this is an AVI file */
  if (!this->streaming)
    this->input->seek(this->input, 0, SEEK_SET);

  if( this->input->read(this->input, data,12) != 12 ) ERR_EXIT(AVI_ERR_READ) ;

  if( strncasecmp(data  ,"RIFF",4) !=0 ||
      strncasecmp(data+8,"AVI ",4) !=0 )
    ERR_EXIT(AVI_ERR_NO_AVI) ;
  /* Go through the AVI file and extract the header list,
     the start position of the 'movi' list and an optionally
     present idx1 tag */

  hdrl_data = NULL;

  while(1) {

    /* Keep track of the last place we tried to read something. */
    this->idx_grow.nexttagoffset = this->input->get_current_pos(this->input);

    if (this->input->read(this->input, data,8) != 8 )
      break; /* We assume it's EOF */

    n = str2ulong(data+4);
    n = PAD_EVEN(n);

    if(strncasecmp(data,"LIST",4) == 0) {
      if( this->input->read(this->input, data,4) != 4 ) ERR_EXIT(AVI_ERR_READ);
      n -= 4;

      if(strncasecmp(data,"hdrl",4) == 0) {

        hdrl_len = n;
        hdrl_data = (unsigned char *) xine_xmalloc(n);
        if(hdrl_data==0)
          ERR_EXIT(AVI_ERR_NO_MEM);
        if (this->input->read(this->input, hdrl_data,n) != n )
          ERR_EXIT(AVI_ERR_READ);

      } else if(strncasecmp(data,"movi",4) == 0)  {

        AVI->movi_start = this->input->get_current_pos(this->input);

        if (this->streaming)
          /* stop reading here, we can't seek back */
          break;
        this->input->seek(this->input, n, SEEK_CUR);
      } else
        this->input->seek(this->input, n, SEEK_CUR);

    } else if(strncasecmp(data,"idx1",4) == 0 ||
              strncasecmp(data,"iddx",4) == 0) {

      /* n must be a multiple of 16, but the reading does not
      break if this is not the case */

      AVI->n_idx = AVI->max_idx = n/16;
      free(AVI->idx);  /* On the off chance there are multiple index chunks */
      AVI->idx = (unsigned  char((*)[16]) ) xine_xmalloc(n);
      if (AVI->idx==0)
        ERR_EXIT(AVI_ERR_NO_MEM);

      if (this->input->read(this->input, (char *)AVI->idx, n) != n ) {
        xine_log (this->stream->xine, XINE_LOG_MSG,
		  _("demux_avi: avi index is broken\n"));
        free (AVI->idx);	/* Index is broken, reconstruct */
        AVI->idx = NULL;
        AVI->n_idx = AVI->max_idx = 0;
        break; /* EOF */
      }

    } else
      this->input->seek(this->input, n, SEEK_CUR);
  }

  /* Interpret the header list */

  for (i=0;i<hdrl_len;) {
    /* List tags are completly ignored */
    lprintf("tag: %c%c%c%c\n",
            hdrl_data[i], hdrl_data[i+1], hdrl_data[i+2], hdrl_data[i+3]);


    if (strncasecmp(hdrl_data+i,"LIST",4)==0) {
      i+= 12;
      continue;
    }

    n = str2ulong(hdrl_data+i+4);
    n = PAD_EVEN(n);

    /* Interpret the tag and its args */

    if(strncasecmp(hdrl_data+i,"strh",4)==0) {
      i += 8;
      lprintf("tag: %c%c%c%c\n",
              hdrl_data[i], hdrl_data[i+1], hdrl_data[i+2], hdrl_data[i+3]);
      if(strncasecmp(hdrl_data+i,"vids",4) == 0 && !vids_strh_seen) {

        AVI->compressor = *(uint32_t *) hdrl_data + i + 4;
        AVI->dwInitialFrames = str2ulong(hdrl_data + i + 16);
        AVI->dwScale         = str2ulong(hdrl_data + i + 20);
        AVI->dwRate          = str2ulong(hdrl_data + i + 24);
        AVI->dwStart         = str2ulong(hdrl_data + i + 28);

        if(AVI->dwScale!=0)
          AVI->fps = (double)AVI->dwRate/(double)AVI->dwScale;

        this->video_step = (long) (90000.0 / AVI->fps);

        AVI->video_strn = num_stream;
        vids_strh_seen = 1;
        lprintf("video stream header\n");
        lasttag = 1; /* vids */
        lprintf("dwScale=%ld, dwRate=%ld, dwInitialFrames=%ld, dwStart=%ld, num_stream=%d\n",
                AVI->dwScale, AVI->dwRate, AVI->dwInitialFrames, AVI->dwStart, num_stream);
      } else if (strncasecmp (hdrl_data+i,"auds",4) ==0 /* && ! auds_strh_seen*/) {
        if(AVI->n_audio < MAX_AUDIO_STREAMS) {
          avi_audio_t *a = (avi_audio_t *) xine_xmalloc(sizeof(avi_audio_t));
          if(a==NULL) {
            this->AVI_errno = AVI_ERR_NO_MEM;
            return 0;
          }
          memset((void *)a,0,sizeof(avi_audio_t));
          AVI->audio[AVI->n_audio] = a;

          a->audio_strn      = num_stream;
          a->dwInitialFrames = str2ulong(hdrl_data + i + 16);
          a->dwScale         = str2ulong(hdrl_data + i + 20);
          a->dwRate          = str2ulong(hdrl_data + i + 24);
          a->dwStart         = str2ulong(hdrl_data + i + 28);
          
          lprintf("dwScale=%ld, dwRate=%ld, dwInitialFrames=%ld, dwStart=%ld, num_stream=%d\n",
                  a->dwScale, a->dwRate, a->dwInitialFrames, a->dwStart, num_stream);
          
          a->dwSampleSize  = str2ulong(hdrl_data+i+44);
	  a->audio_tot     = 0;
          auds_strh_seen = 1;
          lprintf("audio stream header\n");

          lasttag = 2; /* auds */
          AVI->n_audio++;
        }
      } else
        lasttag = 0;
      num_stream++;
    } else if(strncasecmp(hdrl_data+i,"strf",4)==0) {
      i += 8;
      if(lasttag == 1) {
        /* lprintf ("size : %d\n",sizeof(AVI->bih)); */
        AVI->bih = (xine_bmiheader *)
          xine_xmalloc((n < sizeof(xine_bmiheader)) ? sizeof(xine_bmiheader) : n);
        if(AVI->bih == NULL) {
          this->AVI_errno = AVI_ERR_NO_MEM;
          return 0;
        }
        memcpy (AVI->bih, hdrl_data+i, n);
        xine_bmiheader_le2me( AVI->bih );


        /* stream_read(demuxer->stream,(char*) &avi_header.bih,MIN(size2,sizeof(avi_header.bih))); */
        AVI->width  = AVI->bih->biWidth;
        AVI->height = AVI->bih->biHeight;

        /*
          lprintf ("size : %d x %d (%d x %d)\n", AVI->width, AVI->height, AVI->bih.biWidth, AVI->bih.biHeight);
          lprintf ("  biCompression %d='%.4s'\n", AVI->bih.biCompression,
                 &AVI->bih.biCompression);
        */
        lprintf("video stream format\n");

        vids_strf_seen = 1;

        /* load the palette, if there is one */
        AVI->palette_count = AVI->bih->biClrUsed;
        if (AVI->palette_count > 256) {
          lprintf ("number of colors exceeded 256 (%d)", AVI->palette_count);
          AVI->palette_count = 256;
        }
        for (j = 0; j < AVI->palette_count; j++) {
          AVI->palette[j].b = *(hdrl_data + i + sizeof(xine_bmiheader) + j * 4 + 0);
          AVI->palette[j].g = *(hdrl_data + i + sizeof(xine_bmiheader) + j * 4 + 1);
          AVI->palette[j].r = *(hdrl_data + i + sizeof(xine_bmiheader) + j * 4 + 2);
        }

      } else if(lasttag == 2) {

        AVI->audio[AVI->n_audio-1]->wavex=(xine_waveformatex *)malloc(n);
        AVI->audio[AVI->n_audio-1]->wavex_len=n;

        memcpy((void *)AVI->audio[AVI->n_audio-1]->wavex, hdrl_data+i, n);
        xine_waveformatex_le2me( AVI->audio[AVI->n_audio-1]->wavex );
        lprintf("audio stream format\n");
        auds_strf_seen = 1;
      }
      lasttag = 0;
    } else {
      i += 8;
      lasttag = 0;
    }

    i += n;
  }

  if( hdrl_data )
    free( hdrl_data );
  hdrl_data = NULL;

  /* somehow ffmpeg doesn't specify the number of frames here */
  /* if (!vids_strh_seen || !vids_strf_seen || AVI->video_frames==0) */
  if (!vids_strh_seen || !vids_strf_seen)
    ERR_EXIT(AVI_ERR_NO_VIDS);


  AVI->video_tag[0] = AVI->video_strn/10 + '0';
  AVI->video_tag[1] = AVI->video_strn%10 + '0';
  /* do not use the two following bytes */
  AVI->video_tag[2] = 'd';
  AVI->video_tag[3] = 'b';


  for(i = 0; i < AVI->n_audio; i++) {
    /* Audio tag is set to "99wb" if no audio present */
    if(!AVI->audio[i]->wavex->nChannels) AVI->audio[i]->audio_strn = 99;

    AVI->audio[i]->audio_tag[0] = AVI->audio[i]->audio_strn/10 + '0';
    AVI->audio[i]->audio_tag[1] = AVI->audio[i]->audio_strn%10 + '0';
    /* do not use the two following bytes */
    AVI->audio[i]->audio_tag[2] = 'w';
    AVI->audio[i]->audio_tag[3] = 'b';
  }

  idx_type = 0;

  if (!this->streaming) {
    this->input->seek(this->input, AVI->movi_start, SEEK_SET);

    /* if the file has an idx1, check if this is relative
       to the start of the file or to the start of the movi list */

    if(AVI->idx) {
      off_t  pos;
      long   len;

      /* Search the first videoframe in the idx1 and look where
         it is in the file */

      for(i=0;i<AVI->n_idx;i++)
        if( (AVI->idx[i][0] == AVI->video_tag[0]) &&
            (AVI->idx[i][1] == AVI->video_tag[1]))
          break;

#if 0
      /* try again for ##ix */
      if (i>=AVI->n_idx) {
        AVI->video_tag[2] = 'i';
        AVI->video_tag[3] = 'x';
      }

      for(i=0;i<AVI->n_idx;i++)
        if( strncasecmp(AVI->idx[i],AVI->video_tag,3)==0 ) break;
#endif
      if (i>=AVI->n_idx) {
        ERR_EXIT(AVI_ERR_NO_VIDS);
      }

      pos = str2ulong(AVI->idx[i]+ 8);
      len = str2ulong(AVI->idx[i]+12);

      this->input->seek(this->input, pos, SEEK_SET);
      if(this->input->read(this->input, data, 8)!=8) ERR_EXIT(AVI_ERR_READ) ;

      if( strncasecmp(data,AVI->idx[i],4)==0 && str2ulong(data+4)==len ) {
        idx_type = 1; /* Index from start of file */

      } else {

        this->input->seek(this->input, pos+AVI->movi_start-4, SEEK_SET);
        if(this->input->read(this->input, data, 8)!=8)
          ERR_EXIT(AVI_ERR_READ) ;
        if( strncasecmp(data,AVI->idx[i],4)==0 && str2ulong(data+4)==len ) {
          idx_type = 2; /* Index from start of movi list */
        }
      }
      /* idx_type remains 0 if neither of the two tests above succeeds */
    }
  }

  if (idx_type != 0) {
    /* Now generate the video index and audio index arrays from the
     * idx1 record. */
    this->has_index = 1;

    ioff = idx_type == 1 ? 8 : AVI->movi_start+4;

    for(i=0;i<AVI->n_idx;i++) {
      if((AVI->idx[i][0] == AVI->video_tag[0]) &&
         (AVI->idx[i][1] == AVI->video_tag[1])) {
	off_t pos = str2ulong(AVI->idx[i]+ 8)+ioff;
	long len = str2ulong(AVI->idx[i]+12);
	long flags = str2ulong(AVI->idx[i]+4);
	if (video_index_append(AVI, pos, len, flags) == -1) {
	  ERR_EXIT(AVI_ERR_NO_MEM) ;
	}
      }
      for(n = 0; n < AVI->n_audio; n++) {
	avi_audio_t *audio = AVI->audio[n];

	if((AVI->idx[i][0] == AVI->audio[n]->audio_tag[0]) &&
           (AVI->idx[i][1] == AVI->audio[n]->audio_tag[1])) {
	  off_t pos = str2ulong(AVI->idx[i]+ 8)+ioff;
	  long len = str2ulong(AVI->idx[i]+12);

	  /* VBR streams (hack from mplayer) */
	  if (audio->wavex && audio->wavex->nBlockAlign) {
	    audio->block_no += (len + audio->wavex->nBlockAlign - 1) / audio->wavex->nBlockAlign;
	  } else {
	    audio->block_no += 1;
	  }

	  if (audio_index_append(AVI, n, pos, len, audio->audio_tot,
	                         audio->block_no) == -1) {
	    ERR_EXIT(AVI_ERR_NO_MEM) ;
	  }
	  AVI->audio[n]->audio_tot += len;
	}
      }
    }
  } else {
    /* We'll just dynamically grow the index as needed. */
    lprintf("no index\n");
    this->idx_grow.nexttagoffset = AVI->movi_start;
    this->has_index = 0;
  }

  /* Reposition the file */
  if (!this->streaming)
    this->input->seek(this->input, AVI->movi_start, SEEK_SET);
  AVI->video_posf = 0;
  AVI->video_posb = 0;

  lprintf("done, pos=%lld, AVI->movi_start=%lld\n", this->input->get_current_pos(this->input), AVI->movi_start);
  return AVI;
}

static void AVI_seek_start(avi_t *AVI) {
  int i;

  AVI->video_posf = 0;
  AVI->video_posb = 0;

  for(i = 0; i < AVI->n_audio; i++) {
    AVI->audio[i]->audio_posc = 0;
    AVI->audio[i]->audio_posb = 0;
  }
}

static long AVI_read_audio(demux_avi_t *this, avi_audio_t *AVI_A, char *audbuf,
                           long bytes, int *buf_flags) {

  off_t nr, pos, left, todo;
  audio_index_entry_t *aie = audio_cur_index_entry(this, AVI_A);

  if(!aie)  {
    this->AVI_errno = AVI_ERR_NO_IDX;   return -1;
  }

  nr = 0; /* total number of bytes read */

  /* lprintf ("avi audio package len: %d\n", AVI_A->audio_index[AVI_A->audio_posc].len); */


  while(bytes>0) {
    left = aie->len - AVI_A->audio_posb;
    if(left==0) {
      AVI_A->audio_posc++;
      AVI_A->audio_posb = 0;
      aie = audio_cur_index_entry(this, AVI_A);
      if (!aie) {
        this->AVI_errno = AVI_ERR_NO_IDX;
        return -1;
      }
      if (nr>0) {
        *buf_flags = BUF_FLAG_FRAME_END;
        return nr;
      }
      left = aie->len - AVI_A->audio_posb;
    }
    if(bytes<left)
      todo = bytes;
    else
      todo = left;
    pos = aie->pos + AVI_A->audio_posb;
    /* lprintf ("read audio from %lld\n", pos); */
    if (this->input->seek (this->input, pos, SEEK_SET)<0)
      return -1;
    if (this->input->read(this->input, audbuf+nr,todo) != todo) {
      this->AVI_errno = AVI_ERR_READ;
      *buf_flags = 0;
      return -1;
    }
    bytes -= todo;
    nr    += todo;
    AVI_A->audio_posb += todo;
  }

  left = aie->len - AVI_A->audio_posb;
  if (left==0)
    *buf_flags = BUF_FLAG_FRAME_END;
  else
    *buf_flags = 0;

  return nr;
}

static long AVI_read_video(demux_avi_t *this, avi_t *AVI, char *vidbuf,
                           long bytes, int *buf_flags) {

  off_t nr, pos, left, todo;
  video_index_entry_t *vie = video_cur_index_entry(this);

  if (!vie) {
    this->AVI_errno = AVI_ERR_NO_IDX;
    return -1;
  }

  nr = 0; /* total number of bytes read */

  while(bytes>0) {

    left = vie->len - AVI->video_posb;

    if(left==0) {
      AVI->video_posf++;
      AVI->video_posb = 0;
      vie = video_cur_index_entry(this);
      if (!vie) {
        this->AVI_errno = AVI_ERR_NO_IDX;
        return -1;
      }
      if (nr>0) {
        *buf_flags = BUF_FLAG_FRAME_END;
        return nr;
      }
      left = vie->len - AVI->video_posb;
    }
    if(bytes<left)
      todo = bytes;
    else
      todo = left;
    pos = vie->pos + AVI->video_posb;
    /* lprintf ("read video from %lld\n", pos); */
    if (this->input->seek (this->input, pos, SEEK_SET)<0)
      return -1;
    if (this->input->read(this->input, vidbuf+nr,todo) != todo) {
      this->AVI_errno = AVI_ERR_READ;
      *buf_flags = 0;
      return -1;
    }
    bytes -= todo;
    nr    += todo;
    AVI->video_posb += todo;
  }

  left = vie->len - AVI->video_posb;
  if (left==0)
    *buf_flags = BUF_FLAG_FRAME_END;
  else
    *buf_flags = 0;

  return nr;
}


static int demux_avi_next (demux_avi_t *this, int decoder_flags) {

  int            i;
  buf_element_t *buf = NULL;
  int64_t        audio_pts, video_pts;
  int            do_read_video = (this->avi->n_audio == 0);

  /* Try to grow the index, in case more of the avi file has shown up
   * since we last checked.  If it's still too small, well then we're at
   * the end. */
  if (this->avi->video_idx.video_frames <= this->avi->video_posf) {
    if (idx_grow(this, video_pos_stopper, NULL) < 0) {
      return 0;
    }
  }

  for (i=0; i < this->avi->n_audio; i++) {
    if (!this->no_audio && (this->avi->audio[i]->audio_idx.audio_chunks <=
                              this->avi->audio[i]->audio_posc)) {
      if (idx_grow(this, audio_pos_stopper, this->avi->audio[i]) < 0) {
        return 0;
      }
    }
  }

  video_pts = get_video_pts (this, this->avi->video_posf);

  for (i=0; i < this->avi->n_audio; i++) {
    avi_audio_t *audio = this->avi->audio[i];
    audio_index_entry_t *aie = audio_cur_index_entry(this, audio);

    /* The tests above mean aie should never be NULL, but just to be
     * safe. */
    if (!aie) {
      return 0;
    }

    audio_pts =
      get_audio_pts (this, i, aie->block_no, aie->tot, audio->audio_posb);

    lprintf ("video_pts %lld audio_pts %lld\n", video_pts, audio_pts);

    if (!this->no_audio && (audio_pts < video_pts)) {

      if (this->audio_fifo) {
          buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      } else {
          /*
           * no audio:
           * borrow a buffer from video fifo, it immediately gets freed below
           */
          buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      }

      /* read audio */

      buf->pts    = audio_pts;
      buf->size   = AVI_read_audio (this, audio, buf->mem, 2048, &buf->decoder_flags);
      buf->decoder_flags |= decoder_flags;

      if (buf->size<0) {
        buf->free_buffer (buf);
        return 0;
      }

      buf->type = audio->audio_type | i;

      if(this->audio_fifo) {
        this->audio_fifo->put (this->audio_fifo, buf);
      } else {
        buf->free_buffer (buf);
      }
    } else
      do_read_video = 1;
  }

  if (do_read_video) {

    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);

    /* read video */

    buf->pts        = video_pts;
    buf->size       = AVI_read_video (this, this->avi, buf->mem, 2048, &buf->decoder_flags);
    buf->type       = this->avi->video_type;

    buf->extra_info->input_time = video_pts / 90;
    buf->extra_info->input_pos  = this->input->get_current_pos(this->input);

    if (this->has_index) {
      /* use video_frames-2 instead of video_frames-1 to fix problems with weird
         non-interleaved streams */
      buf->extra_info->input_length =
        this->avi->video_idx.vindex[this->avi->video_idx.video_frames - 2].pos;
    }
    buf->extra_info->frame_number = this->avi->video_posf;
    buf->decoder_flags |= decoder_flags;

    if (buf->size<0) {
      buf->free_buffer (buf);
      return 0;
    }

    /*
      lprintf ("adding buf %d to video fifo, decoder_info[0]: %d\n",
      buf, buf->decoder_info[0]);
    */

    this->video_fifo->put (this->video_fifo, buf);
  }

  if( buf ) {
    return (buf->size>0);
  } else {
    return 0;
  }
}

/*
 * Returns next chunk type
 * It's used in streaming mode
 */
static int get_chunk_header(demux_avi_t *this, uint32_t *len, int *audio_stream) {
  long          i;
  char          data[256];

  while (1) {
    if (this->input->read(this->input, data,8) != 8)
      break;
    *len = str2ulong(data+4);

    lprintf("header: %c%c%c%c, pos=%lld, len=%u\n",
            data[0], data[1], data[2], data[3],
            this->input->get_current_pos(this->input), *len);

    /* Dive into RIFF and LIST entries */
    if(strncasecmp(data, "LIST", 4) == 0 ||
       strncasecmp(data, "RIFF", 4) == 0) {
      this->idx_grow.nexttagoffset =
        this->input->seek(this->input, 4,SEEK_CUR);
      continue;
    }

    /*
     * Check if we got a tag ##db, ##dc or ##wb
     * only the 2 first bytes are reliable
     */
    if ((data[0] == this->avi->video_tag[0]) &&
        (data[1] == this->avi->video_tag[1])) {
      lprintf("video header: %c %c %c %c\n", data[0], data[1], data[2], data[3]);
      return AVI_HEADER_VIDEO;
    }

    for(i=0; i < this->avi->n_audio; ++i) {
      /*
       * only the 2 first bytes are reliable
       */
      if ((data[0] == this->avi->audio[i]->audio_tag[0]) &&
          (data[1] == this->avi->audio[i]->audio_tag[1])) {
        *audio_stream = i;
        this->avi->audio[i]->audio_tot += *len;
        lprintf("audio header: %c %c %c %c\n", data[0], data[1], data[2], data[3]);
        return AVI_HEADER_AUDIO;
      }
    }
    lprintf("unknown header: %c %c %c %c\n", data[0], data[1], data[2], data[3]);
    return AVI_HEADER_UNKNOWN;
  }
  return AVI_HEADER_UNKNOWN;
}

/*
 * Read next chunk
 * It's streaming version of demux_avi_next()
 * There is no seeking here.
 */
static int demux_avi_next_streaming (demux_avi_t *this, int decoder_flags) {

  buf_element_t *buf = NULL;
  int64_t        audio_pts, video_pts;
  int64_t        current_pos;
  int            left;
  int            header, chunk_len, audio_stream;
  avi_audio_t   *audio;

  current_pos = this->input->get_current_pos(this->input);
  lprintf("input_pos=%lld\n", current_pos);

  header = get_chunk_header(this, &chunk_len, &audio_stream);

  switch (header) {
    case AVI_HEADER_AUDIO:
      audio = this->avi->audio[audio_stream];
      left = chunk_len;

      lprintf("AVI_HEADER_AUDIO: chunk %ld, len=%d\n", audio->audio_posc, chunk_len);

      while (left > 0) {
        audio_pts =
          get_audio_pts (this, audio_stream, audio->block_no, this->avi->audio[audio_stream]->audio_tot - chunk_len, chunk_len - left);

        if (this->audio_fifo) {
          buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
        } else {
          buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
        }

        /* read audio */
        buf->pts = audio_pts;
        if (left > 2048) {
          buf->size = 2048;
          buf->decoder_flags = 0;
        } else {
          buf->size = left;
          buf->decoder_flags = BUF_FLAG_FRAME_END;
        }
        left -= buf->size;
        if (this->input->read(this->input, buf->mem, buf->size) != buf->size) {
          buf->free_buffer (buf);
          return 0;
        }
        buf->extra_info->input_time = audio_pts / 90;
        buf->extra_info->input_pos  = this->input->get_current_pos(this->input);

        buf->type = audio->audio_type | audio_stream;

        if(this->audio_fifo) {
          this->audio_fifo->put (this->audio_fifo, buf);
        } else {
          buf->free_buffer (buf);
        }
      }
      audio->audio_posc++;

      /* VBR streams (hack from mplayer) */
      if (audio->wavex && audio->wavex->nBlockAlign) {
        audio->block_no += (chunk_len + audio->wavex->nBlockAlign - 1) / audio->wavex->nBlockAlign;
      } else {
        audio->block_no += 1;
      }

      break;


    case AVI_HEADER_VIDEO:
      left = chunk_len;
      lprintf("AVI_HEADER_VIDEO: chunk %ld, len=%d\n", this->avi->video_posf, chunk_len);

      while (left > 0) {
        video_pts = get_video_pts (this, this->avi->video_posf);

        buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);

        /* read video */
        buf->pts = video_pts;
        if (left > 2048) {
          buf->size = 2048;
          buf->decoder_flags = 0;
        } else {
          buf->size = left;
          buf->decoder_flags = BUF_FLAG_FRAME_END;
        }
        left -= buf->size;
        if (this->input->read(this->input, buf->mem, buf->size) != buf->size) {
          buf->free_buffer (buf);
          return 0;
        }

        buf->type       = this->avi->video_type;
        buf->extra_info->input_time = video_pts / 90;
        buf->extra_info->input_pos  = this->input->get_current_pos(this->input);
        buf->extra_info->input_length = buf->extra_info->input_pos;
        buf->extra_info->frame_number = this->avi->video_posf;
        buf->decoder_flags |= decoder_flags;

        this->video_fifo->put (this->video_fifo, buf);
      }

      this->avi->video_posf++;
      break;

    case AVI_HEADER_UNKNOWN:
      lprintf("AVI_HEADER_UNKNOWN: len=%d\n", chunk_len);
      current_pos = this->input->get_current_pos(this->input);
      if (this->input->seek(this->input, chunk_len, SEEK_CUR) != (current_pos + chunk_len)) {
        return 0;
      }
      break;
  }

  /* skip padding */
  this->input->seek (this->input,
    this->input->get_current_pos(this->input) & 1, SEEK_CUR);

  lprintf("done\n");
  return 1;
}

static int demux_avi_send_chunk (demux_plugin_t *this_gen) {
  demux_avi_t   *this = (demux_avi_t *) this_gen;

  if (this->streaming) {
    if (!demux_avi_next_streaming (this, 0)) {
      this->status = DEMUX_FINISHED;
    }
  } else {
    if (!demux_avi_next (this, 0)) {
      this->status = DEMUX_FINISHED;
    }
  }

  return this->status;
}

static void demux_avi_dispose (demux_plugin_t *this_gen) {
  demux_avi_t *this = (demux_avi_t *) this_gen;

  if (this->avi)
    AVI_close (this->avi);

  free(this);
}

static int demux_avi_get_status (demux_plugin_t *this_gen) {
  demux_avi_t *this = (demux_avi_t *) this_gen;

  return this->status;
}

static void demux_avi_send_headers (demux_plugin_t *this_gen) {
  demux_avi_t *this = (demux_avi_t *) this_gen;
  int i;

  lprintf("start\n");
  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH]  = this->avi->width;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = this->avi->height;

  if (this->stream->xine->verbosity >= XINE_VERBOSITY_DEBUG) {
    for (i=0; i < this->avi->n_audio; i++)
      printf ("demux_avi: audio format[%d] = 0x%x\n",
	      i, this->avi->audio[i]->wavex->wFormatTag);
  }
  this->no_audio = 0;

  for(i=0; i < this->avi->n_audio; i++) {
    this->avi->audio[i]->audio_type = formattag_to_buf_audio (this->avi->audio[i]->wavex->wFormatTag);

    if( !this->avi->audio[i]->audio_type ) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
               "unknown audio type 0x%x\n",
	       this->avi->audio[i]->wavex->wFormatTag);
      this->no_audio  = 1;
      this->avi->audio[i]->audio_type     = BUF_AUDIO_UNKNOWN;
    } else
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
               "demux_avi: audio type %s (wFormatTag 0x%x)\n",
               buf_audio_name(this->avi->audio[i]->audio_type),
               (int)this->avi->audio[i]->wavex->wFormatTag);
  }

  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO]  = 1;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = !this->no_audio;

  /*
   * send start/header buffers
   */

  if (!this->stream->demux_thread_running)  {

    buf_element_t  *buf;
    int             i;

    xine_demux_control_start (this->stream);

    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[1] = this->video_step;
    memcpy (buf->content, this->avi->bih, this->avi->bih->biSize);
    buf->size = this->avi->bih->biSize;

    this->avi->video_type = fourcc_to_buf_video(this->avi->bih->biCompression);
    
    if (this->avi->video_type) {
      this->avi->compressor = this->avi->bih->biCompression;
    } else
      this->avi->video_type = fourcc_to_buf_video(this->avi->compressor);

    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_FOURCC] = this->avi->compressor;

    if (!this->avi->video_type) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
               "demux_avi: unknown video codec '%.4s'\n",
               (char*)&this->avi->bih->biCompression);
      this->avi->video_type = BUF_VIDEO_UNKNOWN;
    }
    buf->type = this->avi->video_type;

    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
             "demux_avi: video codec is '%s'\n",
             buf_video_name(buf->type));

    this->video_fifo->put (this->video_fifo, buf);

    /* send off the palette, if there is one */
    if (this->avi->palette_count) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->decoder_flags = BUF_FLAG_SPECIAL;
      buf->decoder_info[1] = BUF_SPECIAL_PALETTE;
      buf->decoder_info[2] = this->avi->palette_count;
      buf->decoder_info_ptr[2] = &this->avi->palette;
      buf->size = 0;
      buf->type = this->avi->video_type;
      this->video_fifo->put (this->video_fifo, buf);
    }

    if (this->audio_fifo) {
      for (i=0; i<this->avi->n_audio; i++) {
        avi_audio_t *a = this->avi->audio[i];

        buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
        buf->decoder_flags = BUF_FLAG_HEADER;
        memcpy (buf->content, a->wavex, a->wavex_len);
        buf->size = a->wavex_len;
        buf->type = a->audio_type | i;
        buf->decoder_info[0] = 0; /* first package, containing wavex */
        buf->decoder_info[1] = a->wavex->nSamplesPerSec; /* Audio Rate */
        buf->decoder_info[2] = a->wavex->wBitsPerSample; /* Audio bits */
        buf->decoder_info[3] = a->wavex->nChannels; /* Audio bits */
        this->audio_fifo->put (this->audio_fifo, buf);
      }

      if(this->avi->n_audio == 1)
        this->stream->stream_info[XINE_STREAM_INFO_AUDIO_FOURCC] = 
          this->avi->audio[0]->wavex->wFormatTag;
    }

    /*
     * send preview buffers
     */

    AVI_seek_start (this->avi);

    if (!this->streaming) {
      for (i=0; i<NUM_PREVIEW_BUFFERS; i++) {
        if (!demux_avi_next(this, BUF_FLAG_PREVIEW))
          break;
      }
    }
    lprintf("done\n");
  }
}

static int demux_avi_seek (demux_plugin_t *this_gen,
                            off_t start_pos, int start_time) {

  demux_avi_t    *this = (demux_avi_t *) this_gen;
  int64_t         video_pts = 0, max_pos, min_pos = 0, cur_pos;
  video_index_entry_t *vie = NULL;
  int64_t         audio_pts;

  this->status = DEMUX_OK;

  if (this->streaming)
    return this->status;

  xine_demux_flush_engine (this->stream);
  AVI_seek_start (this->avi);

  /*
   * seek to start pos / time
   */

  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
           "start pos is %lld, start time is %d\n",start_pos, start_time);

  /* Seek video.  We do a single idx_grow at the beginning rather than
   * incrementally growing the index in a loop, so that if the index
   * grow is going to take a while, the user is notified via the OSD
   * (which only shows up if >= 1000 index entries are added at a time). */

  /* We know for sure the last index entry is past our starting
   * point; find the lowest index entry that's past our starting
   * point. */
  min_pos = 0;

  if (start_pos) {
    idx_grow(this, start_pos_stopper, &start_pos);
  } else if (start_time) {
    video_pts = start_time * 90;
    idx_grow(this, start_time_stopper, &video_pts);
  }

  if (start_pos || start_time)
    max_pos = this->avi->video_idx.video_frames - 1;
  else
    max_pos=0;
  
  cur_pos = this->avi->video_posf;
  if (max_pos < 0) {
    this->status = DEMUX_FINISHED;
    return this->status;
  } else if (start_pos) {
    while (min_pos < max_pos) {
      this->avi->video_posf = cur_pos = (min_pos + max_pos) / 2;
      if (cur_pos == min_pos) break;
      vie = video_cur_index_entry(this);
      if (vie->pos >= start_pos) {
        max_pos = cur_pos;
      } else {
        min_pos = cur_pos;
      }
    }
  } else if (start_time) {
    while (min_pos < max_pos) {
      this->avi->video_posf = cur_pos = (min_pos + max_pos) / 2;
      if (cur_pos == min_pos) break;
      vie = video_cur_index_entry(this);
      if (get_video_pts (this, cur_pos) >= video_pts) {
        max_pos = cur_pos;
      } else {
        min_pos = cur_pos;
      }
    }
  }
  
  while (vie && !(vie->flags & AVIIF_KEYFRAME) && cur_pos) {
    this->avi->video_posf = --cur_pos;
    vie = video_cur_index_entry(this);
  }
  if (!vie || !(vie->flags & AVIIF_KEYFRAME)) {
    lprintf ("No previous keyframe found\n");
  }
  video_pts = get_video_pts (this, cur_pos);

  /* Seek audio.  We can do this incrementally, on the theory that the
   * audio position we're looking for will be pretty close to the video
   * position we've already found, so we won't be seeking though the
   * file much at this point. */

  if (this->stream->xine->verbosity >= XINE_VERBOSITY_DEBUG)
    lprintf ("video_pts = %lld\n", video_pts);

  /* FIXME ? */
  audio_pts = 77777777;

  if (!this->no_audio && this->status == DEMUX_OK) {
    audio_index_entry_t *aie;
    int i;
    for(i=0; i < this->avi->n_audio; i++) {
      max_pos=this->avi->audio[i]->audio_idx.audio_chunks-1;
      min_pos=0;
      while (min_pos < max_pos) {
        cur_pos = this->avi->audio[i]->audio_posc = (max_pos + min_pos) / 2;
        if (cur_pos == min_pos) break;
        aie = audio_cur_index_entry(this, this->avi->audio[i]);
        if (aie) {
          if ( (audio_pts=get_audio_pts(this, i, aie->block_no, aie->tot, 0)) >= video_pts) {
            max_pos = cur_pos;
          } else {
            min_pos = cur_pos;
          }
          lprintf ("audio_pts = %lld %lld < %lld < %lld\n",
                   audio_pts, min_pos, cur_pos, max_pos);
        } else {
          if (cur_pos > min_pos) {
            max_pos = cur_pos;
          } else {
            this->status = DEMUX_FINISHED;
            lprintf ("audio seek to start failed\n");
            break;
          }
        }
      }
      lprintf ("audio_pts = %lld\n", audio_pts);

      /*
       * try to make audio pos more accurate for long index entries
       *
       * yeah, i know this implementation is pathetic (gb)
       */

      if ((audio_pts>video_pts) && (this->avi->audio[i]->audio_posc>0))
	this->avi->audio[i]->audio_posc--;

      aie = audio_cur_index_entry(this, this->avi->audio[i]);
      if (aie) {
	while ((this->avi->audio[i]->audio_posb<aie->len)
	       && ((audio_pts=get_audio_pts(this, i, aie->block_no, aie->tot,
                                            this->avi->audio[i]->audio_posb)) < video_pts))
	  this->avi->audio[i]->audio_posb++;
      }
    }
  }
  lprintf ("video posc: %ld, audio posc: %lld\n", this->avi->video_posf, audio_pts);

  xine_demux_control_newpts (this->stream, video_pts, BUF_FLAG_SEEK);

  return this->status;
}

static int demux_avi_get_stream_length (demux_plugin_t *this_gen) {
  demux_avi_t *this = (demux_avi_t *) this_gen;

  if (this->avi) {
    if (this->streaming) {
      return (int)(get_video_pts(this, this->avi->video_posf) / 90);
    } else {
      return (int)(get_video_pts(this, this->avi->video_idx.video_frames) / 90);
    }
  }

  return 0;
}

static uint32_t demux_avi_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_avi_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
				    input_plugin_t *input) {

  demux_avi_t    *this;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: {
    uint8_t buf[12];

    if (input->get_capabilities(input) & INPUT_CAP_BLOCK)
      return NULL;

    if (xine_demux_read_header(input, buf, 12) != 12)
      return NULL;

    if( strncasecmp(buf  ,"RIFF",4) !=0 ||
        strncasecmp(buf+8,"AVI ",4) !=0 )
      return NULL;
  }
  break;

  case METHOD_BY_EXTENSION: {
    char *extensions, *mrl;

    mrl = input->get_mrl (input);
    extensions = class_gen->get_extensions (class_gen);

    if (!xine_demux_check_extension (mrl, extensions))
      return NULL;
  }
  /* we want to fall through here */

  case METHOD_EXPLICIT:
  break;

  default:
    return NULL;
  }

  this         = xine_xmalloc (sizeof (demux_avi_t));

  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_avi_send_headers;
  this->demux_plugin.send_chunk        = demux_avi_send_chunk;
  this->demux_plugin.seek              = demux_avi_seek;
  this->demux_plugin.dispose           = demux_avi_dispose;
  this->demux_plugin.get_status        = demux_avi_get_status;
  this->demux_plugin.get_stream_length = demux_avi_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_avi_get_capabilities;
  this->demux_plugin.get_optional_data = demux_avi_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  if (!INPUT_IS_SEEKABLE(input)) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "streaming mode\n");
    this->streaming = 1;
  }

  this->avi = AVI_init (this);
  if (!this->avi) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
             "AVI_init failed (AVI_errno: %d)\n", this->AVI_errno);
    free (this);
    return NULL;
  }

  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
           "demux_avi: %ld frames\n", this->avi->video_idx.video_frames);

  return &this->demux_plugin;
}

/*
 * demux avi class
 */

static char *get_description (demux_class_t *this_gen) {
  return "AVI/RIFF demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "AVI";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "avi";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return "video/msvideo: avi: AVI video;"
         "video/x-msvideo: avi: AVI video;";
}

static void class_dispose (demux_class_t *this_gen) {
  demux_avi_class_t *this = (demux_avi_class_t *) this_gen;

  free (this);
}

static void *init_class (xine_t *xine, void *data) {
  demux_avi_class_t     *this;

  this = xine_xmalloc (sizeof (demux_avi_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.get_description = get_description;
  this->demux_class.get_identifier  = get_identifier;
  this->demux_class.get_mimetypes   = get_mimetypes;
  this->demux_class.get_extensions  = get_extensions;
  this->demux_class.dispose         = class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 22, "avi", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
