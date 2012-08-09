/*
 * Copyright (C) 2000, 2001 the xine project
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
 * $Id: video_out_dive.c,v 1.3 2002/01/15 20:39:39 jcdutton Exp $
 * 
 * video_out_dive.c, OS/2 DIVE xine driver by Darwin O'Connor
 * <doconnor@reamined.on.ca>
 *
 * based on
 * video_out_fb.c, frame buffer xine driver by Miguel Freitas
 *
 * based on xine's video_out_xshm.c...
 * ...based on mpeg2dec code from
 * Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * ideas from ppmtofb - Display P?M graphics on framebuffer devices
 *            by Geert Uytterhoeven and Chris Lawrence
 *
 */

#define INCL_OS2MM
#define INCL_WININPUT
#define INCL_WINWINDOWMGR
#define INCL_WINMESSAGEMGR
#define INCL_DOSSEMAPHORES
#define INCL_WINFRAMEMGR
#define INCL_WINMENUS

#include <os2.h>
#include <os2me.h>
#include <dive.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#include "xine_internal.h"
/*#include "xineutils.h"*/

#define USRM_SETSWP (WM_USER+110)

typedef struct dive_frame_s {
  vo_frame_t         vo_frame;
  ULONG BufferNum;
  FOURCC colorformat;
  int xineformat;
  ULONG ScanLineBytes;
  ULONG srcsizex;
  ULONG srcsizey;
} dive_frame_t;

typedef struct {
  HWND Client;
  HWND Frame;
  HWND Display;
  PFNWP oldProc;
  int defaultwindow;
  int newwindow;
  HEV WindowReady;
  int hasput;
  RECTL putrect;
} tvisual_type_pm;

typedef struct dive_driver_s {

  vo_driver_t      vo_driver;
  HDIVE DiveInst;
  LONG posx;
  LONG posy;
  ULONG winsizex;
  ULONG winsizey;
  FOURCC colorformat;
  ULONG ScanLineBytes;
  ULONG srcsizex;
  ULONG srcsizey;
  BOOL SetupBlitter;
  HAB PMab;
  HMQ PMmq;
  FOURCC ScreenFormat;
  tvisual_type_pm *visual_type_pm;
  ULONG rctlCount;
  HEV WindowReady;
  RECTL rctls[50];
} dive_driver_t;

typedef struct dive_list_s {
  HWND wnd;
  dive_driver_t *driver;
  struct dive_list_s *next;
} dive_list_t;

typedef struct {

  video_driver_class_t driver_class;
  config_values_t     *config;

} dive_class_t;

/*
 * first, some utility functions
 */
vo_info_t *get_video_out_plugin_info();

dive_list_t *list_first = NULL;

dive_driver_t *getdriver(HWND wnd) {
   dive_list_t *curr;

   curr=list_first;
     while (!curr && curr->wnd!=wnd) {
        curr=curr->next;
     } /* endwhile */
   return curr->driver;  
}
   

ULONG DiveCheck(ULONG rc) {
   if (rc!=DIVE_SUCCESS) 
      printf("Dive Error: %lX\n",rc); 
   return rc;
}
   

