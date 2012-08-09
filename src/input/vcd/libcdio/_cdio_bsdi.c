/*
    $Id: _cdio_bsdi.c,v 1.1 2003/10/13 11:47:11 f1rmb Exp $

    Copyright (C) 2001 Herbert Valerio Riedel <hvr@gnu.org>
    Copyright (C) 2002,2003 Rocky Bernstein <rocky@panix.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* This file contains BSDI-specific code and implements low-level 
   control of the CD drive.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

static const char _rcsid[] = "$Id: _cdio_bsdi.c,v 1.1 2003/10/13 11:47:11 f1rmb Exp $";

#include <cdio/sector.h>
#include <cdio/util.h>
#include "cdio_assert.h"
#include "cdio_private.h"

#define DEFAULT_CDIO_DEVICE "/dev/rsr0c"
#include <string.h>

#ifdef HAVE_BSDI_CDROM

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

/*#define USE_ETC_FSTAB*/
#ifdef USE_ETC_FSTAB
#include <fstab.h>
#endif

#include <dvd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#define TOTAL_TRACKS    (_obj->tochdr.cdth_trk1)
#define FIRST_TRACK_NUM (_obj->tochdr.cdth_trk0)

typedef struct {
  /* Things common to all drivers like this. 
     This must be first. */
  generic_img_private_t gen; 

  enum {
    _AM_NONE,
    _AM_IOCTL,
  } access_mode;

  char *source_name;
  
  bool init;

  /* Track information */
  bool toc_init;                         /* if true, info below is valid. */
  struct cdrom_tochdr    tochdr;
  struct cdrom_tocentry  tocent[100];    /* entry info for each track */

} _img_private_t;

/* Check a drive to see if it is a CD-ROM 
   Return 1 if a CD-ROM. 0 if it exists but isn't a CD-ROM drive
   and -1 if no device exists .
*/
static bool
cdio_is_cdrom(char *drive, char *mnttype)
{
  bool is_cd=false;
  int cdfd;
  struct cdrom_tochdr    tochdr;
  
  /* If it doesn't exist, return -1 */
  if ( !cdio_is_device_quiet_generic(drive) ) {
    return(false);
  }
  
  /* If it does exist, verify that it's an available CD-ROM */
  cdfd = open(drive, (O_RDONLY|O_EXCL|O_NONBLOCK), 0);

  /* Should we want to test the condition in more detail:
     ENOENT is the error for /dev/xxxxx does not exist;
     ENODEV means there's no drive present. */

  if ( cdfd >= 0 ) {
    if ( ioctl(cdfd, CDROMREADTOCHDR, &tochdr) != -1 ) {
      is_cd = true;
    }
    close(cdfd);
    }
  /* Even if we can't read it, it might be mounted */
  else if ( mnttype && (strcmp(mnttype, "cd9660") == 0) ) {
    is_cd = true;
  }
  return(is_cd);
}

/*!
  Initialize CD device.
 */
static bool
_cdio_init (_img_private_t *_obj)
{
  if (_obj->gen.init) {
    cdio_error ("init called more than once");
    return false;
  }
  
  _obj->gen.fd = open (_obj->source_name, O_RDONLY, 0);

  if (_obj->gen.fd < 0)
    {
      cdio_error ("open (%s): %s", _obj->source_name, strerror (errno));
      return false;
    }

  _obj->gen.init = true;
  _obj->toc_init = false;
  return true;
}

