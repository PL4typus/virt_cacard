EXTRA_DIST = autogen.sh

AM_CFLAGS = -Wall 
AM_CFLAGS += $(GLIB_CFLAGS)
AM_CFLAGS += $(CACARD_CFLAGS) 

bin_PROGRAMS = virt_cacard
virt_cacard_SOURCES = src/virt_cacard.c src/connection.c src/connection.h

virt_cacard_LDADD = $(AM_LDFLAGS)
virt_cacard_LDADD += $(GLIB_LIBS)
virt_cacard_LDADD += $(CACARD_LIBS)