MRESULT xineDisplayProc(HWND wnd, ULONG msg, MPARAM mp1, MPARAM mp2) {
   HPS ps;
   RECTL RectL;
   dive_driver_t *this;
   RGNRECT rgnCtl;
   HRGN hrgn;
   SWP swp;
   POINTL pointl;
   
   this=getdriver(wnd);
   switch (msg) {
   case WM_PAINT:
      ps=WinBeginPaint(wnd,NULLHANDLE,&RectL);
      WinFillRect(ps,&RectL,CLR_BLACK);
      WinEndPaint(wnd);
      break;
   case WM_VRNDISABLED:
      DiveCheck(DiveSetupBlitter(this->DiveInst,NULL));
      break;
   case WM_VRNENABLED:
      ps=WinGetPS(wnd);
      hrgn=GpiCreateRegion(ps,0,NULL);
      WinQueryVisibleRegion(wnd,hrgn);
      rgnCtl.ircStart=0;
      rgnCtl.crc=50;
      rgnCtl.ulDirection=RECTDIR_LFRT_TOPBOT;
      GpiQueryRegionRects(ps,hrgn,NULL,&rgnCtl,this->rctls);
      this->rctlCount=rgnCtl.crcReturned;
      WinQueryWindowPos(wnd,&swp);
      this->winsizex=swp.cx;
      this->winsizey=swp.cy;
      pointl.x=0;
      pointl.y=0;
      WinMapWindowPoints(wnd,HWND_DESKTOP,&pointl,1);
      this->posx=pointl.x;
      this->posy=pointl.y;
      this->SetupBlitter=TRUE;
      WinReleasePS(ps);
      printf("VRNENABLED\n");
      break;
   case USRM_SETSWP:
/*      printf("SetSwp Start\n");
      printf("SetSwp this %d %d\n",this,this->SendSWP.cx);*/
      if (((SWP *)mp2)->fl & SWP_SIZE) {
         RectL.xLeft=0;
         RectL.yBottom=0;
         RectL.xRight=((SWP *)mp2)->cx;
         RectL.yTop=((SWP *)mp2)->cy;
         WinCalcFrameRect(this->visual_type_pm->Frame,&RectL,FALSE);
         ((SWP *)mp2)->cx=RectL.xRight-RectL.xLeft;
         ((SWP *)mp2)->cy=RectL.yTop-RectL.yBottom;
      } /* endif */
      WinSetMultWindowPos(this->PMab,(SWP *)mp2,1);
/*      printf("SetSwp End\n");*/
      DosPostEventSem(this->WindowReady);
      break;
   default: 
      return (this->visual_type_pm->oldProc)(wnd,msg,mp1,mp2);
   } /* endswitch */
   return 0;
}

void divemain(dive_driver_t *this) {
   ULONG CreateFlags;
   QMSG     qmsg;
   DIVE_CAPS dive_caps;
   char formats[100];

   dive_caps.ulStructLen=sizeof(DIVE_CAPS);
   dive_caps.ulFormatLength=100;
   dive_caps.pFormatData=formats;
   DiveCheck(DiveQueryCaps(&dive_caps,DIVE_BUFFER_SCREEN));
   this->ScreenFormat=dive_caps.fccColorEncoding;
   DiveCheck(DiveOpen(&(this->DiveInst),FALSE,NULL));
   this->visual_type_pm->oldProc=WinDefWindowProc;
   this->PMab=WinInitialize(0);
   this->PMmq=WinCreateMsgQueue(this->PMab,0);
   WinRegisterClass(this->PMab,"XINEDISPLAY",xineDisplayProc,0,4);
   CreateFlags=FCF_TITLEBAR+FCF_SYSMENU+FCF_SIZEBORDER+FCF_MINBUTTON+FCF_MAXBUTTON+FCF_SHELLPOSITION+FCF_TASKLIST;
   this->visual_type_pm->Frame=WinCreateStdWindow(HWND_DESKTOP,0,&CreateFlags,"XINEDISPLAY","xine",0,0,10,&(this->visual_type_pm->Client));
   WinSendMsg(WinWindowFromID(this->visual_type_pm->Frame,FID_SYSMENU),MM_SETITEMATTR,
         MPFROM2SHORT(SC_CLOSE,1),MPFROM2SHORT(MIA_DISABLED,MIA_DISABLED));
   DosPostEventSem(this->WindowReady);
   while(WinGetMsg(this->PMab,&qmsg,(HWND)NULL,0,0))
      WinDispatchMsg(this->PMab,&qmsg);
   DosPostEventSem(this->WindowReady);
   WinDestroyMsgQueue(this->PMmq);
   WinTerminate(this->PMab);
}

/*
 * and now, the driver functions
 */

static uint32_t dive_get_capabilities (vo_driver_t *this_gen) {
  return VO_CAP_YV12 | VO_CAP_YUY2;
}

static void dive_frame_copy (vo_frame_t *vo_img, uint8_t **src) {
/*  dive_frame_t  *frame = (dive_frame_t *) vo_img ;
  dive_driver_t *this = (dive_driver_t *) vo_img->instance->driver;*/

}

static void dive_frame_field (vo_frame_t *vo_img, int which_field) {
}