/* Read audio sectors
*/
static int
_read_audio_sectors (void *env, void *data, lsn_t lsn,
		     unsigned int nblocks)
{
  char buf[CDIO_CD_FRAMESIZE_RAW] = { 0, };
  struct cdrom_msf *msf = (struct cdrom_msf *) &buf;
  msf_t _msf;

  _img_private_t *_obj = env;

  cdio_lba_to_msf (cdio_lsn_to_lba(lsn), &_msf);
  msf->cdmsf_min0 = from_bcd8(_msf.m);
  msf->cdmsf_sec0 = from_bcd8(_msf.s);
  msf->cdmsf_frame0 = from_bcd8(_msf.f);

  if (_obj->gen.ioctls_debugged == 75)
    cdio_debug ("only displaying every 75th ioctl from now on");

  if (_obj->gen.ioctls_debugged == 30 * 75)
    cdio_debug ("only displaying every 30*75th ioctl from now on");
  
  if (_obj->gen.ioctls_debugged < 75 
      || (_obj->gen.ioctls_debugged < (30 * 75)  
	  && _obj->gen.ioctls_debugged % 75 == 0)
      || _obj->gen.ioctls_debugged % (30 * 75) == 0)
    cdio_debug ("reading %2.2d:%2.2d:%2.2d",
	       msf->cdmsf_min0, msf->cdmsf_sec0, msf->cdmsf_frame0);
  
  _obj->gen.ioctls_debugged++;
 
  switch (_obj->access_mode) {
    case _AM_NONE:
      cdio_error ("no way to read audio");
      return 1;
      break;
      
    case _AM_IOCTL: {
      unsigned int i;
      for (i=0; i < nblocks; i++) {
	if (ioctl (_obj->gen.fd, CDROMREADRAW, &buf) == -1)  {
	  perror ("ioctl()");
	  return 1;
	  /* exit (EXIT_FAILURE); */
	}
	memcpy (((char *)data) + (CDIO_CD_FRAMESIZE_RAW * i), buf, 
		CDIO_CD_FRAMESIZE_RAW);
      }
      break;
    }
  }

  return 0;
}

/*!
   Reads a single mode2 sector from cd device into data starting
   from lsn. Returns 0 if no error. 
 */
static int
_cdio_read_mode2_sector (void *env, void *data, lsn_t lsn, 
		    bool mode2_form2)
{
  char buf[M2RAW_SECTOR_SIZE] = { 0, };
  struct cdrom_msf *msf = (struct cdrom_msf *) &buf;
  msf_t _msf;

  _img_private_t *_obj = env;

  cdio_lba_to_msf (cdio_lsn_to_lba(lsn), &_msf);
  msf->cdmsf_min0 = from_bcd8(_msf.m);
  msf->cdmsf_sec0 = from_bcd8(_msf.s);
  msf->cdmsf_frame0 = from_bcd8(_msf.f);

  if (_obj->gen.ioctls_debugged == 75)
    cdio_debug ("only displaying every 75th ioctl from now on");

  if (_obj->gen.ioctls_debugged == 30 * 75)
    cdio_debug ("only displaying every 30*75th ioctl from now on");
  
  if (_obj->gen.ioctls_debugged < 75 
      || (_obj->gen.ioctls_debugged < (30 * 75)  
	  && _obj->gen.ioctls_debugged % 75 == 0)
      || _obj->gen.ioctls_debugged % (30 * 75) == 0)
    cdio_debug ("reading %2.2d:%2.2d:%2.2d",
	       msf->cdmsf_min0, msf->cdmsf_sec0, msf->cdmsf_frame0);
  
  _obj->gen.ioctls_debugged++;
 
  switch (_obj->access_mode)
    {
    case _AM_NONE:
      cdio_error ("no way to read mode2");
      return 1;
      break;
      
    case _AM_IOCTL:
      if (ioctl (_obj->gen.fd, CDROMREADMODE2, &buf) == -1)
	{
	  perror ("ioctl()");
	  return 1;
	  /* exit (EXIT_FAILURE); */
	}
      break;
    }

  if (mode2_form2)
    memcpy (data, buf, M2RAW_SECTOR_SIZE);
  else
    memcpy (((char *)data), buf + CDIO_CD_SUBHEADER_SIZE, CDIO_CD_FRAMESIZE);
  
  return 0;
}

/*!
   Reads nblocks of mode2 sectors from cd device into data starting
   from lsn.
   Returns 0 if no error. 
 */
static int
_cdio_read_mode2_sectors (void *env, void *data, lsn_t lsn, 
			  bool mode2_form2, unsigned int nblocks)
{
  _img_private_t *_obj = env;
  int i;
  int retval;

  for (i = 0; i < nblocks; i++) {
    if (mode2_form2) {
      if ( (retval = _cdio_read_mode2_sector (_obj, 
					  ((char *)data) + (M2RAW_SECTOR_SIZE * i),
					  lsn + i, true)) )
	return retval;
    } else {
      char buf[M2RAW_SECTOR_SIZE] = { 0, };
      if ( (retval = _cdio_read_mode2_sector (_obj, buf, lsn + i, true)) )
	return retval;
      
      memcpy (((char *)data) + (CDIO_CD_FRAMESIZE * i), 
	      buf + CDIO_CD_SUBHEADER_SIZE, CDIO_CD_FRAMESIZE);
    }
  }
  return 0;
}

