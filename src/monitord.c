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

#include <stdlib.h>
#include <glib.h>
#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <xcb/randr.h>

#include "pd_xcb_source.h"
#include "log.h"

#define cleanup_free __attribute__((cleanup(pd_free)))

static inline void pd_free(void *p)
{
    free(*(void**) p);
}

typedef struct {
    gboolean on_ac;
    gboolean lid_closed;
    gboolean external_display;
} SystemState;

typedef struct {
    xcb_connection_t *c;
    int event_base;
} RandrContext;

void handle_output_change(xcb_connection_t *c, xcb_randr_notify_event_t *e)
{
    xcb_randr_get_output_info_cookie_t info;
    xcb_randr_get_output_info_reply_t *reply;
    xcb_randr_output_change_t *change = &e->u.oc;
    info = xcb_randr_get_output_info(c, change->output, 0);
    reply = xcb_randr_get_output_info_reply(c, info, 0);
    cleanup_free const char *name = ({
            uint8_t *name = xcb_randr_get_output_info_name(reply);
            int len = xcb_randr_get_output_info_name_length(reply);
            strndup((char *)name, len);
        });
    log_info("output %s connection: %d, crtc: %d",
             name, change->connection, change->crtc);
    if(!strcmp(name, "DVI1")) {
        if(!change->connection && !change->crtc) {
            log_info("enabling DVI");
            system("xrandr --output DVI1 --auto --above LVDS1");
        } else if (change->connection && change->crtc) {
            log_info("disabling DVI");
            system("xrandr --output DVI1 --off");
        }
    }
}

static gboolean handle_xcb_event(xcb_generic_event_t *event,
                                 gpointer user_data)
{
    RandrContext *context;
    check(event, "No event");
    check(user_data, "No user_data");

    context = (RandrContext *)user_data;
    switch((event->response_type & ~0x80) - context->event_base) {
        case XCB_RANDR_NOTIFY_OUTPUT_CHANGE:
            handle_output_change(context->c, (xcb_randr_notify_event_t *)event);
            break;
        default:
            log_info("Unknown xcb event.");
    }
    return TRUE;

error:
    return FALSE;
}

int main(int argc, char *argv[])
{
    GMainLoop *loop;
    PDXcbSource *xcb;

    cleanup_free xcb_connection_t *c;
    cleanup_free xcb_randr_query_version_reply_t *rr_version;
    cleanup_free const xcb_query_extension_reply_t *rr_extn;
    cleanup_free xcb_screen_t *root;

    int screen;
    int r;

    RandrContext context;

    c = xcb_connect(NULL, &screen);
    r = xcb_connection_has_error(c);
    check(r == 0, "xcb_connect failed with error %d", r);

    rr_extn = xcb_get_extension_data(c, &xcb_randr_id);
    check(rr_extn, "Failed to retrive Randr extension data");
    context.event_base = rr_extn->first_event;
    context.c = c;

    root = xcb_aux_get_screen(c, screen);
    check(root, "Failed to retrive X11 root");

    loop = g_main_loop_new(NULL, FALSE);
    check(loop, "Failed to create g_main_loop");

    xcb = pd_xcb_source_new(NULL,
                           c,
                           &handle_xcb_event,
                           &context,
                           NULL);
    check(xcb, "Failed to init xcb g_source");

    xcb_randr_select_input(c,root->root,XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE);
    xcb_flush(c);

    g_main_loop_run(loop);

    pd_xcb_source_unref(xcb);
    g_main_loop_unref(loop);
    xcb_disconnect(c);
    return EXIT_SUCCESS;

error:
    return EXIT_FAILURE;
}
