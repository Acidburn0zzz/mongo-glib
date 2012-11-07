include Makefile.tests
include mongo-glib/Makefile.include
include tests/Makefile.include

SUBDIRS = . doc

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}
DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc --enable-introspection=yes --enable-silent-rules

mongodocdir = ${datadir}/doc/mongo-glib-@VERSION@
mongodoc_DATA =
mongodoc_DATA += COPYING
mongodoc_DATA += NEWS
mongodoc_DATA += README.md

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA =
pkgconfig_DATA += $(top_builddir)/mongo-glib-1.0.pc

EXTRA_DIST += $(mongodoc_DATA)

dist-hook:
	@if test -d "$(srcdir)/.git"; then                              \
          (cd "$(srcdir)" &&                                            \
           $(top_srcdir)/missing --run git log --stat ) > ChangeLog.tmp \
           && mv -f ChangeLog.tmp $(top_distdir)/ChangeLog              \
           || (rm -f ChangeLog.tmp;                                     \
               echo Failed to generate ChangeLog >&2);                  \
	else                                                            \
	  echo A git checkout is required to generate a ChangeLog >&2;  \
	fi

uninstall-local:
	-rm -r $(mongodocdir)