/*!
   Return the size of the CD in logical block address (LBA) units.
 */
static uint32_t 
_cdio_stat_size (void *env)
{
  _img_private_t *_obj = env;

  struct cdrom_tocentry tocent;
  uint32_t size;

  tocent.cdte_track = CDIO_CDROM_LEADOUT_TRACK;
  tocent.cdte_format = CDROM_LBA;
  if (ioctl (_obj->gen.fd, CDROMREADTOCENTRY, &tocent) == -1)
    {
      perror ("ioctl(CDROMREADTOCENTRY)");
      exit (EXIT_FAILURE);
    }

  size = tocent.cdte_addr.lba;

  return size;
}

/*!
  Set the key "arg" to "value" in source device.
*/
static int
_cdio_set_arg (void *env, const char key[], const char value[])
{
  _img_private_t *_obj = env;

  if (!strcmp (key, "source"))
    {
      if (!value)
	return -2;

      free (_obj->source_name);
      
      _obj->source_name = strdup (value);
    }
  else if (!strcmp (key, "access-mode"))
    {
      if (!strcmp(value, "IOCTL"))
	_obj->access_mode = _AM_IOCTL;
      else
	cdio_error ("unknown access type: %s. ignored.", value);
    }
  else 
    return -1;

  return 0;
}

/*! 
  Read and cache the CD's Track Table of Contents and track info.
  Return false if successful or true if an error.
*/
static bool
_cdio_read_toc (_img_private_t *_obj) 
{
  int i;

  /* read TOC header */
  if ( ioctl(_obj->gen.fd, CDROMREADTOCHDR, &_obj->tochdr) == -1 ) {
    cdio_error("%s: %s\n", 
            "error in ioctl CDROMREADTOCHDR", strerror(errno));
    return false;
  }

  /* read individual tracks */
  for (i= FIRST_TRACK_NUM; i<=TOTAL_TRACKS; i++) {
    _obj->tocent[i-1].cdte_track = i;
    _obj->tocent[i-1].cdte_format = CDROM_MSF;
    if ( ioctl(_obj->gen.fd, CDROMREADTOCENTRY, &_obj->tocent[i-1]) == -1 ) {
      cdio_error("%s %d: %s\n",
              "error in ioctl CDROMREADTOCENTRY for track", 
              i, strerror(errno));
      return false;
    }
    /****
    struct cdrom_msf0 *msf= &_obj->tocent[i-1].cdte_addr.msf;
    
    fprintf (stdout, "--- track# %d (msf %2.2x:%2.2x:%2.2x)\n",
	     i, msf->minute, msf->second, msf->frame);
    ****/

  }

  /* read the lead-out track */
  _obj->tocent[TOTAL_TRACKS].cdte_track = CDIO_CDROM_LEADOUT_TRACK;
  _obj->tocent[TOTAL_TRACKS].cdte_format = CDROM_MSF;

  if (ioctl(_obj->gen.fd, CDROMREADTOCENTRY, 
	    &_obj->tocent[TOTAL_TRACKS]) == -1 ) {
    cdio_error("%s: %s\n", 
	     "error in ioctl CDROMREADTOCENTRY for lead-out",
            strerror(errno));
    return false;
  }

  /*
  struct cdrom_msf0 *msf= &_obj->tocent[TOTAL_TRACKS].cdte_addr.msf;

  fprintf (stdout, "--- track# %d (msf %2.2x:%2.2x:%2.2x)\n",
	   i, msf->minute, msf->second, msf->frame);
  */

  return true;
}

/*!
  Eject media in CD drive. If successful, as a side effect we 
  also free obj.
 */
static int 
_cdio_eject_media (void *env) {

  _img_private_t *_obj = env;
  int ret=2;
  int status;
  int fd;

  close(_obj->gen.fd);
  _obj->gen.fd = -1;
  if ((fd = open (_obj->source_name, O_RDONLY|O_NONBLOCK)) > -1) {
    if((status = ioctl(fd, CDROM_DRIVE_STATUS, (void *) CDSL_CURRENT)) > 0) {
      switch(status) {
      case CDS_TRAY_OPEN:
	if((ret = ioctl(fd, CDROMCLOSETRAY, 0)) != 0) {
	  cdio_error ("ioctl CDROMCLOSETRAY failed: %s\n", strerror(errno));  
	}
	break;
      case CDS_DISC_OK:
	if((ret = ioctl(fd, CDROMEJECT, 0)) != 0) {
	  cdio_error("ioctl CDROMEJECT failed: %s\n", strerror(errno));  
	}
	break;
      }
      ret=0;
    } else {
      cdio_error ("CDROM_DRIVE_STATUS failed: %s\n", strerror(errno));
      ret=1;
    }
    close(fd);
  }
  return 2;
}

