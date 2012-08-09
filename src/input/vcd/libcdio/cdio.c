/*
    $Id: cdio.c,v 1.1 2003/10/13 11:47:11 f1rmb Exp $

    Copyright (C) 2003 Rocky Bernstein <rocky@panix.com>
    Copyright (C) 2001 Herbert Valerio Riedel <hvr@gnu.org>

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


#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "cdio_assert.h"
#include <cdio/cdio.h>
#include <cdio/cd_types.h>
#include <cdio/util.h>
#include <cdio/logging.h>
#include "cdio_private.h"

static const char _rcsid[] = "$Id: cdio.c,v 1.1 2003/10/13 11:47:11 f1rmb Exp $";


const char *track_format2str[6] = 
  {
    "audio", "CD-i", "XA", "data", "PSX", "error"
  };

/* The below array gives of the drivers that are currently available for 
   on a particular host. */

CdIo_driver_t CdIo_driver[CDIO_MAX_DRIVER] = { {0} };

/* The last valid entry of Cdio_driver. 
   -1 or (CDIO_DRIVER_UNINIT) means uninitialzed. 
   -2 means some sort of error.
*/

#define CDIO_DRIVER_UNINIT -1
int CdIo_last_driver = CDIO_DRIVER_UNINIT;

static bool 
cdio_have_false(void)
{
  return false;
}


/* The below array gives all drivers that can possibly appear.
   on a particular host. */

CdIo_driver_t CdIo_all_drivers[CDIO_MAX_DRIVER+1] = {
  {DRIVER_UNKNOWN, 
   0,
   "Unknown", 
   "No driver",
   &cdio_have_false,
   NULL,
   NULL,
   NULL,
   NULL
  },

  {DRIVER_BSDI, 
   CDIO_SRC_IS_DEVICE_MASK|CDIO_SRC_IS_NATIVE_MASK|CDIO_SRC_IS_SCSI_MASK,
   "BSDI",
   "BSDI ATAPI and SCSI driver",
   &cdio_have_bsdi,
   &cdio_open_bsdi,
   &cdio_get_default_device_bsdi,
   &cdio_is_device_generic,
   &cdio_get_devices_bsdi
  },

  {DRIVER_FREEBSD, 
   CDIO_SRC_IS_DEVICE_MASK|CDIO_SRC_IS_NATIVE_MASK|CDIO_SRC_IS_SCSI_MASK,
   "FreeBSD",
   "FreeBSD driver",
   &cdio_have_freebsd,
   &cdio_open_freebsd,
   &cdio_get_default_device_freebsd,
   &cdio_is_device_generic,
   NULL
  },

  {DRIVER_LINUX, 
   CDIO_SRC_IS_DEVICE_MASK|CDIO_SRC_IS_NATIVE_MASK,
   "GNU/Linux", 
   "GNU/Linux ioctl and MMC driver",
   &cdio_have_linux,
   &cdio_open_linux,
   &cdio_get_default_device_linux,
   &cdio_is_device_generic,
   &cdio_get_devices_linux
  },

  {DRIVER_SOLARIS, 
   CDIO_SRC_IS_DEVICE_MASK|CDIO_SRC_IS_NATIVE_MASK|CDIO_SRC_IS_SCSI_MASK,
   "Solaris",
   "Solaris ATAPI and SCSI driver",
   &cdio_have_solaris,
   &cdio_open_solaris,
   &cdio_get_default_device_solaris,
   &cdio_is_device_generic,
   &cdio_get_devices_solaris
  },

  {DRIVER_OSX, 
   CDIO_SRC_IS_DEVICE_MASK|CDIO_SRC_IS_NATIVE_MASK|CDIO_SRC_IS_SCSI_MASK,
   "OS X",
   "Apple Darwin OS X driver",
   &cdio_have_osx,
   &cdio_open_osx,
   &cdio_get_default_device_osx,
   &cdio_is_device_generic,
   &cdio_get_devices_osx
  },

  {DRIVER_WIN32, 
   CDIO_SRC_IS_DEVICE_MASK|CDIO_SRC_IS_NATIVE_MASK|CDIO_SRC_IS_SCSI_MASK,
   "WIN32",
   "Windows 32-bit ASPI and winNT/2K/XP ioctl driver",
   &cdio_have_win32,
   &cdio_open_win32,
   &cdio_get_default_device_win32,
   &cdio_is_device_win32,
   &cdio_get_devices_win32
  },

  {DRIVER_BINCUE,
   CDIO_SRC_IS_DISK_IMAGE_MASK,
   "BIN/CUE",
   "bin/cuesheet disk image driver",
   &cdio_have_bincue,
   &cdio_open_bincue,
   &cdio_get_default_device_bincue,
   NULL,
   &cdio_get_devices_bincue
  },

  {DRIVER_NRG,
   CDIO_SRC_IS_DISK_IMAGE_MASK,
   "NRG",
   "Nero NRG disk image driver",
   &cdio_have_nrg,
   &cdio_open_nrg,
   &cdio_get_default_device_nrg,
   NULL,
   &cdio_get_devices_nrg
  }

};

