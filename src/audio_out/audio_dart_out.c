/*
 * Copyright (C) 2002 the xine project
 *
 * This file is part of xine, a unix video player.
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
 * $Id: audio_dart_out.c,v 1.0 Apr 26, 2002 17:34:23 doconnor Exp $
 
audio_dart_out.c, OS/2 DART audio xine driver, by Darwin O'Connor
<doconnor@reamined.on.ca>

 */

#define INCL_NOPMAPI
#define INCL_OS2MM
#define INCL_DOSSEMAPHORES

#include <os2.h>
#include <os2me.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <memory.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "audio_out.h"

#define AO_DART_IFACE_VERSION 7

#define GAP_TOLERANCE         5000
#define GAP_NONRT_TOLERANCE  150000
#define NOT_REAL_TIME           -1

#define MAXBUFFERS 200
#define BUFFERFRAMES 1200

typedef struct {
  audio_driver_class_t driver_class;
  config_values_t *config;
} dart_class_t;

typedef struct dart_driver_s {

  ao_driver_t    ao_driver;

  int            mode;

  int32_t        input_sample_rate;
  double         sample_rate_factor;
  uint32_t       num_channels;
  int            bytes_per_frame;

  uint32_t       frames_in_buffer;     /* number of frames writen to audio hardware   */
  uint32_t maxbuffers;
  uint32_t buffersize;
  uint32_t buffcount;
  MCI_MIX_BUFFER Buffers[MAXBUFFERS];
  ULONG MixHandle;
  PMIXERPROC mixWrite;
  USHORT DeviceID;
  int outbytes;
  int outbuffers;
  int blocking;
  int stopping;
  HEV sem;

    int          convert_u8_s8;        /* Builtin conversion 8-bit UNSIGNED->SIGNED */
} dart_driver_t;

void error(ULONG rc) {
   char errstr[80];

   mciGetErrorString(rc,errstr,80);
   printf("MMOS/2 error: %s\n",errstr);
}

/*
 * open the audio device for writing to
 *
 * Implicit assumptions about audio format (bits/rate/mode):
 *
 * bits == 16: We always get 16-bit samples in native endian format,
 *      using signed linear encoding
 *
 * bits ==  8: 8-bit samples use unsigned linear encoding,
 *      other 8-bit formats (uLaw, aLaw, etc) are currently not supported
 *      by xine
 */
LONG MyEvent (ULONG ulStatus, PMCI_MIX_BUFFER pBuffer, ULONG ulFlags) {
   dart_driver_t *this = (dart_driver_t *) (pBuffer->ulUserParm);
   this->outbytes-=pBuffer->ulBufferLength;
   this->outbuffers--;
   if (this->blocking) {
      DosPostEventSem(this->sem);
      this->blocking=0;
   } 
   return 0;
}

static int ao_dart_open(ao_driver_t *this_gen,
                       uint32_t bits, uint32_t rate, int mode)
{
  dart_driver_t *this = (dart_driver_t *) this_gen;
  ULONG rc;
  MCI_AMP_OPEN_PARMS AmpOpenParms = {NULLHANDLE,0,0,(PSZ)MCI_DEVTYPE_AUDIO_AMPMIX,NULL,NULL,NULL};
  MCI_MIXSETUP_PARMS MixSetupParms = {NULLHANDLE,0,MCI_WAVE_FORMAT_PCM,0,0,MCI_PLAY,MCI_DEVTYPE_WAVEFORM_AUDIO,0,NULL,NULL,MyEvent,NULL,0,0};
  MCI_BUFFER_PARMS BufferParms = {NULLHANDLE,sizeof(MCI_BUFFER_PARMS),0,
                                BUFFERFRAMES,0,0,0,NULL};

/*  printf ("audio_dart_out: ao_dart_open rate=%d, mode=%d\n", rate, mode);*/


/*  if (this->audio_fd >= 0) {

    if ( (mode == this->mode) && (rate == this->input_sample_rate) )
      return this->output_sample_rate;

    close (this->audio_fd);
  }*/

  this->mode                    = mode;
  this->input_sample_rate       = rate;
  this->frames_in_buffer        = 0;

  /*
   * open audio device
   */
   rc = mciSendCommand(0,MCI_OPEN,MCI_WAIT | MCI_OPEN_TYPE_ID
                       ,&AmpOpenParms,0);
   if (ULONG_LOWD(rc)!=MCIERR_SUCCESS) { 
      error(rc);
      return 0;
   }
   this->DeviceID=AmpOpenParms.usDeviceID;
   MixSetupParms.ulBitsPerSample=bits;
   MixSetupParms.ulSamplesPerSec=rate;
   MixSetupParms.ulChannels=mode & AO_CAP_MODE_MONO?1:2;
   rc=mciSendCommand(this->DeviceID,MCI_MIXSETUP,
                     MCI_WAIT | MCI_MIXSETUP_INIT,&MixSetupParms,0);
   if (ULONG_LOWD(rc)!=MCIERR_SUCCESS) {error(rc); return 0;}
   this->MixHandle=MixSetupParms.ulMixHandle;
   this->mixWrite=MixSetupParms.pmixWrite;
   this->num_channels = MixSetupParms.ulChannels;

   this->bytes_per_frame =MixSetupParms.ulChannels*MixSetupParms.ulBitsPerSample/8;
   this->maxbuffers=MAXBUFFERS;
   BufferParms.ulNumBuffers=this->maxbuffers;
   this->buffersize=BUFFERFRAMES;
   BufferParms.ulBufferSize=this->buffersize*this->bytes_per_frame;
   BufferParms.pBufList=this->Buffers;
   rc=mciSendCommand(this->DeviceID,MCI_BUFFER,
                     MCI_WAIT | MCI_ALLOCATE_MEMORY,&BufferParms,0);
   if (ULONG_LOWD(rc)!=MCIERR_SUCCESS) {error(rc); return 0;}
/*  printf ("audio_dart_out: %d channels output\n",this->num_channels);*/
   this->outbytes=0;
   this->outbuffers=0;
   DosCreateEventSem(NULL,&(this->sem),0,TRUE);
  return MixSetupParms.ulSamplesPerSec;
}

