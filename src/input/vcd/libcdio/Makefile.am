include $(top_srcdir)/misc/Makefile.common

SUBDIRS = cdio

INCLUDES = $(LIBCDIO_CFLAGS)

libcdio_SRCS = \
	_cdio_bincue.c \
	_cdio_bsdi.c \
	_cdio_freebsd.c \
	_cdio_generic.c \
	_cdio_linux.c \
	_cdio_osx.c \
	_cdio_nrg.c \
	_cdio_stdio.c \
	_cdio_stream.c \
	_cdio_sunos.c \
	_cdio_win32.c \
	cdio.c \
	cd_types.c \
	ds.c \
	logging.c \
	sector.c \
	util.c
libiso9660_SRCS = \
	iso9660.c \
	iso9660_fs.c \
	xa.c

EXTRA_DIST = $(libcdio_SRCS) $(libiso9660_SRCS)

if ENABLE_VCDX
if HAVE_VCDNAV
else
noinst_LTLIBRARIES = libcdio.la libiso9660.la
libcdio_la_SOURCES = $(libcdio_SRCS)
libcdio_la_LDFLAGS = -avoid-version -module
libiso9660_la_SOURCES = $(libiso9660_SRCS)
libiso9660_la_LDFLAGS = -avoid-version -module
endif
endif

noinst_HEADERS = \
	bytesex_asm.h \
	cdio_assert.h \
	_cdio_stdio.h \
	ds.h \
	scsi_mmc.h \
	bytesex.h \
	cdio_private.h \
	_cdio_stream.h \
	iso9660_private.h
