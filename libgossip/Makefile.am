# We don't use DATADIR here because it conflicts on Windows.
AM_CPPFLAGS =                                           \
	-I.                                             \
	-I$(top_srcdir)                                 \
	-DSHAREDIR=\""$(datadir)"\"              	\
	-DLOCALEDIR=\""$(datadir)/locale"\"		\
	-DLM_COMPILATION				\
	$(GOSSIP_JABBER_CFLAGS)                         \
	$(LIBGOSSIP_CFLAGS) 				\
	$(LIBLOUDERMOUTH_CFLAGS)			\
	$(WARN_CFLAGS) 					\
	$(PLATFORM_CFLAGS)

noinst_LTLIBRARIES = 					\
	libgossip-jabber.la 				\
	libgossip.la

if HAVE_PLATFORM_X11
platform_impl = 					\
	gossip-conf-gnome.c				\
	gossip-paths-gnome.c
endif
if HAVE_PLATFORM_OSX
platform_impl = 					\
	gossip-conf-cocoa.c				\
	gossip-paths-cocoa.c
endif
if HAVE_PLATFORM_WIN32
platform_impl = 					\
	gossip-conf-windows.c				\
	gossip-paths-windows.c

libgossip_la_LDFLAGS = -lregex
endif

if HAVE_EBOOK
ebook_sources =						\
	gossip-ebook.h					\
	gossip-ebook.c
endif

libgossip_la_SOURCES =             			\
	$(ebook_sources)				\
	gossip.h                                        \
	gossip-account.c           			\
	gossip-account.h           			\
	gossip-account-manager.c   			\
	gossip-account-manager.h   			\
	gossip-async.c             			\
	gossip-async.h             			\
	gossip-avatar.c           			\
	gossip-avatar.h           			\
	gossip-chatroom.c				\
	gossip-chatroom.h				\
	gossip-chatroom-invite.c			\
	gossip-chatroom-invite.h			\
	gossip-chatroom-manager.c 			\
	gossip-chatroom-manager.h 			\
	gossip-chatroom-provider.c 			\
	gossip-chatroom-provider.h 			\
	gossip-contact.c           			\
	gossip-contact.h           			\
	gossip-contact-manager.c       			\
	gossip-contact-manager.h       			\
	gossip-debug.c					\
	gossip-debug.h					\
	gossip-event.c             			\
	gossip-event.h             			\
	gossip-event-manager.c     			\
	gossip-event-manager.h     			\
	gossip-ft.c		 			\
	gossip-ft.h 					\
	gossip-ft-provider.c	 			\
	gossip-ft-provider.h 				\
	gossip-log.c					\
	gossip-log.h					\
	gossip-message.c           			\
	gossip-message.h           			\
	gossip-conf.h          				\
	gossip-paths.h         				\
	gossip-presence.c          			\
	gossip-presence.h          			\
	gossip-private.h				\
	gossip-session.c           			\
	gossip-session.h           			\
	gossip-stock.c                                  \
	gossip-stock.h                                  \
	gossip-time.c              			\
	gossip-time.h              			\
	gossip-utils.c             			\
	gossip-utils.h					\
	gossip-vcard.c             			\
	gossip-vcard.h             			\
	gossip-version-info.c      			\
	gossip-version-info.h      			\
	libgossip-marshal-main.c			\
	$(platform_impl)

libgossip_la_LIBADD =              			\
	libgossip-jabber.la				\
	$(LIBGOSSIP_LIBS)

libgossipincludedir = $(includedir)/gossip/

libgossip_jabber_la_SOURCES =                  		\
	gossip-jabber.c                         	\
	gossip-jabber.h                         	\
	gossip-jabber-chatrooms.c               	\
	gossip-jabber-chatrooms.h               	\
	gossip-jabber-disco.c				\
	gossip-jabber-disco.h				\
	gossip-jabber-ft.c				\
	gossip-jabber-ft.h				\
	gossip-jabber-ft-utils.c			\
	gossip-jabber-ft-utils.h			\
	gossip-jabber-ns.h                              \
	gossip-jabber-private.h                 	\
	gossip-jabber-vcard.c				\
	gossip-jabber-vcard.h				\
	gossip-jabber-register.c			\
	gossip-jabber-register.h			\
	gossip-jabber-services.c			\
	gossip-jabber-services.h			\
	gossip-jabber-utils.c				\
	gossip-jabber-utils.h				\
	gossip-jid.c                            	\
	gossip-jid.h					\
	gossip-sha.c                            	\
	gossip-sha.h

libgossip_jabber_la_LIBADD = 				\
	$(top_builddir)/libloudermouth/libloudermouth.la	\
	$(GOSSIP_JABBER_LIBS)

dtddir = $(datadir)/gossip
dtd_DATA = 						\
	gossip-account.dtd				\
	gossip-contact.dtd				\
	gossip-chatroom.dtd

stylesheetdir = $(datadir)/gossip
stylesheet_DATA = gossip-log.xsl

libgossip-marshal.h: libgossip-marshal.list
	(cd $(srcdir) && \
	$(GLIB_GENMARSHAL) --prefix=libgossip_marshal libgossip-marshal.list --header) > xgen-gmh \
	&& (cmp -s xgen-gmh libgossip-marshal.h || cp xgen-gmh libgossip-marshal.h) \
	&& rm -f xgen-gmh xgen-gmh~

libgossip-marshal.c: libgossip-marshal.list
	(cd $(srcdir) && \
	$(GLIB_GENMARSHAL) --prefix=libgossip_marshal libgossip-marshal.list --body) > xgen-gmc \
	&& cp xgen-gmc libgossip-marshal.c \
	&& rm -f xgen-gmc xgen-gmc~

libgossip-marshal-main.c: libgossip-marshal.c libgossip-marshal.h

BUILT_SOURCES = 					\
	libgossip-marshal.h				\
	libgossip-marshal.c

EXTRA_DIST =						\
	$(dtd_DATA)					\
	$(stylesheet_DATA)				\
	libgossip-marshal.list

CLEANFILES = $(BUILT_SOURCES)
