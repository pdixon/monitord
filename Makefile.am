bin_PROGRAMS = monitord

monitord_SOURCES = src/monitord.c src/pd_xcb_source.c
monitord_CFLAGS = $(GLIB_CFLAGS) $(UPOWER_CFLAGS) $(XCB_FLAGS)
monitord_LDADD = $(GLIB_LIBS) $(UPOWER_LIBS) $(XCB_LIBS)
