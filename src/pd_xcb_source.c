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
#include "pd_xcb_source.h"

#include <stdlib.h>
#include <glib.h>
#include <xcb/xcb.h>

#include "log.h"

struct _PDXcbSource {
    GSource source;
    xcb_connection_t *connection;
    GQueue *queue;
    GPollFD fd;
};

static void _pd_xcb_source_event_free(gpointer data, gpointer user_data)
{
    free(data);
}

static gboolean _pd_xcb_source_prepare(GSource *source, gint *timeout)
{
    PDXcbSource *self = (PDXcbSource *)source;
    xcb_flush(self->connection);
    *timeout = -1;
    return ! g_queue_is_empty(self->queue);
}

static gboolean _pd_xcb_source_check(GSource *source)
{
    PDXcbSource *self = (PDXcbSource *)source;

    if ( self->fd.revents & G_IO_IN )
    {
        xcb_generic_event_t *event;
        int status;

        status = xcb_connection_has_error(self->connection);
        check(status == 0, "xcb connection error: %d", status);

        while ( ( event = xcb_poll_for_event(self->connection) ) != NULL )
            g_queue_push_tail(self->queue, event);
    }

    return ! g_queue_is_empty(self->queue);

error:
    return TRUE;
}

static gboolean _pd_xcb_source_dispatch(GSource *source,
                                        GSourceFunc callback,
                                        gpointer user_data)
{
    PDXcbSource *self = (PDXcbSource *)source;
    xcb_generic_event_t *event;

    gboolean ret;

    event = g_queue_pop_head(self->queue);
    ret = ((PDXcbEventCallback)callback)(event, user_data);
    _pd_xcb_source_event_free(event, NULL);

    return ret;
}

static void _pd_xcb_source_finalise(GSource *source)
{
    PDXcbSource *self = (PDXcbSource *)source;

    g_queue_foreach(self->queue, _pd_xcb_source_event_free, NULL);
    g_queue_free(self->queue);
}

static GSourceFuncs _pd_xcb_source_funcs = {
    _pd_xcb_source_prepare,
    _pd_xcb_source_check,
    _pd_xcb_source_dispatch,
    _pd_xcb_source_finalise
};

PDXcbSource *pd_xcb_source_new(GMainContext *context,
                               xcb_connection_t *connection,
                               PDXcbEventCallback callback,
                               gpointer user_data,
                               GDestroyNotify destroy_func)
{
    if(!callback)
        return NULL;

    PDXcbSource *source;

    source = (PDXcbSource *)g_source_new(&_pd_xcb_source_funcs, sizeof(PDXcbSource));

    source->connection = connection;
    source->queue = g_queue_new();

    source->fd.fd = xcb_get_file_descriptor(connection);
    source->fd.events = G_IO_IN;

    g_source_add_poll((GSource *)source, &source->fd);
    g_source_attach((GSource *)source, context);
    g_source_set_callback((GSource *)source,
                          (GSourceFunc)callback,
                          user_data,
                          destroy_func);

    return source;
}

void pd_xcb_source_ref(PDXcbSource *self)
{
    if(!self)
        return;

    g_source_ref((GSource *)self);
}

void pd_xcb_source_unref(PDXcbSource *self)
{
    if(!self)
        return;

    g_source_unref((GSource *)self);
}
