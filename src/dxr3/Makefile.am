include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = $(X_CFLAGS) $(LIBFAME_CFLAGS)

libdir = $(XINE_PLUGINDIR)

if HAVE_DXR3
dxr3_modules = xineplug_decode_dxr3_video.la \
	xineplug_decode_dxr3_spu.la \
	xineplug_vo_out_dxr3.la 
endif
if HAVE_X11
link_x_libs = $(X_LIBS) -lXext
endif
if HAVE_LIBFAME
link_fame = $(LIBFAME_LIBS)
endif
if HAVE_LIBRTE
link_rte = -lrte
endif

lib_LTLIBRARIES = $(dxr3_modules) 

xineplug_decode_dxr3_video_la_SOURCES = dxr3_decode_video.c
xineplug_decode_dxr3_video_la_LIBADD = $(XINE_LIB)
xineplug_decode_dxr3_video_la_LDFLAGS = -avoid-version -module

if HAVE_DVDNAV
AM_CPPFLAGS = $(DVDNAV_CFLAGS)
xineplug_decode_dxr3_spu_la_SOURCES = dxr3_decode_spu.c
xineplug_decode_dxr3_spu_la_LIBADD = $(XINE_LIB) $(DVDNAV_LIBS)
else
AM_CPPFLAGS = -I$(top_srcdir)/src/input/libdvdnav
xineplug_decode_dxr3_spu_la_SOURCES = dxr3_decode_spu.c nav_read.c
xineplug_decode_dxr3_spu_la_LIBADD = $(XINE_LIB)
endif
xineplug_decode_dxr3_spu_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_dxr3_la_SOURCES = \
	alphablend.c \
	dxr3_mpeg_encoders.c \
	dxr3_spu_encoder.c \
	dxr3_scr.c \
	video_out_dxr3.c 

xineplug_vo_out_dxr3_la_LIBADD = $(link_fame) $(link_rte) $(link_x_libs) $(XINE_LIB)
xineplug_vo_out_dxr3_la_LDFLAGS = -avoid-version -module

noinst_HEADERS = \
	em8300.h \
	alphablend.h \
	dxr3.h \
	dxr3_scr.h \
	video_out_dxr3.h