static CdIo *
scan_for_driver(driver_id_t start, driver_id_t end, const char *source_name) 
{
  driver_id_t driver_id;
  
  for (driver_id=start; driver_id<=end; driver_id++) {
    if ((*CdIo_all_drivers[driver_id].have_driver)()) {
      CdIo *ret=(*CdIo_all_drivers[driver_id].driver_open)(source_name);
      if (ret != NULL) {
        ret->driver_id = driver_id;
        return ret;
      }
    }
  }
  return NULL;
}

const char *
cdio_driver_describe(driver_id_t driver_id)
{
  return CdIo_all_drivers[driver_id].describe;
}

/*!
  Eject media in CD drive if there is a routine to do so. 
  Return 0 if success and 1 for failure, and 2 if no routine.
  If the CD is ejected *obj is freed and obj set to NULL.
 */
int
cdio_eject_media (CdIo **obj)
{
  
  if ((obj == NULL) || (*obj == NULL)) return 1;

  if ((*obj)->op.eject_media) {
    int ret = (*obj)->op.eject_media ((*obj)->env);
    if (0 == ret) {
      cdio_destroy(*obj);
      *obj = NULL;
    }
    return ret;
  } else {
    cdio_destroy(*obj);
    *obj = NULL;
    return 2;
  }
}

/*!
  Free device list returned by cdio_get_devices or
  cdio_get_devices_with_cap.
*/
void cdio_free_device_list (char * device_list[]) 
{
  if (NULL == device_list) return;
  for ( ; *device_list != NULL ; device_list++ )
    free(*device_list);
}


/*!
  Return the value associatied with key. NULL is returned if obj is NULL
  or "key" does not exist.
 */
const char *
cdio_get_arg (const CdIo *obj, const char key[])
{
  if (obj == NULL) return NULL;
  
  if (obj->op.get_arg) {
    return obj->op.get_arg (obj->env, key);
  } else {
    return NULL;
  }
}

/*!
  Return a string containing the default CD device if none is specified.
  if CdIo is NULL (we haven't initialized a specific device driver), 
  then find a suitable one and return the default device for that.

  NULL is returned if we couldn't get a default device.
 */
char *
cdio_get_default_device (const CdIo *obj)
{
  if (obj == NULL) {
    driver_id_t driver_id;
    /* Scan for driver */
    for (driver_id=DRIVER_UNKNOWN; driver_id<=CDIO_MAX_DRIVER; driver_id++) {
      if ( (*CdIo_all_drivers[driver_id].have_driver)() &&
           *CdIo_all_drivers[driver_id].get_default_device ) {
        return (*CdIo_all_drivers[driver_id].get_default_device)();
      }
    }
    return NULL;
  }
  
  if (obj->op.get_default_device) {
    return obj->op.get_default_device ();
  } else {
    return NULL;
  }
}

/*!Return an array of device names. If you want a specific
  devices, dor a driver give that device, if you want hardware
  devices, give DRIVER_DEVICE and if you want all possible devices,
  image drivers and hardware drivers give DRIVER_UNKNOWN.
  
  NULL is returned if we couldn't return a list of devices.
*/
char **
cdio_get_devices (driver_id_t driver_id)
{
  CdIo *cdio;

  switch (driver_id) {
    /* FIXME: spit out unknown to give image drivers as well.  */
  case DRIVER_UNKNOWN:
  case DRIVER_DEVICE:
    cdio = scan_for_driver(DRIVER_UNKNOWN, CDIO_MAX_DRIVER, NULL);
    break;
  default:
    return (*CdIo_all_drivers[driver_id].get_devices)();
  }
  
  if (cdio == NULL) return NULL;
  if (cdio->op.get_devices) {
    return cdio->op.get_devices ();
  } else {
    return NULL;
  }
}

