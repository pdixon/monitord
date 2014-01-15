AC_INIT([monitord], [0.1], [phil@dixon.gen.nz])

AM_INIT_AUTOMAKE([foreign subdir-objects no-dist-gzip dist-xz])
AM_SILENT_RULES([yes])

AC_PROG_CC

PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 2.36, gio-2.0 >= 2.36])
PKG_CHECK_MODULES([XCB], [xcb, xcb-randr, xcb-util])

AC_OUTPUT([Makefile])