/*!
  Return the value associated with the key "arg".
*/
static const char *
_cdio_get_arg (void *env, const char key[])
{
  _img_private_t *_obj = env;

  if (!strcmp (key, "source")) {
    return _obj->source_name;
  } else if (!strcmp (key, "access-mode")) {
    switch (_obj->access_mode) {
    case _AM_IOCTL:
      return "ioctl";
    case _AM_NONE:
      return "no access method";
    }
  } 
  return NULL;
}

/*!
  Return the number of of the first track. 
  CDIO_INVALID_TRACK is returned on error.
*/
static track_t
_cdio_get_first_track_num(void *env) 
{
  _img_private_t *_obj = env;
  
  if (!_obj->toc_init) _cdio_read_toc (_obj) ;

  return FIRST_TRACK_NUM;
}

/*!
  Return the media catalog number MCN.
  Note: string is malloc'd so caller should free() then returned
  string when done with it.
 */
static char *
_cdio_get_mcn (void *env) {

  struct cdrom_mcn mcn;
  _img_private_t *_obj = env;
  if (ioctl(_obj->gen.fd, CDROM_GET_MCN, &mcn) != 0)
    return NULL;
  return strdup(mcn.medium_catalog_number);
}

/*!
  Return the number of tracks in the current medium.
  CDIO_INVALID_TRACK is returned on error.
*/
static track_t
_cdio_get_num_tracks(void *env) 
{
  _img_private_t *_obj = env;
  
  if (!_obj->toc_init) _cdio_read_toc (_obj) ;

  return TOTAL_TRACKS;
}

/*!  
  Get format of track. 
*/
static track_format_t
_cdio_get_track_format(void *env, track_t track_num) 
{
  _img_private_t *_obj = env;
  
  if (!_obj->toc_init) _cdio_read_toc (_obj) ;

  if (track_num > TOTAL_TRACKS || track_num == 0)
    return TRACK_FORMAT_ERROR;

  /* This is pretty much copied from the "badly broken" cdrom_count_tracks
     in linux/cdrom.c.
   */
  if (_obj->tocent[track_num-1].cdte_ctrl & CDROM_DATA_TRACK) {
    if (_obj->tocent[track_num-1].cdte_format == 0x10)
      return TRACK_FORMAT_CDI;
    else if (_obj->tocent[track_num-1].cdte_format == 0x20) 
      return TRACK_FORMAT_XA;
    else
      return TRACK_FORMAT_DATA;
  } else
    return TRACK_FORMAT_AUDIO;
  
}

/*!
  Return true if we have XA data (green, mode2 form1) or
  XA data (green, mode2 form2). That is track begins:
  sync - header - subheader
  12     4      -  8

  FIXME: there's gotta be a better design for this and get_track_format?
*/
static bool
_cdio_get_track_green(void *env, track_t track_num) 
{
  _img_private_t *_obj = env;
  
  if (!_obj->toc_init) _cdio_read_toc (_obj) ;

  if (track_num == CDIO_CDROM_LEADOUT_TRACK) track_num = TOTAL_TRACKS+1;

  if (track_num > TOTAL_TRACKS+1 || track_num == 0)
    return false;

  /* FIXME: Dunno if this is the right way, but it's what 
     I was using in cdinfo for a while.
   */
  return ((_obj->tocent[track_num-1].cdte_ctrl & 2) != 0);
}