/*!
  Return an array of device names in search_devices that have at
  least the capabilities listed by cap.  If search_devices is NULL,
  then we'll search all possible CD drives.  
  
  If "any" is set false then every capability listed in the extended
  portion of capabilities (i.e. not the basic filesystem) must be
  satisified. If "any" is set true, then if any of the capabilities
  matches, we call that a success.
  
  To find a CD-drive of any type, use the mask CDIO_FS_MATCH_ALL.
  
  NULL is returned if we couldn't get a default device.
*/
char **
cdio_get_devices_with_cap (char* search_devices[], 
                           cdio_fs_anal_t capabilities, bool any)
{
  char **drives=search_devices;
  char **drives_ret=NULL;
  int num_drives=0;

  if (NULL == drives) drives=cdio_get_devices(DRIVER_DEVICE);
  if (NULL == drives) return NULL;

  if (capabilities == CDIO_FS_MATCH_ALL) {
    /* Duplicate drives into drives_ret. */
    for( ; *drives != NULL; drives++ ) {
      cdio_add_device_list(&drives_ret, *drives, &num_drives);
    }
  } else {
    cdio_fs_anal_t got_fs=0;
    cdio_fs_anal_t need_fs = CDIO_FSTYPE(capabilities);
    cdio_fs_anal_t need_fs_ext;
    need_fs_ext = capabilities & ~CDIO_FS_MASK;
      
    for( ;  *drives != NULL; drives++ ) {
      CdIo *cdio = cdio_open(*drives, DRIVER_UNKNOWN);
      
      if (NULL != cdio) {
        track_t first_track = cdio_get_first_track_num(cdio);
        cdio_analysis_t cdio_analysis; 
        got_fs = cdio_guess_cd_type(cdio, 0, first_track, 
                                &cdio_analysis);
        /* Match on fs and add */
        if ( (CDIO_FS_UNKNOWN == need_fs || CDIO_FSTYPE(got_fs) == need_fs) )
          {
            bool doit = any
              ? (got_fs & need_fs_ext)  != 0
              : (got_fs | ~need_fs_ext) == -1;
            if (doit) 
              cdio_add_device_list(&drives_ret, *drives, &num_drives);
          }
             
        cdio_destroy(cdio);
      }
    }
    cdio_add_device_list(&drives_ret, NULL, &num_drives);
  }
  return drives_ret;
}

/*!
  Return a string containing the name of the driver in use.
  if CdIo is NULL (we haven't initialized a specific device driver), 
  then return NULL.
*/
const char *
cdio_get_driver_name (const CdIo *cdio) 
{
  return CdIo_all_drivers[cdio->driver_id].name;
}


/*!
  Return the number of of the first track. 
  CDIO_INVALID_TRACK is returned on error.
*/
track_t
cdio_get_first_track_num (const CdIo *cdio)
{
  cdio_assert (cdio != NULL);

  if (cdio->op.get_first_track_num) {
    return cdio->op.get_first_track_num (cdio->env);
  } else {
    return CDIO_INVALID_TRACK;
  }
}

/*!
  Return a string containing the name of the driver in use.
  if CdIo is NULL (we haven't initialized a specific device driver), 
  then return NULL.
*/
char *
cdio_get_mcn (const CdIo *cdio) 
{
  if (cdio->op.get_mcn) {
    return cdio->op.get_mcn (cdio->env);
  } else {
    return NULL;
  }
}

/*! 
  Return the number of tracks in the current medium.
  CDIO_INVALID_TRACK is returned on error.
*/
track_t
cdio_get_num_tracks (const CdIo *cdio)
{
  if (cdio == NULL) return CDIO_INVALID_TRACK;

  if (cdio->op.get_num_tracks) {
    return cdio->op.get_num_tracks (cdio->env);
  } else {
    return CDIO_INVALID_TRACK;
  }
}

/*!  
  Get format of track. 
*/
track_format_t
cdio_get_track_format(const CdIo *cdio, track_t track_num)
{
  cdio_assert (cdio != NULL);

  if (cdio->op.get_track_format) {
    return cdio->op.get_track_format (cdio->env, track_num);
  } else {
    return TRACK_FORMAT_ERROR;
  }
}

/*!
  Return true if we have XA data (green, mode2 form1) or
  XA data (green, mode2 form2). That is track begins:
  sync - header - subheader
  12     4      -  8
  
  FIXME: there's gotta be a better design for this and get_track_format?
*/
bool
cdio_get_track_green(const CdIo *cdio, track_t track_num)
{
  cdio_assert (cdio != NULL);

  if (cdio->op.get_track_green) {
    return cdio->op.get_track_green (cdio->env, track_num);
  } else {
    return false;
  }
}

