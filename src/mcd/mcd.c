#define INCL_WININPUT
#define INCL_WINWINDOWMGR
#define INCL_WINMESSAGEMGR
#define INCL_DOSSEMAPHORES
#define INCL_OS2MM
#include <os2.h>
#include <os2me.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include "xine.h"
#include "xineutils.h"

#define USRM_SETSWP (WM_USER+110)

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

typedef struct {
  xine_t           *xine;
  xine_video_port_t      *vo_driver;
  xine_audio_port_t *ao_driver;
  xine_stream_t    *stream;
  xine_event_queue_t *event_queue;
  USHORT DeviceID;
  USHORT playuserparm;
  ULONG playflags;
  HWND playcallback;
  HEV playsem;
  int speedformat;
  int timeformat;
  int fps;
  int posadviseUnits;
  HWND posadviseCallback;
  USHORT posadviseUserParm;
  tvisual_type_pm visual_type_pm;
  char filename[200];
  char title[200];
  int cuetime;
  WNDPARAMS wndparams;
  int logfile;
//  FILE *f;
} tmcdxine;

ULONG statusconvert[4] = {MCI_MODE_NOT_READY,MCI_MODE_STOP,MCI_MODE_PLAY,MCI_MODE_NOT_READY};

ULONG WaitForEnd(tmcdxine *this) {
   if (this->playflags & MCI_WAIT) {
      DosWaitEventSem(this->playsem,SEM_INDEFINITE_WAIT);
   }
   return MCIERR_SUCCESS;
}

ULONG toMMFormat(tmcdxine *this, int time) {
   int out;
   
   switch (this->timeformat) {
      case MCI_FORMAT_MILLISECONDS: out=time; break;
      case MCI_FORMAT_MMTIME: out=time*3; break;
      case MCI_FORMAT_FRAMES: out=time * this->fps / 1000; break;
      default: out= 0;
   }
//   fprintf(this->f,"int time %d out time %d\n",time,out);
   return out;
}

int fromMMFormat(tmcdxine *this, ULONG time) {
   switch (this->timeformat) {
      case MCI_FORMAT_MILLISECONDS: return time;
      case MCI_FORMAT_MMTIME: return time / 3;
      case MCI_FORMAT_FRAMES: return time * 1000 / this->fps;
      default: return 0;
   }
}

void  mcd_listener(void *user_data, const xine_event_t *event) {
   tmcdxine *this = (tmcdxine *)user_data;
   int pos_stream;
   int pos_time;
   
   if (event->type==XINE_EVENT_UI_PLAYBACK_FINISHED) {
      xine_get_pos_length(this->stream,&pos_stream,&pos_time,&this->cuetime);
      DosPostEventSem(this->playsem);
      if (this->playflags & MCI_NOTIFY) {
         mdmDriverNotify(this->DeviceID,this->playcallback,MM_MCINOTIFY,
                      this->playuserparm,
                      MAKEULONG(MCI_PLAY,MCI_NOTIFY_SUCCESSFUL));

      }
   } else if (event->type==XINE_EVENT_CUE_POINT) {
      mdmDriverNotify(this->DeviceID,this->posadviseCallback,MM_MCIPOSITIONCHANGE,this->posadviseUserParm,((xine_cue_point_data_t *)event->data)->currtime*3);
   }
}
/*
void waiter(ULONG DeviceID) {
   MCI_GENERIC_PARMS GenericParms;

   sleep(10000);
   printf("Done\n");
}
*/