/*!  
  Return the starting MSF (minutes/secs/frames) for track number
  track_num in obj.  Track numbers start at 1.
  The "leadout" track is specified either by
  using track_num LEADOUT_TRACK or the total tracks+1.
  False is returned if there is no track entry.
*/
static bool
_cdio_get_track_msf(void *env, track_t track_num, msf_t *msf)
{
  _img_private_t *_obj = env;

  if (NULL == msf) return false;

  if (!_obj->toc_init) _cdio_read_toc (_obj) ;

  if (track_num == CDIO_CDROM_LEADOUT_TRACK) track_num = TOTAL_TRACKS+1;

  if (track_num > TOTAL_TRACKS+1 || track_num == 0) {
    return false;
  } else {
    struct cdrom_msf0  *msf0= &_obj->tocent[track_num-1].cdte_addr.msf;
    msf->m = to_bcd8(msf0->minute);
    msf->s = to_bcd8(msf0->second);
    msf->f = to_bcd8(msf0->frame);
    return true;
  }
}

#endif /* HAVE_BSDI_CDROM */

/*!
  Return an array of strings giving possible CD devices.
 */
char **
cdio_get_devices_bsdi (void)
{
#ifndef HAVE_BSDI_CDROM
  return NULL;
#else
  char drive[40];
  char **drives = NULL;
  unsigned int num_drives=0;
  bool exists=true;
  char c;
  
  /* Scan the system for CD-ROM drives.
  */

#ifdef USE_ETC_FSTAB

  struct fstab *fs;
  setfsent();
  
  /* Check what's in /etc/fstab... */
  while ( (fs = getfsent()) )
    {
      if (strncmp(fs->fs_spec, "/dev/sr", 7))
	cdio_add_device_list(&drives, fs->fs_spec, &num_drives);
    }
  
#endif

  /* Scan the system for CD-ROM drives.
     Not always 100% reliable, so use the USE_MNTENT code above first.
  */
  for ( c='0'; exists && c <='9'; c++ ) {
    sprintf(drive, "/dev/rsr%cc", c);
    exists = cdio_is_cdrom(drive, NULL);
    if ( exists ) {
      cdio_add_device_list(&drives, drive, &num_drives);
    }
  }
  cdio_add_device_list(&drives, NULL, &num_drives);
  return drives;
#endif /*HAVE_BSDI_CDROM*/
}

/*!
  Return a string containing the default CD device if none is specified.
 */
char *
cdio_get_default_device_bsdi(void)
{
  return strdup(DEFAULT_CDIO_DEVICE);
}

/*!
  Initialization routine. This is the only thing that doesn't
  get called via a function pointer. In fact *we* are the
  ones to set that up.
 */
CdIo *
cdio_open_bsdi (const char *source_name)
{

#ifdef HAVE_BSDI_CDROM
  CdIo *ret;
  _img_private_t *_data;

  cdio_funcs _funcs = {
    .eject_media        = _cdio_eject_media,
    .free               = cdio_generic_free,
    .get_arg            = _cdio_get_arg,
    .get_default_device = cdio_get_default_device_bsdi,
    .get_devices        = cdio_get_devices_bsdi,
    .get_first_track_num= _cdio_get_first_track_num,
    .get_mcn            = _cdio_get_mcn, 
    .get_num_tracks     = _cdio_get_num_tracks,
    .get_track_format   = _cdio_get_track_format,
    .get_track_green    = _cdio_get_track_green,
    .get_track_lba      = NULL, /* This could be implemented if need be. */
    .get_track_msf      = _cdio_get_track_msf,
    .lseek              = cdio_generic_lseek,
    .read               = cdio_generic_read,
    .read_mode2_sector  = _cdio_read_mode2_sector,
    .read_audio_sectors = _read_audio_sectors,
    .read_mode2_sectors = _cdio_read_mode2_sectors,
    .set_arg            = _cdio_set_arg,
    .stat_size          = _cdio_stat_size
  };

  _data                 = _cdio_malloc (sizeof (_img_private_t));
  _data->access_mode    = _AM_IOCTL;
  _data->gen.init       = false;
  _data->gen.fd         = -1;

  _cdio_set_arg(_data, "source", (NULL == source_name) 
		? DEFAULT_CDIO_DEVICE: source_name);

  ret = cdio_new (_data, &_funcs);
  if (ret == NULL) return NULL;

  if (_cdio_init(_data))
    return ret;
  else {
    cdio_generic_free (_data);
    return NULL;
  }
  
#else 
  return NULL;
#endif /* HAVE_BSDI_CDROM */

}

bool
cdio_have_bsdi (void)
{
#ifdef HAVE_BSDI_CDROM
  return true;
#else 
  return false;
#endif /* HAVE_BSDI_CDROM */
}