/*!  
  Return the starting LBA for track number
  track_num in cdio.  Tracks numbers start at 1.
  The "leadout" track is specified either by
  using track_num LEADOUT_TRACK or the total tracks+1.
  CDIO_INVALID_LBA is returned on error.
*/
lba_t
cdio_get_track_lba(const CdIo *cdio, track_t track_num)
{
  if (cdio == NULL) return CDIO_INVALID_LBA;

  if (cdio->op.get_track_lba) {
    return cdio->op.get_track_lba (cdio->env, track_num);
  } else {
    msf_t msf;
    if (cdio->op.get_track_msf) 
      if (cdio_get_track_msf(cdio, track_num, &msf))
        return cdio_msf_to_lba(&msf);
    return CDIO_INVALID_LBA;
  }
}

/*!  
  Return the starting LSN for track number
  track_num in cdio.  Tracks numbers start at 1.
  The "leadout" track is specified either by
  using track_num LEADOUT_TRACK or the total tracks+1.
  CDIO_INVALID_LBA is returned on error.
*/
lsn_t
cdio_get_track_lsn(const CdIo *cdio, track_t track_num)
{
  if (cdio == NULL) return CDIO_INVALID_LBA;

  if (cdio->op.get_track_lba) {
    return cdio_lba_to_lsn(cdio->op.get_track_lba (cdio->env, track_num));
  } else {
    msf_t msf;
    if (cdio_get_track_msf(cdio, track_num, &msf))
      return cdio_msf_to_lsn(&msf);
    return CDIO_INVALID_LSN;
  }
}

/*!  
  Return the starting MSF (minutes/secs/frames) for track number
  track_num in cdio.  Track numbers start at 1.
  The "leadout" track is specified either by
  using track_num LEADOUT_TRACK or the total tracks+1.
  False is returned if there is no track entry.
*/
bool
cdio_get_track_msf(const CdIo *cdio, track_t track_num, /*out*/ msf_t *msf)
{
  cdio_assert (cdio != NULL);

  if (cdio->op.get_track_msf) {
    return cdio->op.get_track_msf (cdio->env, track_num, msf);
  } else if (cdio->op.get_track_lba) {
    lba_t lba = cdio->op.get_track_lba (cdio->env, track_num);
    if (lba  == CDIO_INVALID_LBA) return false;
    cdio_lba_to_msf(lba, msf);
    return true;
  } else {
    return false;
  }
}

/*!  
  Return the number of sectors between this track an the next.  This
  includes any pregap sectors before the start of the next track.
  Tracks start at 1.
  0 is returned if there is an error.
*/
unsigned int
cdio_get_track_sec_count(const CdIo *cdio, track_t track_num)
{
  track_t num_tracks = cdio_get_num_tracks(cdio);

  if (track_num >=1 && track_num <= num_tracks) 
    return ( cdio_get_track_lba(cdio, track_num+1) 
             - cdio_get_track_lba(cdio, track_num) );
  return 0;
}

bool
cdio_have_driver(driver_id_t driver_id)
{
  return (*CdIo_all_drivers[driver_id].have_driver)();
}

bool
cdio_is_device(const char *source_name, driver_id_t driver_id)
{
  if (CdIo_all_drivers[driver_id].is_device == NULL) return false;
  return (*CdIo_all_drivers[driver_id].is_device)(source_name);
}


/*!
  Initialize CD Reading and control routines. Should be called first.
  May be implicitly called by other routines if not called first.
*/
bool
cdio_init(void)
{
  
  CdIo_driver_t *all_dp;
  CdIo_driver_t *dp = CdIo_driver;
  driver_id_t driver_id;

  if (CdIo_last_driver != CDIO_DRIVER_UNINIT) {
    cdio_warn ("Init routine called more than once.");
    return false;
  }

  for (driver_id=DRIVER_UNKNOWN; driver_id<=CDIO_MAX_DRIVER; driver_id++) {
    all_dp = &CdIo_all_drivers[driver_id];
    if ((*CdIo_all_drivers[driver_id].have_driver)()) {
      *dp++ = *all_dp;
      CdIo_last_driver++;
    }
  }

  return true;
}

CdIo *
cdio_new (void *env, const cdio_funcs *funcs)
{
  CdIo *new_cdio;

  new_cdio = _cdio_malloc (sizeof (CdIo));

  new_cdio->env = env;
  new_cdio->op = *funcs;

  return new_cdio;
}

