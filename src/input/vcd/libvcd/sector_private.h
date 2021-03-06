/*
    $Id: sector_private.h,v 1.1 2003/10/13 11:47:12 f1rmb Exp $

    Copyright (C) 2000 Herbert Valerio Riedel <hvr@gnu.org>
              (C) 1998 Heiko Eissfeldt <heiko@colossus.escape.de>

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

#ifndef __VCD_CD_SECTOR_PRIVATE_H__
#define __VCD_CD_SECTOR_PRIVATE_H__

#define RS_L12_BITS 8 

#define L2_RAW (1024*2) 
#define L2_Q   (26*2*2) 
#define L2_P   (43*2*2) 

typedef enum {
  MODE_0,
  MODE_2,
  MODE_2_FORM_1,
  MODE_2_FORM_2
} sectortype_t;

#define SYNC_LEN 12
#define DATA_LEN 2336
#define HEADER_LEN 4

typedef struct {
  uint8_t sync[SYNC_LEN];
  msf_t msf;
  uint8_t mode;
  uint8_t user_data[EMPTY_ARRAY_SIZE];
} raw_cd_sector_t;

#define raw_cd_sector_t_SIZEOF (SYNC_LEN+HEADER_LEN)

typedef struct {
  uint8_t sync[SYNC_LEN];
  msf_t msf;
  uint8_t mode;
} sector_header_t;

#define sector_header_t_SIZEOF (SYNC_LEN+HEADER_LEN)

typedef struct {
  sector_header_t sector_header;
  uint8_t data[M2RAW_SECTOR_SIZE];
} mode0_sector_t;

#define mode0_sector_t_SIZEOF CDIO_CD_FRAMESIZE_RAW

typedef struct {
  sector_header_t sector_header;
  uint8_t subheader[CDIO_CD_SUBHEADER_SIZE];
  uint8_t data[CDIO_CD_FRAMESIZE];
  uint32_t edc;
  uint8_t l2_p[L2_P];
  uint8_t l2_q[L2_Q];
} mode2_form1_sector_t;

#define mode2_form1_sector_t_SIZEOF CDIO_CD_FRAMESIZE_RAW

typedef struct {
  sector_header_t sector_header;
  uint8_t subheader[CDIO_CD_SUBHEADER_SIZE];
  uint8_t data[M2F2_SECTOR_SIZE];
  uint32_t edc;
} mode2_form2_sector_t;

#define mode2_form2_sector_t_SIZEOF CDIO_CD_FRAMESIZE_RAW

static const uint32_t EDC_crctable[256] = {
  0x00000000l, 0x90910101l, 0x91210201l, 0x01b00300l,
  0x92410401l, 0x02d00500l, 0x03600600l, 0x93f10701l,
  0x94810801l, 0x04100900l, 0x05a00a00l, 0x95310b01l,
  0x06c00c00l, 0x96510d01l, 0x97e10e01l, 0x07700f00l,
  0x99011001l, 0x09901100l, 0x08201200l, 0x98b11301l,
  0x0b401400l, 0x9bd11501l, 0x9a611601l, 0x0af01700l,
  0x0d801800l, 0x9d111901l, 0x9ca11a01l, 0x0c301b00l,
  0x9fc11c01l, 0x0f501d00l, 0x0ee01e00l, 0x9e711f01l,
  0x82012001l, 0x12902100l, 0x13202200l, 0x83b12301l,
  0x10402400l, 0x80d12501l, 0x81612601l, 0x11f02700l,
  0x16802800l, 0x86112901l, 0x87a12a01l, 0x17302b00l,
  0x84c12c01l, 0x14502d00l, 0x15e02e00l, 0x85712f01l,
  0x1b003000l, 0x8b913101l, 0x8a213201l, 0x1ab03300l,
  0x89413401l, 0x19d03500l, 0x18603600l, 0x88f13701l,
  0x8f813801l, 0x1f103900l, 0x1ea03a00l, 0x8e313b01l,
  0x1dc03c00l, 0x8d513d01l, 0x8ce13e01l, 0x1c703f00l,
  0xb4014001l, 0x24904100l, 0x25204200l, 0xb5b14301l,
  0x26404400l, 0xb6d14501l, 0xb7614601l, 0x27f04700l,
  0x20804800l, 0xb0114901l, 0xb1a14a01l, 0x21304b00l,
  0xb2c14c01l, 0x22504d00l, 0x23e04e00l, 0xb3714f01l,
  0x2d005000l, 0xbd915101l, 0xbc215201l, 0x2cb05300l,
  0xbf415401l, 0x2fd05500l, 0x2e605600l, 0xbef15701l,
  0xb9815801l, 0x29105900l, 0x28a05a00l, 0xb8315b01l,
  0x2bc05c00l, 0xbb515d01l, 0xbae15e01l, 0x2a705f00l,
  0x36006000l, 0xa6916101l, 0xa7216201l, 0x37b06300l,
  0xa4416401l, 0x34d06500l, 0x35606600l, 0xa5f16701l,
  0xa2816801l, 0x32106900l, 0x33a06a00l, 0xa3316b01l,
  0x30c06c00l, 0xa0516d01l, 0xa1e16e01l, 0x31706f00l,
  0xaf017001l, 0x3f907100l, 0x3e207200l, 0xaeb17301l,
  0x3d407400l, 0xadd17501l, 0xac617601l, 0x3cf07700l,
  0x3b807800l, 0xab117901l, 0xaaa17a01l, 0x3a307b00l,
  0xa9c17c01l, 0x39507d00l, 0x38e07e00l, 0xa8717f01l,
  0xd8018001l, 0x48908100l, 0x49208200l, 0xd9b18301l,
  0x4a408400l, 0xdad18501l, 0xdb618601l, 0x4bf08700l,
  0x4c808800l, 0xdc118901l, 0xdda18a01l, 0x4d308b00l,
  0xdec18c01l, 0x4e508d00l, 0x4fe08e00l, 0xdf718f01l,
  0x41009000l, 0xd1919101l, 0xd0219201l, 0x40b09300l,
  0xd3419401l, 0x43d09500l, 0x42609600l, 0xd2f19701l,
  0xd5819801l, 0x45109900l, 0x44a09a00l, 0xd4319b01l,
  0x47c09c00l, 0xd7519d01l, 0xd6e19e01l, 0x46709f00l,
  0x5a00a000l, 0xca91a101l, 0xcb21a201l, 0x5bb0a300l,
  0xc841a401l, 0x58d0a500l, 0x5960a600l, 0xc9f1a701l,
  0xce81a801l, 0x5e10a900l, 0x5fa0aa00l, 0xcf31ab01l,
  0x5cc0ac00l, 0xcc51ad01l, 0xcde1ae01l, 0x5d70af00l,
  0xc301b001l, 0x5390b100l, 0x5220b200l, 0xc2b1b301l,
  0x5140b400l, 0xc1d1b501l, 0xc061b601l, 0x50f0b700l,
  0x5780b800l, 0xc711b901l, 0xc6a1ba01l, 0x5630bb00l,
  0xc5c1bc01l, 0x5550bd00l, 0x54e0be00l, 0xc471bf01l,
  0x6c00c000l, 0xfc91c101l, 0xfd21c201l, 0x6db0c300l,
  0xfe41c401l, 0x6ed0c500l, 0x6f60c600l, 0xfff1c701l,
  0xf881c801l, 0x6810c900l, 0x69a0ca00l, 0xf931cb01l,
  0x6ac0cc00l, 0xfa51cd01l, 0xfbe1ce01l, 0x6b70cf00l,
  0xf501d001l, 0x6590d100l, 0x6420d200l, 0xf4b1d301l,
  0x6740d400l, 0xf7d1d501l, 0xf661d601l, 0x66f0d700l,
  0x6180d800l, 0xf111d901l, 0xf0a1da01l, 0x6030db00l,
  0xf3c1dc01l, 0x6350dd00l, 0x62e0de00l, 0xf271df01l,
  0xee01e001l, 0x7e90e100l, 0x7f20e200l, 0xefb1e301l,
  0x7c40e400l, 0xecd1e501l, 0xed61e601l, 0x7df0e700l,
  0x7a80e800l, 0xea11e901l, 0xeba1ea01l, 0x7b30eb00l,
  0xe8c1ec01l, 0x7850ed00l, 0x79e0ee00l, 0xe971ef01l,
  0x7700f000l, 0xe791f101l, 0xe621f201l, 0x76b0f300l,
  0xe541f401l, 0x75d0f500l, 0x7460f600l, 0xe4f1f701l,
  0xe381f801l, 0x7310f900l, 0x72a0fa00l, 0xe231fb01l,
  0x71c0fc00l, 0xe151fd01l, 0xe0e1fe01l, 0x7070ff00l
};

static const uint8_t rs_l12_alog[255] = {
  0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 
  0x1d, 0x3a, 0x74, 0xe8, 0xcd, 0x87, 0x13, 0x26, 
  0x4c, 0x98, 0x2d, 0x5a, 0xb4, 0x75, 0xea, 0xc9, 
  0x8f, 0x03, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0, 
  0x9d, 0x27, 0x4e, 0x9c, 0x25, 0x4a, 0x94, 0x35, 
  0x6a, 0xd4, 0xb5, 0x77, 0xee, 0xc1, 0x9f, 0x23, 
  0x46, 0x8c, 0x05, 0x0a, 0x14, 0x28, 0x50, 0xa0, 
  0x5d, 0xba, 0x69, 0xd2, 0xb9, 0x6f, 0xde, 0xa1, 
  0x5f, 0xbe, 0x61, 0xc2, 0x99, 0x2f, 0x5e, 0xbc, 
  0x65, 0xca, 0x89, 0x0f, 0x1e, 0x3c, 0x78, 0xf0, 
  0xfd, 0xe7, 0xd3, 0xbb, 0x6b, 0xd6, 0xb1, 0x7f, 
  0xfe, 0xe1, 0xdf, 0xa3, 0x5b, 0xb6, 0x71, 0xe2, 
  0xd9, 0xaf, 0x43, 0x86, 0x11, 0x22, 0x44, 0x88, 
  0x0d, 0x1a, 0x34, 0x68, 0xd0, 0xbd, 0x67, 0xce, 
  0x81, 0x1f, 0x3e, 0x7c, 0xf8, 0xed, 0xc7, 0x93, 
  0x3b, 0x76, 0xec, 0xc5, 0x97, 0x33, 0x66, 0xcc, 
  0x85, 0x17, 0x2e, 0x5c, 0xb8, 0x6d, 0xda, 0xa9, 
  0x4f, 0x9e, 0x21, 0x42, 0x84, 0x15, 0x2a, 0x54, 
  0xa8, 0x4d, 0x9a, 0x29, 0x52, 0xa4, 0x55, 0xaa, 
  0x49, 0x92, 0x39, 0x72, 0xe4, 0xd5, 0xb7, 0x73, 
  0xe6, 0xd1, 0xbf, 0x63, 0xc6, 0x91, 0x3f, 0x7e, 
  0xfc, 0xe5, 0xd7, 0xb3, 0x7b, 0xf6, 0xf1, 0xff, 
  0xe3, 0xdb, 0xab, 0x4b, 0x96, 0x31, 0x62, 0xc4, 
  0x95, 0x37, 0x6e, 0xdc, 0xa5, 0x57, 0xae, 0x41, 
  0x82, 0x19, 0x32, 0x64, 0xc8, 0x8d, 0x07, 0x0e, 
  0x1c, 0x38, 0x70, 0xe0, 0xdd, 0xa7, 0x53, 0xa6, 
  0x51, 0xa2, 0x59, 0xb2, 0x79, 0xf2, 0xf9, 0xef, 
  0xc3, 0x9b, 0x2b, 0x56, 0xac, 0x45, 0x8a, 0x09, 
  0x12, 0x24, 0x48, 0x90, 0x3d, 0x7a, 0xf4, 0xf5, 
  0xf7, 0xf3, 0xfb, 0xeb, 0xcb, 0x8b, 0x0b, 0x16, 
  0x2c, 0x58, 0xb0, 0x7d, 0xfa, 0xe9, 0xcf, 0x83, 
  0x1b, 0x36, 0x6c, 0xd8, 0xad, 0x47, 0x8e
};

static const uint8_t rs_l12_log[256] = {
  0x00, 0x00, 0x01, 0x19, 0x02, 0x32, 0x1a, 0xc6, 
  0x03, 0xdf, 0x33, 0xee, 0x1b, 0x68, 0xc7, 0x4b, 
  0x04, 0x64, 0xe0, 0x0e, 0x34, 0x8d, 0xef, 0x81, 
  0x1c, 0xc1, 0x69, 0xf8, 0xc8, 0x08, 0x4c, 0x71, 
  0x05, 0x8a, 0x65, 0x2f, 0xe1, 0x24, 0x0f, 0x21, 
  0x35, 0x93, 0x8e, 0xda, 0xf0, 0x12, 0x82, 0x45, 
  0x1d, 0xb5, 0xc2, 0x7d, 0x6a, 0x27, 0xf9, 0xb9, 
  0xc9, 0x9a, 0x09, 0x78, 0x4d, 0xe4, 0x72, 0xa6, 
  0x06, 0xbf, 0x8b, 0x62, 0x66, 0xdd, 0x30, 0xfd, 
  0xe2, 0x98, 0x25, 0xb3, 0x10, 0x91, 0x22, 0x88, 
  0x36, 0xd0, 0x94, 0xce, 0x8f, 0x96, 0xdb, 0xbd, 
  0xf1, 0xd2, 0x13, 0x5c, 0x83, 0x38, 0x46, 0x40, 
  0x1e, 0x42, 0xb6, 0xa3, 0xc3, 0x48, 0x7e, 0x6e, 
  0x6b, 0x3a, 0x28, 0x54, 0xfa, 0x85, 0xba, 0x3d, 
  0xca, 0x5e, 0x9b, 0x9f, 0x0a, 0x15, 0x79, 0x2b, 
  0x4e, 0xd4, 0xe5, 0xac, 0x73, 0xf3, 0xa7, 0x57, 
  0x07, 0x70, 0xc0, 0xf7, 0x8c, 0x80, 0x63, 0x0d, 
  0x67, 0x4a, 0xde, 0xed, 0x31, 0xc5, 0xfe, 0x18, 
  0xe3, 0xa5, 0x99, 0x77, 0x26, 0xb8, 0xb4, 0x7c, 
  0x11, 0x44, 0x92, 0xd9, 0x23, 0x20, 0x89, 0x2e, 
  0x37, 0x3f, 0xd1, 0x5b, 0x95, 0xbc, 0xcf, 0xcd, 
  0x90, 0x87, 0x97, 0xb2, 0xdc, 0xfc, 0xbe, 0x61, 
  0xf2, 0x56, 0xd3, 0xab, 0x14, 0x2a, 0x5d, 0x9e, 
  0x84, 0x3c, 0x39, 0x53, 0x47, 0x6d, 0x41, 0xa2, 
  0x1f, 0x2d, 0x43, 0xd8, 0xb7, 0x7b, 0xa4, 0x76, 
  0xc4, 0x17, 0x49, 0xec, 0x7f, 0x0c, 0x6f, 0xf6, 
  0x6c, 0xa1, 0x3b, 0x52, 0x29, 0x9d, 0x55, 0xaa, 
  0xfb, 0x60, 0x86, 0xb1, 0xbb, 0xcc, 0x3e, 0x5a, 
  0xcb, 0x59, 0x5f, 0xb0, 0x9c, 0xa9, 0xa0, 0x51, 
  0x0b, 0xf5, 0x16, 0xeb, 0x7a, 0x75, 0x2c, 0xd7, 
  0x4f, 0xae, 0xd5, 0xe9, 0xe6, 0xe7, 0xad, 0xe8, 
  0x74, 0xd6, 0xf4, 0xea, 0xa8, 0x50, 0x58, 0xaf
};

static const uint8_t DQ[2][43] = {
  { 0xbe, 0x60, 0xfa, 0x84, 0x3b, 0x51, 0x9f, 0x9a,
    0xc8, 0x07, 0x6f, 0xf5, 0x0a, 0x14, 0x29, 0x9c,
    0xa8, 0x4f, 0xad, 0xe7, 0xe5, 0xab, 0xd2, 0xf0, 
    0x11, 0x43, 0xd7, 0x2b, 0x78, 0x08, 0xc7, 0x4a,
    0x66, 0xdc, 0xfb, 0x5f, 0xaf, 0x57, 0xa6, 0x71, 
    0x4b, 0xc6, 0x19 },
  { 0x61, 0xfb, 0x85, 0x3c, 0x52, 0xa0, 0x9b, 0xc9,
    0x08, 0x70, 0xf6, 0x0b, 0x15, 0x2a, 0x9d, 0xa9,
    0x50, 0xae, 0xe8, 0xe6, 0xac, 0xd3, 0xf1, 0x12, 
    0x44, 0xd8, 0x2c, 0x79, 0x09, 0xc8, 0x4b, 0x67,
    0xdd, 0xfc, 0x60, 0xb0, 0x58, 0xa7, 0x72, 0x4c,
    0xc7, 0x1a, 0x01 },
};

static const uint8_t DP[2][24] = {
  { 0xe7, 0xe5, 0xab, 0xd2, 0xf0, 0x11, 0x43, 0xd7, 
    0x2b, 0x78, 0x08, 0xc7, 0x4a, 0x66, 0xdc, 0xfb, 
    0x5f, 0xaf, 0x57, 0xa6, 0x71, 0x4b, 0xc6, 0x19 },
  { 0xe6, 0xac, 0xd3, 0xf1, 0x12, 0x44, 0xd8, 0x2c,
    0x79, 0x09, 0xc8, 0x4b, 0x67, 0xdd, 0xfc, 0x60,
    0xb0, 0x58, 0xa7, 0x72, 0x4c, 0xc7, 0x1a, 0x01 },
};

#endif /* __VCD_CD_SECTOR_PRIVATE_H__ */


/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
