include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = $(X_CFLAGS) -fno-omit-frame-pointer \
	-Wmissing-prototypes -Wimplicit-function-declaration \
	-DWIN32_PATH=\"@w32_path@\" -DNOAVIFILE_HEADERS \
	-I$(srcdir)/.. -I$(srcdir)/../wine

if HAVE_W32DLL
ds_filter_lib = libds_filter.la
endif

noinst_LTLIBRARIES = $(ds_filter_lib)

libds_filter_la_SOURCES = \
	allocator.c \
	cmediasample.c \
	guids.c \
	inputpin.c \
	outputpin.c \
	DS_Filter.c \
	DS_AudioDecoder.c \
	DS_VideoDecoder.c

noinst_HEADERS = \
	allocator.h \
	cmediasample.h \
	guids.h \
	inputpin.h \
	interfaces.h \
	iunk.h \
	outputpin.h \
	DS_AudioDecoder.h \
	DS_Filter.h \
	DS_VideoDecoder.h
