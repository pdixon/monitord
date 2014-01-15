/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) Phillip Dixon, 2014
 */
#pragma once

#include <glib.h>
#include <xcb/xcb.h>

G_BEGIN_DECLS

typedef struct _PDXcbSource PDXcbSource;

typedef gboolean (*PDXcbEventCallback)(xcb_generic_event_t *event,
                                       gpointer user_data);


PDXcbSource *pd_xcb_source_new(GMainContext *context,
                               xcb_connection_t *connection,
                               PDXcbEventCallback callback,
                               gpointer user_data,
                               GDestroyNotify destroy_func);

void pd_xcb_source_ref(PDXcbSource *self);
void pd_xcb_source_unref(PDXcbSource *self);

G_END_DECLS