static int ao_dart_num_channels(ao_driver_t *this_gen)
{
  dart_driver_t *this = (dart_driver_t *) this_gen;
  return this->num_channels;
}

static int ao_dart_bytes_per_frame(ao_driver_t *this_gen)
{
  dart_driver_t *this = (dart_driver_t *) this_gen;
  return this->bytes_per_frame;
}

static int ao_dart_delay(ao_driver_t *this_gen)
{
  dart_driver_t *this = (dart_driver_t *) this_gen;
  return this->outbytes / this->bytes_per_frame;
}

static int ao_dart_get_gap_tolerance (ao_driver_t *this_gen)
{
  return GAP_TOLERANCE;
}

 /* Write audio samples
  * num_frames is the number of audio frames present
  * audio frames are equivalent one sample on each channel.
  * I.E. Stereo 16 bits audio frames are 4 bytes.
  */
static int ao_dart_write(ao_driver_t *this_gen,
                               int16_t* frame_buffer, uint32_t num_frames)
{
  dart_driver_t *this = (dart_driver_t *) this_gen;
  ULONG numposted;
  int num_written;
  int done=0;

  this->stopping=0;
  printf("Frames recieved: %d\n",num_frames);
/*  printf("Stop Reset\n");*/
 do {
  if (num_frames>this->buffersize) {
     num_written=this->buffersize*this->bytes_per_frame;
  } else {
     num_written=num_frames*this->bytes_per_frame;
  } /* endif */
   if (this->outbuffers>=this->maxbuffers) {
      DosResetEventSem(this->sem,&numposted);
      this->blocking=1;
      DosWaitEventSem(this->sem,-1);
   } 
   memcpy(this->Buffers[this->buffcount].pBuffer,frame_buffer,num_written);
   this->Buffers[this->buffcount].ulBufferLength=num_written;
   this->Buffers[this->buffcount].ulUserParm=(ULONG)this;
   if (this->stopping) {
/*      printf("Aborting\n");*/
      return 1;
   }
   this->mixWrite(this->MixHandle,&(this->Buffers[(this->buffcount)++]),1);
   if (this->buffcount>=this->maxbuffers) this->buffcount=0;
   this->outbytes+=num_written;
   this->outbuffers++;
   done=(num_frames<=this->buffersize);
   frame_buffer+=num_written;
   num_frames-=this->buffersize;
 } while (!done); /* enddo */
/*   printf("Write Done\n");*/
   return 1;
}

void closethread(ULONG DeviceID) {
   MCI_GENERIC_PARMS GenericParms;

   mciSendCommand(DeviceID,MCI_CLOSE,MCI_WAIT,&GenericParms,0);
}

static void ao_dart_close(ao_driver_t *this_gen)
{
  dart_driver_t *this = (dart_driver_t *) this_gen;
   MCI_GENERIC_PARMS GenericParms;
   ULONG rc;
MCI_BUFFER_PARMS BufferParms = {NULLHANDLE,sizeof(MCI_BUFFER_PARMS),0,
                                BUFFERFRAMES,0,0,0,NULL};

   BufferParms.ulNumBuffers=this->maxbuffers;
   BufferParms.ulBufferSize=this->buffersize*this->bytes_per_frame;
   BufferParms.pBufList=this->Buffers;
   rc=mciSendCommand(this->DeviceID,MCI_BUFFER,
                     MCI_WAIT | MCI_DEALLOCATE_MEMORY,&BufferParms,0);
   if (ULONG_LOWD(rc)!=MCIERR_SUCCESS) error(rc);
   _beginthread(closethread,NULL,32768,(ULONG)(this->DeviceID));
/*Can't close AMPMIX while closing XINEMCD. MMOS/2 doesn't allow it. Create thread to close it after XINEMCD closed*/
/*   rc=mciSendCommand(this->DeviceID,MCI_CLOSE,MCI_WAIT,&GenericParms,0);
   if (ULONG_LOWD(rc)!=MCIERR_SUCCESS) error(rc);*/
}

