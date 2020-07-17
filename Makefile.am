EXTRA_DIST = setup-softhsm2.sh 

AM_CFLAGS = -Wall 
AM_CFLAGS += $(GLIB_CFLAGS)		\
	-DG_LOG_DOMAIN=\"virt_cacard\"
AM_CFLAGS += $(CACARD_CFLAGS) 

noinst_PROGRAMS = virt_cacard
virt_cacard_SOURCES = src/virt_cacard.c src/connection.c src/connection.h src/glib-compat.h $(NULL) 

virt_cacard_LDADD = $(AM_LDFLAGS)
virt_cacard_LDADD += $(GLIB_LIBS)
virt_cacard_LDADD += $(CACARD_LIBS)