/*!
  Free any resources associated with cdio.
*/
void
cdio_destroy (CdIo *cdio)
{
  CdIo_last_driver = CDIO_DRIVER_UNINIT;
  if (cdio == NULL) return;

  if (cdio->op.free != NULL) 
    cdio->op.free (cdio->env);
  free (cdio);
}

/*!
  lseek - reposition read/write file offset
  Returns (off_t) -1 on error. 
  Similar to (if not the same as) libc's lseek()
*/
off_t
cdio_lseek (const CdIo *cdio, off_t offset, int whence)
{
  if (cdio == NULL) return -1;
  
  if (cdio->op.lseek)
    return cdio->op.lseek (cdio->env, offset, whence);
  return -1;
}

/*!
  Reads into buf the next size bytes.
  Returns -1 on error. 
  Similar to (if not the same as) libc's read()
*/
ssize_t
cdio_read (const CdIo *cdio, void *buf, size_t size)
{
  if (cdio == NULL) return -1;
  
  if (cdio->op.read)
    return cdio->op.read (cdio->env, buf, size);
  return -1;
}

int
cdio_read_audio_sector (const CdIo *cdio, void *buf, lsn_t lsn) 
{
  cdio_assert (cdio != NULL);
  cdio_assert (buf != NULL);

  if  (cdio->op.read_audio_sectors != NULL)
    return cdio->op.read_audio_sectors (cdio->env, buf, lsn, 1);
  return -1;
}

int
cdio_read_audio_sectors (const CdIo *cdio, void *buf, lsn_t lsn,
                         unsigned int nblocks) 
{
  cdio_assert (cdio != NULL);
  cdio_assert (buf != NULL);

  if  (cdio->op.read_audio_sectors != NULL)
    return cdio->op.read_audio_sectors (cdio->env, buf, lsn, nblocks);
  return -1;
}

/*!
   Reads a single mode1 form1 or form2  sector from cd device 
   into data starting from lsn. Returns 0 if no error. 
 */
int
cdio_read_mode1_sector (const CdIo *cdio, void *data, lsn_t lsn, bool is_form2)
{
  uint32_t size = is_form2 ? M2RAW_SECTOR_SIZE : CDIO_CD_FRAMESIZE ;
  char buf[M2RAW_SECTOR_SIZE] = { 0, };
  int ret;
  
  cdio_assert (cdio != NULL);
  cdio_assert (data != NULL);

  if (cdio->op.lseek && cdio->op.read) {
    if (0 > cdio_lseek(cdio, CDIO_CD_FRAMESIZE*lsn, SEEK_SET))
      return -1;
    if (0 > cdio_read(cdio, buf, CDIO_CD_FRAMESIZE))
      return -1;
    memcpy (data, buf, size);
    return 0;
  } else {
    ret = cdio_read_mode2_sector(cdio, data, lsn, is_form2);
    if (ret == 0) 
      memcpy (data, buf+CDIO_CD_SUBHEADER_SIZE, size);
  }
  return ret;

}

int
cdio_read_mode1_sectors (const CdIo *cdio, void *data, lsn_t lsn, 
                         bool is_form2,  unsigned int num_sectors)
{
  uint32_t size = is_form2 ? M2RAW_SECTOR_SIZE : CDIO_CD_FRAMESIZE ;
  int retval;
  int i;

  cdio_assert (cdio != NULL);
  
  for (i = 0; i < num_sectors; i++) {
    if ( (retval = cdio_read_mode1_sector (cdio, 
                                           ((char *)data) + (size * i),
                                           lsn + i, is_form2)) )
      return retval;
  }
  return 0;
}

/*!
   Reads a single mode2 sector from cd device into data starting
   from lsn. Returns 0 if no error. 
 */
int
cdio_read_mode2_sector (const CdIo *cdio, void *buf, lsn_t lsn, 
                        bool is_form2)
{
  cdio_assert (cdio != NULL);
  cdio_assert (buf != NULL);
  cdio_assert (cdio->op.read_mode2_sector != NULL 
	      || cdio->op.read_mode2_sectors != NULL);

  if (cdio->op.read_mode2_sector)
    return cdio->op.read_mode2_sector (cdio->env, buf, lsn, is_form2);

  /* fallback */
  if (cdio->op.read_mode2_sectors != NULL)
    return cdio_read_mode2_sectors (cdio, buf, lsn, is_form2, 1);
  return 1;
}