ULONG load(tmcdxine *this, PSZ filename) {
   int err;

   if (strncmp(".inet+file:///",filename,14)==0) {
      strcpy(this->filename,filename+14);
      if (this->filename[1]=='|') this->filename[1]=':';
   } else
   if (strncmp(".inet+",filename,6)==0) {
      strcpy(this->filename,(filename)+6);
   } else {
      strcpy(this->filename,filename);
   } /* endif */
   if (!xine_open(this->stream,this->filename)) {
      err=xine_get_error(this->stream);
      printf("Open error %d\n",err);
      if (err==XINE_ERROR_MALFORMED_MRL) {
         return MCIERR_FILE_NOT_FOUND;
      } else {
         return MCIERR_INVALID_MEDIA_TYPE;
      } /* endif */
   }
   if (this->timeformat==MCI_FORMAT_FRAMES) {
      this->fps=90000 / xine_get_stream_info(this->stream,XINE_STREAM_INFO_FRAME_DURATION);
   }
   return MCIERR_SUCCESS;
}


ULONG mciDriverEntry(tmcdxine *this, USHORT usMessage, ULONG ulParam1,
                     ULONG ulParam2, USHORT usUserParm) {
   ULONG PostCount;
   int pos_stream;
   int pos_time;
   int length_time;
   int stat;
   ULONG err;
   char *file;

   switch (usMessage) {
   case MCI_OPEN:
      this=malloc(sizeof(tmcdxine));
      ((MMDRV_OPEN_PARMS *)ulParam2)->pInstance=this;
      ((MMDRV_OPEN_PARMS *)ulParam2)->usResourceUnitsRequired=1;
//      this->f=fopen("/xine10/xine.log","w+t");
      /*      this->logfile=open("/xine.log",O_WRONLY|O_CREAT|O_TEXT,S_IREAD|S_IWRITE);
      dup2(this->logfile, STDOUT_FILENO);
      dup2(this->logfile, STDERR_FILENO);*/
//      fprintf(this->f,"debug done\n");
      this->DeviceID=((MMDRV_OPEN_PARMS *)ulParam2)->usDeviceID;
      this->timeformat=MCI_FORMAT_MMTIME;
      this->speedformat=MCI_FORMAT_FPS;
      this->posadviseUnits=0;
      this->visual_type_pm.defaultwindow=1;
      this->visual_type_pm.newwindow=1;
      this->visual_type_pm.hasput=0;
      this->cuetime=0;
      this->xine=xine_new();
//      fprintf(this->f,"new done\n");
      file=xine_home("xine.cfg");
      xine_config_load(this->xine,file);
//      fprintf(this->f,"config done\n");
      xine_init(this->xine);
//      printf("init done\n");
      this->vo_driver = xine_open_video_driver(this->xine,"dive",XINE_VISUAL_TYPE_PM,&(this->visual_type_pm));
      if (!this->vo_driver) return MCIERR_DRIVER;
//      printf("video done\n");
      this->ao_driver = xine_open_audio_driver(this->xine, "dart",NULL);
      if (!this->ao_driver) return MCIERR_DRIVER;
//      printf("audio done\n");
      this->stream = xine_stream_new(this->xine,this->ao_driver,this->vo_driver);
      this->event_queue = xine_event_new_queue(this->stream);
      xine_event_create_listener_thread(this->event_queue,mcd_listener,this);
//      printf("queue done\n");
      DosCreateEventSem(NULL,&(this->playsem),0,FALSE);
      if (ulParam1 & MCI_OPEN_ELEMENT) {
         err=load(this,((MMDRV_OPEN_PARMS *)ulParam2)->pszElementName);
         if (err!=MCIERR_SUCCESS) return err;
      }
//      printf("open done\n");
      break;
   case MCI_LOAD:
      if (!(ulParam1 & MCI_OPEN_ELEMENT)) return MCIERR_INVALID_MEDIA_TYPE;
      this->cuetime=0;
      err=load(this,((MCI_LOAD_PARMS *)ulParam2)->pszElementName);
      break;
   case MCI_PLAY:
      this->playuserparm=usUserParm;
      this->playflags=ulParam1;
      this->playcallback=((MCI_PLAY_PARMS *)ulParam2)->hwndCallback;
      if (this->visual_type_pm.defaultwindow) {
         SWP SendSWP;
         ULONG PostCt;
         
         SendSWP.fl=SWP_ZORDER;
         SendSWP.hwndInsertBehind=HWND_TOP;
         SendSWP.hwnd=this->visual_type_pm.Frame;
         DosResetEventSem(this->visual_type_pm.WindowReady,&PostCt);
         WinPostMsg(this->visual_type_pm.Client,USRM_SETSWP,0,&(SendSWP));
         DosWaitEventSem(this->visual_type_pm.WindowReady,SEM_INDEFINITE_WAIT);
         
      } /* endif */
      DosResetEventSem(this->playsem,&PostCount);
      xine_play(this->stream,0,this->cuetime);
      return WaitForEnd(this);
   case MCI_STOP:
      xine_get_pos_length(this->stream,&pos_stream,&this->cuetime,&length_time);
      xine_stop(this->stream);
      if (this->playflags & MCI_NOTIFY) 
         mdmDriverNotify(this->DeviceID,this->playcallback,MM_MCINOTIFY,
                      this->playuserparm,
                      MAKEULONG(MCI_PLAY,MCI_NOTIFY_ABORTED));
      break;
   case MCI_CLOSE:
      if (ulParam1 & MCI_CLOSE_EXIT) return MCIERR_SUCCESS;
      file=xine_home("xine.cfg");
      xine_config_save(this->xine,file);
/*      printf("close: quueue\n");*/
      xine_event_dispose_queue(this->event_queue);
/*      printf("close: stream\n");*/
      xine_close(this->stream);
/*      printf("close: dispose\n"); */
      xine_dispose(this->stream);
/*      printf("close: video\n");     */
      xine_close_video_driver(this->xine,this->vo_driver);
/*      printf("close: audio\n");       */
      xine_close_audio_driver(this->xine,this->ao_driver);
/*      printf("close: exit\n"); */
      xine_exit(this->xine);
/*      printf("close: fclose\n"); */
      close(this->logfile);
/*      printf("close: free\n");     */
      free(this);
//      printf("close: break\n");      
      break;
   case MCI_GETDEVCAPS:
      if (ulParam1 & MCI_GETDEVCAPS_MESSAGE) {
         switch (((MCI_GETDEVCAPS_PARMS *)(ulParam2))->usMessage) {
         case MCI_OPEN:
         case MCI_GETDEVCAPS:
         case MCI_PLAY:
         case MCI_CLOSE:
         case MCI_STOP:
         case MCI_LOAD:
         case MCI_ACQUIREDEVICE:
         case MCI_RELEASEDEVICE:
         case MCIDRV_RESTORE:
         case MCIDRV_SAVE:
         case MCI_CUE:
         case MCI_WINDOW:
         case MCI_SET:
         case MCI_STATUS:
            ((MCI_GETDEVCAPS_PARMS *)(ulParam2))->ulReturn=MCI_TRUE;
            break;
         default:
            ((MCI_GETDEVCAPS_PARMS *)(ulParam2))->ulReturn=MCI_FALSE;
         }
         return (MCI_TRUE_FALSE_RETURN << 16) | MCIERR_SUCCESS;
      }
      if (ulParam1 & MCI_GETDEVCAPS_ITEM) {
//         printf("getdevcaps item %d\n",((MCI_GETDEVCAPS_PARMS *)(ulParam2))->ulItem);
         switch (((MCI_GETDEVCAPS_PARMS *)(ulParam2))->ulItem) {
         case MCI_DGV_GETDEVCAPS_NORMAL_RATE:
            if (this->speedformat==MCI_FORMAT_FPS) {
               length_time=xine_get_stream_info(this->stream,XINE_STREAM_INFO_FRAME_DURATION);
               if (length_time) {
                  ((MCI_GETDEVCAPS_PARMS *)(ulParam2))->ulReturn=90000 / length_time;
               } else {
                  ((MCI_GETDEVCAPS_PARMS *)(ulParam2))->ulReturn=0;
               } /* endif */
            }
            else {((MCI_GETDEVCAPS_PARMS *)(ulParam2))->ulReturn=100;}
            return (MCI_SPEED_FORMAT_RETURN << 16) | MCIERR_SUCCESS;
         case MCI_GETDEVCAPS_USES_FILES:
         case MCI_GETDEVCAPS_CAN_PLAY:
         case MCI_GETDEVCAPS_CAN_SETVOLUME:
         case MCI_GETDEVCAPS_HAS_AUDIO:
         case MCI_GETDEVCAPS_HAS_VIDEO:
         case MCI_DGV_GETDEVCAPS_CAN_DISTORT:
         case MCI_DGV_GETDEVCAPS_CAN_STRETCH:
            ((MCI_GETDEVCAPS_PARMS *)(ulParam2))->ulReturn=MCI_TRUE;
            return (MCI_TRUE_FALSE_RETURN << 16) | MCIERR_SUCCESS;
         case MCI_GETDEVCAPS_CAN_EJECT:
         case MCI_GETDEVCAPS_CAN_LOCKEJECT:
         case MCI_GETDEVCAPS_CAN_PROCESS_INTERNAL:
         case MCI_GETDEVCAPS_CAN_RECORD:
         case MCI_GETDEVCAPS_CAN_RECORD_INSERT:
         case MCI_GETDEVCAPS_CAN_SAVE:
         case MCI_GETDEVCAPS_CAN_STREAM:
         case MCI_GETDEVCAPS_HAS_IMAGE:
         case MCI_DGV_GETDEVCAPS_CAN_REVERSE:
            ((MCI_GETDEVCAPS_PARMS *)(ulParam2))->ulReturn=MCI_FALSE;
            return (MCI_TRUE_FALSE_RETURN << 16) | MCIERR_SUCCESS;

         }
      }
      break;
   case MCI_STATUS:
      if (!(ulParam1 & MCI_STATUS_ITEM)) return MCIERR_UNSUPPORTED_FLAG;
//      fprintf(this->f,"Status Item: %ld\n",((MCI_STATUS_PARMS *)(ulParam2))->ulItem);
      switch (((MCI_STATUS_PARMS *)(ulParam2))->ulItem) {
      case MCI_STATUS_MEDIA_PRESENT:
      case MCI_STATUS_READY:
            stat=xine_get_status(this->stream);
            if ((stat==XINE_STATUS_STOP) || (stat==XINE_STATUS_PLAY)) {
//               fprintf(this->f,"Status Ready\n");
               ((MCI_STATUS_PARMS *)(ulParam2))->ulReturn=MCI_TRUE;
            } else {
//               fprintf(this->f,"Status Not Ready\n");
               ((MCI_STATUS_PARMS *)(ulParam2))->ulReturn=MCI_FALSE;
            } /* endif */
            return (MCI_TRUE_FALSE_RETURN << 16) | MCIERR_SUCCESS;
      case MCI_STATUS_VIDEO:
            ((MCI_STATUS_PARMS *)(ulParam2))->ulReturn=MCI_TRUE;
            return (MCI_TRUE_FALSE_RETURN << 16) | MCIERR_SUCCESS;
         break;
      case MCI_STATUS_LENGTH:
            xine_get_pos_length(this->stream,&pos_stream,&pos_time,&length_time);
            ((MCI_STATUS_PARMS *)(ulParam2))->ulReturn=toMMFormat(this,length_time);
            return (MCI_INTEGER_RETURNED << 16) | MCIERR_SUCCESS;
         break;
      case MCI_STATUS_POSITION:
      case MCI_STATUS_POSITION_IN_TRACK:
            stat=xine_get_status(this->stream);
            if (stat==XINE_STATUS_PLAY) {
               xine_get_pos_length(this->stream,&pos_stream,&pos_time,&length_time);
               ((MCI_STATUS_PARMS *)(ulParam2))->ulReturn=toMMFormat(this,pos_time);
            } else {
               ((MCI_STATUS_PARMS *)(ulParam2))->ulReturn=toMMFormat(this,this->cuetime);
//               fprintf(this->f,"Position from cuetime\n");
            } /* endif */
            return (MCI_INTEGER_RETURNED << 16) | MCIERR_SUCCESS;
         break;
      case MCI_STATUS_MODE:
            ((MCI_STATUS_PARMS *)(ulParam2))->ulReturn=statusconvert[xine_get_status(this->stream)];
//            printf("Mode: %d\n",xine_get_status(this->stream));
            return (MCI_MODE_RETURN << 16) | MCIERR_SUCCESS;
         break;
      case MCI_STATUS_TIME_FORMAT:
            ((MCI_STATUS_PARMS *)(ulParam2))->ulReturn=this->timeformat;
            return (MCI_TIME_FORMAT_RETURN << 16) | MCIERR_SUCCESS;
         break;
/*      case MCI_STATUS_AUDIO:
      case MCI_STATUS_VOLUME:
         return mciSendCommand(AmpOpenParms.usDeviceID,MCI_STATUS,ulParam1,(PVOID)ulParam2,usUserParm);
         break;*/
      case MCI_DGV_STATUS_VIDEO_X_EXTENT:
         return (xine_get_stream_info(this->stream,XINE_STREAM_INFO_VIDEO_WIDTH) << 16) | MCIERR_SUCCESS;
         break;
      case MCI_DGV_STATUS_VIDEO_Y_EXTENT:
         return (xine_get_stream_info(this->stream,XINE_STREAM_INFO_VIDEO_HEIGHT) << 16) | MCIERR_SUCCESS;
         break;
      case MCI_DGV_STATUS_HWND:
            ((MCI_STATUS_PARMS *)(ulParam2))->ulReturn=this->visual_type_pm.Client;
            return (MCI_TIME_FORMAT_RETURN << 16) | MCIERR_SUCCESS;
         break;
      default:
         return MCIERR_UNSUPPORTED_FLAG;
      }
      break;
   case MCI_SET:
      if (ulParam1 & MCI_SET_TIME_FORMAT) {
         this->timeformat=((MCI_SET_PARMS *)(ulParam2))->ulTimeFormat;
         if (this->timeformat==MCI_FORMAT_FRAMES) {
            this->fps=90000 / xine_get_stream_info(this->stream,XINE_STREAM_INFO_FRAME_DURATION);
         }
//         fprintf(this->f,"format now: %d\n",this->timeformat);
      }
      break;
   case MCI_WINDOW:
      if (ulParam1 & MCI_DGV_WINDOW_TEXT) {
         strcpy(this->title,((MCI_DGV_WINDOW_PARMS *)(ulParam2))->pszText);
         this->wndparams.fsStatus=WPM_TEXT;
         this->wndparams.cchText=strlen(this->title)+1;
         this->wndparams.pszText=this->title;
         this->wndparams.cbPresParams=0;
         this->wndparams.pPresParams=NULL;
         this->wndparams.cbCtlData=0;
         this->wndparams.pCtlData=NULL;
/*         printf("Set Window Text %d %s\n",this->wndparams.cchText,this->title);*/
         WinPostMsg(this->visual_type_pm.Frame,WM_SETWINDOWPARAMS,MPFROMP(&(this->wndparams)),0);
      } else if (ulParam1 & (MCI_DGV_WINDOW_DEFAULT | MCI_DGV_WINDOW_HWND)) {
         if (!(this->visual_type_pm.defaultwindow)) {
            WinSetVisibleRegionNotify(this->visual_type_pm.Display,FALSE);
            WinSubclassWindow(this->visual_type_pm.Display,this->visual_type_pm.oldProc);
         } /* endif */
         if (ulParam1 & MCI_DGV_WINDOW_DEFAULT) {
            this->visual_type_pm.defaultwindow=1;
         } else {
            this->visual_type_pm.defaultwindow=0;
            this->visual_type_pm.Display=((MCI_DGV_WINDOW_PARMS *)(ulParam2))->hwndDest;
         } /* endif */
         this->visual_type_pm.newwindow=1;
      }
      break;
   case MCI_CUE:
      if (ulParam1 & MCI_TO) {
         this->cuetime=fromMMFormat(this,((MCI_SEEK_PARMS *)(ulParam2))->ulTo);
      } /* endif */
      break;
   case MCI_SET_POSITION_ADVISE:
      if (ulParam1 & MCI_SET_POSITION_ADVISE_ON) {
         if (this->posadviseUnits>0) xine_event_dispose_cue_point(this->stream,0,this->posadviseUnits);
         this->posadviseUnits=fromMMFormat(this,((MCI_POSITION_PARMS *)(ulParam2))->ulUnits);
         this->posadviseUserParm=((MCI_POSITION_PARMS *)(ulParam2))->usUserParm;
         this->posadviseCallback=((MCI_POSITION_PARMS *)(ulParam2))->hwndCallback;
         xine_event_create_cue_point(this->stream,0,this->posadviseUnits,(void *)MCI_SET_POSITION_ADVISE);
//         fprintf(this->f,"Position Advise done\n");
      } else if (ulParam1 & MCI_SET_POSITION_ADVISE_OFF) {
         if (this->posadviseUnits>0) xine_event_dispose_cue_point(this->stream,0,this->posadviseUnits);
         this->posadviseUnits=0;
      }
      break;
/*   case MCI_SET_CUEPOINT:
      if (ulParam1 & MCI_SET_POSITION_ADVISE_ON) {
         xine_event_create_cue_point(this->stream,0,((MCI_CUEPOINT_PARMS *)(ulParam2))->ulCuepoint/timesize[this->timeformat],((MCI_CUEPOINT_PARMS *)(ulParam2))->usUserParm);
      } else if (ulParam1 & MCI_SET_POSITION_ADVISET_OFF) {
         xine_event_dispose_cue_point(this->stream,((MCI_CUEPOINT_PARMS *)(ulParam2))->ulCuepoint/timesize[this->timeformat],0);
      } 
      break;*/
   case MCI_PAUSE:
      xine_set_param(this->stream,XINE_PARAM_SPEED,XINE_SPEED_PAUSE);
      break;
   case MCI_RESUME:
      xine_set_param(this->stream,XINE_PARAM_SPEED,XINE_SPEED_NORMAL);
      break;
   case MCI_SEEK:
      xine_stop(this->stream);
      if (this->playflags & MCI_NOTIFY) 
         mdmDriverNotify(this->DeviceID,this->playcallback,MM_MCINOTIFY,
                      this->playuserparm,
                      MAKEULONG(MCI_PLAY,MCI_NOTIFY_ABORTED));
      if (ulParam1 & MCI_TO_START) this->cuetime=0;
      else if (ulParam1 & MCI_TO_END) xine_get_pos_length(this->stream,&pos_stream,&pos_time,&this->cuetime);
      else if (ulParam1 & MCI_TO) this->cuetime=fromMMFormat(this,((MCI_SEEK_PARMS *)(ulParam2))->ulTo);
      break;
   case MCI_PUT:
      if (ulParam1 & MCI_DGV_PUT_DESTINATION) {
         this->visual_type_pm.hasput=1;
         memcpy(&(this->visual_type_pm.putrect),&(((MCI_DGV_RECT_PARMS *)(ulParam2))->rc),sizeof(RECTL));
      } /* endif */
      break;
   case MCI_ACQUIREDEVICE:
   case MCI_RELEASEDEVICE:
   case MCIDRV_RESTORE:
   case MCIDRV_SAVE:
      break;
   default:
      return MCIERR_UNRECOGNIZED_COMMAND;
   }
   if (ulParam1 & MCI_NOTIFY) 
      mdmDriverNotify(this->DeviceID,((MCI_GENERIC_PARMS *)ulParam2)->hwndCallback,
                   MM_MCINOTIFY,usUserParm,
                   MAKEULONG(usMessage,MCI_NOTIFY_SUCCESSFUL));
   return MCIERR_SUCCESS;
}