static void dive_frame_dispose (vo_frame_t *vo_img) {

  dive_frame_t  *frame = (dive_frame_t *) vo_img ;
   dive_driver_t *this = (dive_driver_t *) vo_img->driver; 

  if (frame->BufferNum!=0) {
     DiveCheck(DiveFreeImageBuffer(this->DiveInst,frame->BufferNum));
  } /* endif */

  free (frame);
}


static vo_frame_t *dive_alloc_frame (vo_driver_t *this_gen) {
  dive_frame_t   *frame ;

  frame = (dive_frame_t *) malloc (sizeof (dive_frame_t));
  if (frame==NULL) {
    printf ("dive_alloc_frame: out of memory\n");
    return NULL;
  }

  memset (frame, 0, sizeof(dive_frame_t));

  /*
   * supply required functions
   */
  
  frame->vo_frame.proc_slice = NULL;
  frame->vo_frame.proc_frame = NULL;
  frame->vo_frame.field   = dive_frame_field; 
  frame->vo_frame.dispose = dive_frame_dispose;
  frame->vo_frame.driver = this_gen;
  
  
  return (vo_frame_t *) frame;
}

static void dive_update_frame_format (vo_driver_t *this_gen,
				      vo_frame_t *frame_gen,
				      uint32_t width, uint32_t height,
				      double ratio, int format, int flags) {

  dive_driver_t  *this = (dive_driver_t *) this_gen;
  dive_frame_t   *frame = (dive_frame_t *) frame_gen;
  ULONG ScanLines;
  PBYTE Buffer;

  if (frame->srcsizex==width && frame->srcsizey==height && frame->xineformat==format) {
     return;
  }

  if (format==XINE_IMGFMT_YV12) frame->colorformat=mmioFOURCC( 'Y', '2', 'X', '2' );
  else if (format==XINE_IMGFMT_YUY2) frame->colorformat=mmioFOURCC('Y','4','2','2');

/*  printf("dive_update_frame_format format: %X %lX\n",format,frame->colorformat);*/
  if (frame->BufferNum!=0) {
     DiveCheck(DiveFreeImageBuffer(this->DiveInst,frame->BufferNum));
  } /* endif */
  frame->srcsizex=width;
  frame->srcsizey=height;
  frame->xineformat=format;
  DiveCheck(DiveAllocImageBuffer(this->DiveInst,&(frame->BufferNum),frame->colorformat,width,height,0,NULL));
  DiveCheck(DiveBeginImageBufferAccess(this->DiveInst,frame->BufferNum,&Buffer,&(frame->ScanLineBytes),&ScanLines));
  frame->vo_frame.base[0]=Buffer;
  if (format==XINE_IMGFMT_YV12) {
     frame->vo_frame.pitches[0] = width;//8*((width + 7) / 8);
     frame->vo_frame.pitches[1] = width/2;//8*((width + 15) / 16);
     frame->vo_frame.pitches[2] = width/2;//8*((width + 15) / 16);
     frame->vo_frame.base[2]=Buffer+width*height;
     frame->vo_frame.base[1]=Buffer+width*height*5/4;
  } else {
     frame->vo_frame.pitches[0] = 8*((width + 3) / 4);
  }
}                                                                     

static void dive_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay) {
/*  dive_driver_t  *this = (dive_driver_t *) this_gen;
  dive_frame_t   *frame = (dive_frame_t *) frame_gen;*/

}

