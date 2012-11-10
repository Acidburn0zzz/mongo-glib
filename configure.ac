dnl **************************************************************************
dnl Versioning Macros
dnl **************************************************************************
m4_define([mongo_major_version], [0])
m4_define([mongo_minor_version], [3])
m4_define([mongo_micro_version], [5])
m4_define([mongo_version],
          [mongo_major_version.mongo_minor_version.mongo_micro_version])
m4_define([mongo_binary_age], [m4_eval(100 * mongo_minor_version +
           mongo_micro_version)])
m4_define([lt_current], [m4_eval(100 * mongo_minor_version +
           mongo_micro_version - mongo_interface_age)])
m4_define([lt_revision], [mongo_interface_age])
m4_define([lt_age], [m4_eval(mongo_binary_age - mongo_interface_age)])


dnl **************************************************************************
dnl Initialize Automake
dnl **************************************************************************
AC_PREREQ([2.64])
AC_INIT([mongo-glib],[mongo_version],[https://github.com/chergert/mongo-glib])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES])
AS_AM_REALLY_SILENT

# Initialize libtool
LT_PREREQ([2.2])
LT_INIT

AM_INIT_AUTOMAKE([1.11.1 no-dist-gzip dist-xz -Wno-portability])


dnl **************************************************************************
dnl Versioning Definitions
dnl **************************************************************************
MONGO_MAJOR_VERSION=mongo_major_version
MONGO_MINOR_VERSION=mongo_minor_version
MONGO_MICRO_VERSION=mongo_micro_version
MONGO_API_VERSION=1.0
MONGO_VERSION=mongo_version
AC_SUBST(MONGO_MAJOR_VERSION)
AC_SUBST(MONGO_MINOR_VERSION)
AC_SUBST(MONGO_MICRO_VERSION)
AC_SUBST(MONGO_API_VERSION)
AC_SUBST(MONGO_VERSION)


dnl **************************************************************************
dnl Check for Required Modules
dnl **************************************************************************
PKG_CHECK_MODULES(GIO,     [gio-2.0 >= 2.32])
PKG_CHECK_MODULES(GOBJECT, [gobject-2.0 >= 2.32])


dnl **************************************************************************
dnl Check for libunistring
dnl **************************************************************************
AC_CHECK_HEADERS([unistr.h], [
	utf8_support="libunistring; full UTF-8 support."
], [
	utf8_support="glib; only modified UTF-8."
])


dnl **************************************************************************
dnl Enable extra debugging options
dnl **************************************************************************
m4_define([debug_default],
	  [m4_if(m4_eval(mongo_minor_version % 2), [1],
	  [yes], [minimum])])
AC_ARG_ENABLE([debug],
	      [AS_HELP_STRING([--enable-debug=@<:@no/minimum/yes@:>@],
	      		      [turn on debugging @<:@default=debug_default@:>@])],
	      		      [],
	      		      [enable_debug=debug_default])
AS_CASE([$enable_debug],
	[yes], [
		CFLAGS="$CFLAGS -g"
		CFLAGS="$CFLAGS -Wall"
		CFLAGS="$CFLAGS -Werror"
		CFLAGS="$CFLAGS -Wold-style-definition"
		CFLAGS="$CFLAGS -Wdeclaration-after-statement"
		CFLAGS="$CFLAGS -Wredundant-decls"
		CFLAGS="$CFLAGS -Wmissing-noreturn"
		CFLAGS="$CFLAGS -Wcast-align"
		CFLAGS="$CFLAGS -Wwrite-strings"
		CFLAGS="$CFLAGS -Winline"
		CFLAGS="$CFLAGS -Wformat-nonliteral"
		CFLAGS="$CFLAGS -Wformat-security"
		CFLAGS="$CFLAGS -Wswitch-enum"
		CFLAGS="$CFLAGS -Wswitch-default"
		CFLAGS="$CFLAGS -Winit-self"
		CFLAGS="$CFLAGS -Wmissing-include-dirs"
		CFLAGS="$CFLAGS -Wundef"
		CFLAGS="$CFLAGS -Waggregate-return"
		CFLAGS="$CFLAGS -Wmissing-format-attribute"
		CFLAGS="$CFLAGS -Wnested-externs"
		CFLAGS="$CFLAGS -Wshadow"
	],
	[minimum], [
		CFLAGS="$CFLAGS -DG_DISABLE_CAST_CHECKS"
	],
	[no], [
		CFLAGS="$CFLAGS -DG_DISABLE_ASSERT"
		CFLAGS="$CFLAGS -DG_DISABLE_CHECKS"
		CFLAGS="$CFLAGS -DG_DISABLE_CAST_CHECKS"
	],
	[AC_MSG_ERROR([Unknown argument to --enable-debug])]
)

AC_ARG_ENABLE([trace],
	      [AS_HELP_STRING([--enable-trace=@<:@no/yes@:>@],
	      		      [turn on tracing @<:@default=no@:>@])],
	      		      [],
	      		      [enable_trace=no])
AS_CASE([$enable_trace],
	[yes], [
		CFLAGS="$CFLAGS -DMONGO_TRACE"
	],
	[no], [],
	[AC_MSG_ERROR([Unknown argument to --enable-trace])]
)


dnl **************************************************************************
dnl Unit Tests
dnl **************************************************************************
AC_PATH_PROG([GTESTER], [gtester])
AC_PATH_PROG([GTESTER_REPORT], [gtester-report])


dnl **************************************************************************
dnl API Documentation
dnl **************************************************************************
GTK_DOC_CHECK([1.14])


dnl **************************************************************************
dnl Miscellaneous
dnl **************************************************************************
GOBJECT_INTROSPECTION_CHECK([1.30.0])


dnl **************************************************************************
dnl Output
dnl **************************************************************************
AC_CONFIG_FILES([
	Makefile
	mongo-glib-1.0.pc
	mongo-glib.spec
	mongo-glib/mongo-version.h
	doc/Makefile
	doc/version.xml
])
AC_OUTPUT

echo ""
echo "Mongo-GLib $VERSION"
echo ""
echo "  Prefix ....................: ${prefix}"
echo "  Enable Introspection ......: ${found_introspection}"
echo "  API Documentation .........: ${enable_gtk_doc}"
echo "  Debug Level ...............: ${enable_debug}"
echo "  Tracing ...................: ${enable_trace}"
echo "  UTF-8 .....................: ${utf8_support}"
echo ""
