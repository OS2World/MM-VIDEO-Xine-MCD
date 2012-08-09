/*!
   \file vcdinf.h

    Copyright (C) 2002,2003 Rocky Bernstein <rocky@panix.com>

 \verbatim
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Foundation
    Software, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Like vcdinfo but exposes more of the internal structure. It is probably
    better to use vcdinfo, when possible.
 \endverbatim
*/

#ifndef _VCD_INFO_PRIVATE_H
#define _VCD_INFO_PRIVATE_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <cdio/cdio.h>
#include <cdio/iso9660.h>
#include <libvcd/types.h>
#include <libvcd/files_private.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

  struct _VcdInfo {
    vcd_type_t vcd_type;
    
    CdIo *img;
    
    iso9660_pvd_t pvd;
    
    InfoVcd info;
    EntriesVcd entries;
    
    VcdList *offset_list;
    VcdList *offset_x_list;
    uint32_t *seg_sizes; 
    lsn_t   first_segment_lsn;
    
    LotVcd *lot;
    LotVcd *lot_x;
    uint8_t *psd;
    uint8_t *psd_x;
    unsigned int psd_x_size;
    bool extended;

    bool has_xa;           /* True if has extended attributes (XA) */
    
    void *tracks_buf;
    void *search_buf;
    void *scandata_buf;
    
    char *source_name; /* VCD device or file currently open */
    
  };
  
  /*!  Return the starting MSF (minutes/secs/frames) for sequence
    entry_num in obj.  NULL is returned if there is no entry.
    The first entry number is 0.
  */
  const msf_t * vcdinf_get_entry_msf(const EntriesVcd *entries, 
				     unsigned int entry_num);

  struct _vcdinf_pbc_ctx {
    unsigned int psd_size;
    lid_t maximum_lid;
    unsigned offset_mult;
    VcdList *offset_x_list;
    VcdList *offset_list;
    
    LotVcd *lot;
    LotVcd *lot_x;
    uint8_t *psd;
    uint8_t *psd_x;
    unsigned int psd_x_size;
    bool extended;
  };

  /*!
     Calls recursive routine to populate obj->offset_list or obj->offset_x_list
     by going through LOT.
  */
  void vcdinf_visit_lot (struct _vcdinf_pbc_ctx *obj);
  
  /*! 
     Recursive routine to populate obj->offset_list or obj->offset_x_list
     by reading playback control entries referred to via lid.
  */
  void vcdinf_visit_pbc (struct _vcdinf_pbc_ctx *obj, lid_t lid, 
			 unsigned int offset, bool in_lot);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /*_VCD_INFO_PRIVATE_H*/