static void dive_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {

  dive_driver_t  *this = (dive_driver_t *) this_gen;
  dive_frame_t   *frame = (dive_frame_t *) frame_gen;
  SETUP_BLITTER SetupBlitter;
  ULONG PostCt;
  dive_list_t *curr;
  SWP SendSWP;

  if (this->visual_type_pm->newwindow) {
     printf("New Window Begin\n");
     if (this->visual_type_pm->defaultwindow) {
        this->visual_type_pm->Display=this->visual_type_pm->Client;
     } 
     curr=list_first;
     while (!curr && curr->driver!=this) {
        curr=curr->next;
     } /* endwhile */
     curr->wnd=this->visual_type_pm->Client;
/*     printf("getdriver: curr %ld wnd %ld driver %ld\n",curr,curr->wnd,curr->driver);*/
     if (!this->visual_type_pm->defaultwindow) {
        this->visual_type_pm->oldProc=WinSubclassWindow(this->visual_type_pm->Display,xineDisplayProc);
     } else {
        this->visual_type_pm->oldProc=WinDefWindowProc;
     }
     WinSetVisibleRegionNotify(this->visual_type_pm->Display,TRUE);
     if (this->visual_type_pm->defaultwindow) {
        SendSWP.fl=SWP_SHOW|SWP_SIZE|SWP_ACTIVATE|SWP_ZORDER;
        SendSWP.cx=frame->srcsizex;
        SendSWP.cy=frame->srcsizey;
        SendSWP.hwnd=this->visual_type_pm->Frame;
        SendSWP.hwndInsertBehind=HWND_TOP;
        DosResetEventSem(this->visual_type_pm->WindowReady,&PostCt);
        WinPostMsg(this->visual_type_pm->Client,USRM_SETSWP,0,&(SendSWP));
        DosWaitEventSem(this->visual_type_pm->WindowReady,SEM_INDEFINITE_WAIT);
     } else {
        WinPostMsg(this->visual_type_pm->Display,WM_VRNENABLED,0,0);
     }
     this->visual_type_pm->newwindow=0;
     printf("New Window End\n");
  } /* endif */
  if (this->SetupBlitter || this->srcsizex!=frame->srcsizex || this->srcsizey!=frame->srcsizey || this->colorformat!=frame->colorformat) {
     printf("New Blitter Begin\n");
     SetupBlitter.ulStructLen=sizeof(SETUP_BLITTER);
     SetupBlitter.fInvert=0;
     SetupBlitter.fccSrcColorFormat=frame->colorformat;
     SetupBlitter.ulSrcWidth=frame->srcsizex;
     SetupBlitter.ulSrcHeight=frame->srcsizey;
     SetupBlitter.ulSrcPosX=0;
     SetupBlitter.ulSrcPosY=0;
     SetupBlitter.ulDitherType=0;
     SetupBlitter.fccDstColorFormat=this->ScreenFormat;
     if (!this->visual_type_pm->hasput) {
        SetupBlitter.ulDstWidth=this->winsizex;
        SetupBlitter.ulDstHeight=this->winsizey;
        SetupBlitter.lDstPosX=0;
        SetupBlitter.lDstPosY=0;
     } else {
        SetupBlitter.ulDstWidth=this->visual_type_pm->putrect.xRight-this->visual_type_pm->putrect.xLeft;
        SetupBlitter.ulDstHeight=this->visual_type_pm->putrect.yTop-this->visual_type_pm->putrect.yBottom;
        SetupBlitter.lDstPosX=this->visual_type_pm->putrect.xLeft;
        SetupBlitter.lDstPosY=this->visual_type_pm->putrect.yBottom;
     } /* endif */
     SetupBlitter.lScreenPosX=this->posx;
     SetupBlitter.lScreenPosY=this->posy;
     printf("Dest: %d %d %d %d %d %d\n",SetupBlitter.ulDstWidth,SetupBlitter.ulDstHeight,SetupBlitter.lDstPosX,SetupBlitter.lDstPosY,SetupBlitter.lScreenPosX,SetupBlitter.lScreenPosY);
     SetupBlitter.ulNumDstRects=this->rctlCount;
     SetupBlitter.pVisDstRects=this->rctls;
     DiveCheck(DiveSetupBlitter(this->DiveInst,&SetupBlitter));
     this->srcsizex=frame->srcsizex;
     this->srcsizey=frame->srcsizey;
     this->ScanLineBytes=frame->ScanLineBytes;
     this->colorformat=frame->colorformat;
     this->SetupBlitter=FALSE;
     printf("New End Begin\n");
  } /* endif */
  DiveCheck(DiveBlitImage(this->DiveInst,frame->BufferNum,DIVE_BUFFER_SCREEN));
  frame->vo_frame.free(&frame->vo_frame);
}

static int dive_get_property (vo_driver_t *this_gen, int property) {

/*  dive_driver_t *this = (dive_driver_t *) this_gen;*/

  return 0;
}

static int dive_set_property (vo_driver_t *this_gen, 
			      int property, int value) {

/*  dive_driver_t *this = (dive_driver_t *) this_gen;*/

  return value;
}