static uint32_t ao_dart_get_capabilities (ao_driver_t *this_gen) {
  return AO_CAP_MODE_MONO | AO_CAP_MODE_STEREO | AO_CAP_8BITS/* | AO_CAP_MODE_PCM_VOL | AO_CAP_MODE_MUTE_VOL*/;
}

static void ao_dart_exit(ao_driver_t *this_gen)
{
  dart_driver_t *this = (dart_driver_t *) this_gen;

  free (this);
}

/*
 * Get a property of audio driver.
 * return 1 in success, 0 on failure. (and the property value?)
 */
static int ao_dart_get_property (ao_driver_t *this_gen, int property) {
  dart_driver_t *this = (dart_driver_t *) this_gen;
  return 0;
}

/*
 * Set a property of audio driver.
 * return value on success, ~value on failure
 */
static int ao_dart_set_property (ao_driver_t *this_gen, int property, int value) {
  dart_driver_t *this = (dart_driver_t *) this_gen;
  return 0;
}

static int ao_dart_control(ao_driver_t *this_gen, int cmd, ...) {
   dart_driver_t *this = (dart_driver_t *) this_gen;
   MCI_GENERIC_PARMS GenericParms;
   ULONG rc;

/*   printf("audio_dart_out.c: ao_dart_control: %d\n",cmd);*/
   switch (cmd) {
   case AO_CTRL_PLAY_PAUSE:
      rc=mciSendCommand(this->DeviceID,MCI_PAUSE,MCI_WAIT,&GenericParms,0);
/*      if (ULONG_LOWD(rc)!=MCIERR_SUCCESS) error(rc);*/
      break;
   case AO_CTRL_PLAY_RESUME:
      rc=mciSendCommand(this->DeviceID,MCI_RESUME,MCI_WAIT,&GenericParms,0);
/*      if (ULONG_LOWD(rc)!=MCIERR_SUCCESS) error(rc);*/
      break;
   case AO_CTRL_FLUSH_BUFFERS:
/*      printf("Stop sent\n");*/
      this->stopping=1;
      rc=mciSendCommand(this->DeviceID,MCI_STOP,MCI_WAIT,&GenericParms,0);
      if (ULONG_LOWD(rc)!=MCIERR_SUCCESS) error(rc);
      this->outbytes=0;
      this->outbuffers=0;
      break;
   } /* endswitch */
   return 0;
}

ao_driver_t *ao_dart_open_plugin (audio_driver_class_t *class_gen, const void *data) {

  dart_driver_t   *this;

  this = (dart_driver_t *) malloc (sizeof (dart_driver_t));


  this->ao_driver.get_capabilities      = ao_dart_get_capabilities;
  this->ao_driver.get_property          = ao_dart_get_property;
  this->ao_driver.set_property          = ao_dart_set_property;
  this->ao_driver.open                  = ao_dart_open;
  this->ao_driver.num_channels          = ao_dart_num_channels;
  this->ao_driver.bytes_per_frame       = ao_dart_bytes_per_frame;
  this->ao_driver.delay                 = ao_dart_delay;
  this->ao_driver.write                 = ao_dart_write;
  this->ao_driver.close                 = ao_dart_close;
  this->ao_driver.exit                  = ao_dart_exit;
  this->ao_driver.get_gap_tolerance     = ao_dart_get_gap_tolerance;
  this->ao_driver.control                = ao_dart_control;

  return &this->ao_driver;
}


/*
 * class functions
 */

static char* ao_dart_get_identifier (audio_driver_class_t *this_gen) {
  return "dart";
}

static char* ao_dart_get_description (audio_driver_class_t *this_gen) {
  return _("xine audio output plugin using OS/2 DART");
}

static void ao_dart_dispose_class (audio_driver_class_t *this_gen) {

  dart_class_t *this = (dart_class_t *) this_gen;

  free (this);
}

static void *ao_dart_init_class (xine_t *xine, void *data) {
  dart_class_t         *this;

  this = (dart_class_t *) malloc (sizeof (dart_class_t));

  this->driver_class.open_plugin     = ao_dart_open_plugin;
  this->driver_class.get_identifier  = ao_dart_get_identifier;
  this->driver_class.get_description = ao_dart_get_description;
  this->driver_class.dispose         = ao_dart_dispose_class;

  this->config = xine->config;

  return this;
}


static ao_info_t ao_info_dart = {
  2
};

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_AUDIO_OUT, AO_DART_IFACE_VERSION, "dart", XINE_VERSION_CODE, &ao_info_dart, ao_dart_init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

