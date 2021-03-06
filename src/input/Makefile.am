include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = input_dvd.c input_vcd.c input_gnome_vfs.c input_rtp.c

if HAVE_DVDNAV
SUBDIRS = vcd dvb libreal librtsp
else
SUBDIRS = vcd dvb libreal librtsp libdvdnav
endif

libdir = $(XINE_PLUGINDIR)

##
# IMPORTANT:
# ---------
# All of xine input plugins should be named like the scheme "xineplug_inp_"
#

if HAVE_CDROM_IOCTLS
in_dvd = xineplug_inp_dvd.la
## build both vcd for now
##if ENABLE_VCDX
##else
in_vcd = xineplug_inp_vcdo.la
##endif
#in_cda = xineplug_inp_cda.la
endif

if HAVE_V4L
in_v4l = xineplug_inp_v4l.la
in_pvr = xineplug_inp_pvr.la
endif

if HAVE_GNOME_VFS
in_gnome_vfs = xineplug_inp_gnome_vfs.la
endif

# For DVD
if HAVE_DVDNAV
DVD_CFLAGS = $(DVDNAV_CFLAGS)
link_dvdnav = $(DVDNAV_LIBS)
else
DVD_CFLAGS = -I$(top_srcdir)/src/input/libdvdnav/
link_dvdnav = libdvdnav/libdvdnav.la
endif

AM_CFLAGS = -D_LARGEFILE64_SOURCE $(GNOME_VFS_CFLAGS) $(ALSA_CFLAGS) $(DVD_CFLAGS)

lib_LTLIBRARIES = \
	xineplug_inp_file.la \
	xineplug_inp_http.la \
	$(in_dvd) \
	$(in_vcd) \
	$(in_v4l) \
	$(in_gnome_vfs) \
	xineplug_inp_mms.la \
	xineplug_inp_stdin_fifo.la \
	xineplug_inp_pnm.la \
	xineplug_inp_rtp.la \
	xineplug_inp_rtsp.la \
	xineplug_inp_net.la \
	$(in_pvr) \
	xineplug_inp_dvb.la \
	xineplug_inp_cdda.la


xineplug_inp_file_la_SOURCES = input_file.c
xineplug_inp_file_la_LIBADD = $(XINE_LIB)
xineplug_inp_file_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_dvd_la_SOURCES = input_dvd.c media_helper.c
xineplug_inp_dvd_la_LIBADD = $(XINE_LIB) $(link_dvdnav)
xineplug_inp_dvd_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_net_la_SOURCES = input_net.c net_buf_ctrl.c
xineplug_inp_net_la_LIBADD = $(XINE_LIB)
xineplug_inp_net_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_mms_la_SOURCES = input_mms.c net_buf_ctrl.c mms.c mmsh.c
xineplug_inp_mms_la_LIBADD = $(XINE_LIB) @LIBICONV@
xineplug_inp_mms_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_vcdo_la_SOURCES = input_vcd.c media_helper.c
xineplug_inp_vcdo_la_LIBADD = $(XINE_LIB)
xineplug_inp_vcdo_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_stdin_fifo_la_SOURCES = input_stdin_fifo.c net_buf_ctrl.c
xineplug_inp_stdin_fifo_la_LIBADD = $(XINE_LIB)
xineplug_inp_stdin_fifo_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_rtp_la_SOURCES = input_rtp.c
xineplug_inp_rtp_la_LIBADD = $(XINE_LIB)
xineplug_inp_rtp_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_http_la_SOURCES = input_http.c net_buf_ctrl.c
xineplug_inp_http_la_LIBADD = $(XINE_LIB)
xineplug_inp_http_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_pnm_la_SOURCES = input_pnm.c net_buf_ctrl.c pnm.c
xineplug_inp_pnm_la_LIBADD = $(XINE_LIB)
xineplug_inp_pnm_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_dvb_la_SOURCES = input_dvb.c net_buf_ctrl.c
xineplug_inp_dvb_la_LIBADD = $(XINE_LIB)
xineplug_inp_dvb_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_rtsp_la_SOURCES = input_rtsp.c net_buf_ctrl.c
xineplug_inp_rtsp_la_LIBADD = $(XINE_LIB) libreal/libreal.la librtsp/librtsp.la
xineplug_inp_rtsp_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_cdda_la_SOURCES = input_cdda.c media_helper.c
xineplug_inp_cdda_la_LIBADD = $(XINE_LIB)
xineplug_inp_cdda_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_v4l_la_SOURCES = input_v4l.c
xineplug_inp_v4l_la_LIBADD = $(ALSA_LIBS) $(XINE_LIB)
xineplug_inp_v4l_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_gnome_vfs_la_SOURCES = input_gnome_vfs.c
xineplug_inp_gnome_vfs_la_LIBADD = $(GNOME_VFS_LIBS) $(XINE_LIB)
xineplug_inp_gnome_vfs_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_pvr_la_SOURCES = input_pvr.c
xineplug_inp_pvr_la_LIBADD = $(XINE_LIB)
xineplug_inp_pvr_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

include_HEADERS = input_plugin.h
noinst_HEADERS = net_buf_ctrl.h mms.h mmsh.h pnm.h media_helper.h videodev2.h