static void dive_get_property_min_max (vo_driver_t *this_gen,
				     int property, int *min, int *max) {

  /* dive_driver_t *this = (dive_driver_t *) this_gen;  */
    *min = 0;
    *max = 0;
}


static int dive_gui_data_exchange (vo_driver_t *this_gen, 
				 int data_type, void *data) {

  return 0;
}


static void dive_dispose (vo_driver_t *this_gen) {
  dive_driver_t *this = (dive_driver_t *) this_gen;
  dive_list_t **curr;
  dive_list_t *todel;
  ULONG PostCt;
  
  DosResetEventSem(this->WindowReady,&PostCt);
  WinPostMsg(this->visual_type_pm->Frame,WM_CLOSE,0,0);
  DosWaitEventSem(this->WindowReady,1000/*SEM_INDEFINITE_WAIT*/);
  DosCloseEventSem(this->WindowReady);
  DiveCheck(DiveClose(this->DiveInst));
  curr=&list_first;
  while (*curr) {
     if ((*curr)->driver==this) {
        todel=*curr;  
        (*curr)=(*curr)->next;
        free(todel);
     } else {
        curr=&((*curr)->next);
     } /* endif */
  } /* endwhile */
}

static int dive_redraw_needed (vo_driver_t *this_gen) {
  return 0;
}

static vo_driver_t *open_plugin (video_driver_class_t *class_gen, const void *visual_gen) {
/*  dive_class_t           *class = (dive_class_t *) class_gen;*/
  dive_driver_t        *this;
  dive_list_t *newlist;
  /*
   * allocate plugin struct
   */
  this = malloc (sizeof (dive_driver_t));

  memset (this, 0, sizeof(dive_driver_t));

/*  this->context = (dive_context*) visual_gen;*/
  
/*  this->config = class->config;*/
  this->vo_driver.get_capabilities     = dive_get_capabilities;
  this->vo_driver.alloc_frame          = dive_alloc_frame;
  this->vo_driver.update_frame_format  = dive_update_frame_format;
  this->vo_driver.display_frame        = dive_display_frame;
  this->vo_driver.overlay_begin        = NULL;
  this->vo_driver.overlay_blend        = dive_overlay_blend;
  this->vo_driver.overlay_end          = NULL;
  this->vo_driver.get_property         = dive_get_property;
  this->vo_driver.set_property         = dive_set_property;
  this->vo_driver.get_property_min_max = dive_get_property_min_max;
  this->vo_driver.gui_data_exchange    = dive_gui_data_exchange;
  this->vo_driver.redraw_needed        = dive_redraw_needed;
  this->vo_driver.dispose              = dive_dispose;
  this->SetupBlitter=FALSE;
  this->visual_type_pm=(tvisual_type_pm *)visual_gen;
  newlist=malloc(sizeof(dive_list_t));
  newlist->driver=this;
  newlist->next=list_first;
  printf("visual_type_pm: %ld\n",this->visual_type_pm);
  list_first=newlist;
  DosCreateEventSem(NULL,&(this->WindowReady),0,FALSE);
  this->visual_type_pm->WindowReady=this->WindowReady;
  _beginthread(divemain,NULL,32768,this);
  DosWaitEventSem(this->WindowReady,SEM_INDEFINITE_WAIT);
  return &this->vo_driver;
}

static char* get_identifier (video_driver_class_t *this_gen) {
  return "dive";
}

static char* get_description (video_driver_class_t *this_gen) {
  return _("xine video output plugin for OS/2 DIVE");
}

static void dispose_class (video_driver_class_t *this_gen) {
  dive_class_t   *this = (dive_class_t *) this_gen;
  free(this);
}
static void *init_class (xine_t *xine, void *visual_gen) {
  dive_class_t    *this;

  this = (dive_class_t *) malloc(sizeof(dive_class_t));
  
  this->driver_class.open_plugin     = open_plugin;
  this->driver_class.get_identifier  = get_identifier;
  this->driver_class.get_description = get_description;
  this->driver_class.dispose         = dispose_class;
  
  this->config            = xine->config;

  return this;
}

static vo_info_t vo_info_dive = {
  6,
  XINE_VISUAL_TYPE_PM
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_OUT, 18, "dive", XINE_VERSION_CODE, &vo_info_dive, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