int
cdio_read_mode2_sectors (const CdIo *cdio, void *buf, lsn_t lsn, bool mode2raw, 
                         unsigned num_sectors)
{
  cdio_assert (cdio != NULL);
  cdio_assert (buf != NULL);
  cdio_assert (cdio->op.read_mode2_sectors != NULL);
  
  return cdio->op.read_mode2_sectors (cdio->env, buf, lsn,
                                     mode2raw, num_sectors);
}

uint32_t
cdio_stat_size (const CdIo *cdio)
{
  cdio_assert (cdio != NULL);

  return cdio->op.stat_size (cdio->env);
}

/*!
  Set the arg "key" with "value" in the source device.
*/
int
cdio_set_arg (CdIo *cdio, const char key[], const char value[])
{
  cdio_assert (cdio != NULL);
  cdio_assert (cdio->op.set_arg != NULL);
  cdio_assert (key != NULL);

  return cdio->op.set_arg (cdio->env, key, value);
}

/*! Sets up to read from place specified by source_name and
  driver_id. This should be called before using any other routine,
  except cdio_init. This will call cdio_init, if that hasn't been
  done previously.
  
  NULL is returned on error.
*/
CdIo *
cdio_open (const char *orig_source_name, driver_id_t driver_id)
{
  char *source_name;
  
  if (CdIo_last_driver == -1) cdio_init();

  if (NULL == orig_source_name || strlen(orig_source_name)==0) 
    source_name = cdio_get_default_device(NULL);
  else 
    source_name = strdup(orig_source_name);
  
 retry:
  switch (driver_id) {
  case DRIVER_UNKNOWN: 
    {
      CdIo *cdio=scan_for_driver(CDIO_MIN_DRIVER, CDIO_MAX_DRIVER, 
                                 source_name);
      if (cdio != NULL && cdio_is_device(source_name, cdio->driver_id)) {
        driver_id = cdio->driver_id;
      } else {
        struct stat buf;
        if (0 != stat(source_name, &buf)) {
          return NULL;
        }
        if (S_ISREG(buf.st_mode)) {
        /* FIXME: check to see if is a text file. If so, then 
           set SOURCE_CUE. */
          int i=strlen(source_name)-strlen("bin");
          if (i > 0
              && ( (source_name)[i]   =='n' || (source_name)[i]   =='N' )
              && ( (source_name)[i+1] =='r' || (source_name)[i+1] =='R' )
              && ( (source_name)[i+2] =='g' || (source_name)[i+2] =='G' ) )
            driver_id = DRIVER_NRG;
          else if (i > 0
                   && ( (source_name)[i]   =='c' || (source_name)[i]   =='C')
                   && ( (source_name)[i+1] =='u' || (source_name)[i+1] =='U')
                   && ( (source_name)[i+2] =='e' || (source_name)[i+2] =='E') )
            driver_id = DRIVER_BINCUE;
          else
            driver_id = DRIVER_BINCUE;
        } else {
          cdio_destroy(cdio);
          return NULL;
        }
        
      }
      cdio_destroy(cdio);
      goto retry;
    }
  case DRIVER_DEVICE: 
    {  
      /* Scan for a driver. */
      CdIo *ret = cdio_open_cd(source_name);
      free(source_name);
      return ret;
    }
    break;
  case DRIVER_BSDI:
  case DRIVER_FREEBSD:
  case DRIVER_LINUX:
  case DRIVER_SOLARIS:
  case DRIVER_WIN32:
  case DRIVER_OSX:
  case DRIVER_NRG:
  case DRIVER_BINCUE:
    if ((*CdIo_all_drivers[driver_id].have_driver)()) {
      CdIo *ret = (*CdIo_all_drivers[driver_id].driver_open)(source_name);
      if (ret) ret->driver_id = driver_id;
      return ret;
    }
  }

  free(source_name);
  return NULL;
}


/* In the future we'll have more complicated code to allow selection
   of an I/O routine as well as code to find an appropriate default
   routine among the "registered" routines. Possibly classes too
   disk-based, SCSI-based, native-based, vendor (e.g. Sony, or
   Plextor) based 

   For now though, we'll start more simply...
*/
CdIo *
cdio_open_cd (const char *source_name)
{
  if (CdIo_last_driver == -1) cdio_init();

  /* Scan for a driver. */
  return scan_for_driver(CDIO_MIN_DEVICE_DRIVER, CDIO_MAX_DEVICE_DRIVER, 
                         source_name);
}



/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
