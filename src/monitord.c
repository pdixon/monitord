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
#include <assert.h>
#include <stdlib.h>
#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <xcb/randr.h>
#include <upower.h>

#include "pd_xcb_source.h"
#include "log.h"

#define cleanup_free __attribute__((cleanup(pd_free)))

static inline void pd_free(void *p)
{
    free(*(void**) p);
}

typedef struct {
    int lid_inhibit_fd;
    uint8_t on_battery:1;
    uint8_t lid_present:1;
    uint8_t lid_closed:1;
    uint8_t ext_display_present:1;
    uint8_t ext_display_active:1;
    uint8_t int_display_present:1;
    uint8_t int_display_active:1;
} SystemState;

typedef struct {
    xcb_connection_t *c;
    int event_base;
    GDBusProxy *proxy;
    SystemState state;
} Context;

static void take_inhibit_cb(GObject *source_object,
                            GAsyncResult *res,
                            gpointer user_data)
{
    GVariant *result = NULL;
    GError *error = NULL;
    GUnixFDList *fd_list;
    gint32 fd_index = 0;
    Context *context = (Context *)user_data;

    result = g_dbus_proxy_call_with_unix_fd_list_finish(context->proxy,
                                                        &fd_list,
                                                        res,
                                                        &error);
    if (!result) {
        log_warn("Error taking lid inhibitor lock: %s", error->message);
        g_error_free(error);
        return;
    }

    g_variant_get(result, "(h)", &fd_index);
    context->state.lid_inhibit_fd = g_unix_fd_list_get(fd_list, fd_index, &error);
    if (context->state.lid_inhibit_fd == -1) {
        log_warn("Error getting file descriptor for lid inhibitor lock: %s",
                 error->message);
        g_error_free(error);
    }
    g_variant_unref(result);
    g_object_unref(fd_list);
}

static void take_inhibit(Context *context)
{
    if(context->state.lid_inhibit_fd >= 0)
    {
        return;
    }
    else
    {
        g_dbus_proxy_call_with_unix_fd_list(context->proxy,
                                            "Inhibit",
                                            g_variant_new("(ssss)",
                                                          "handle-lid-switch",
                                                          "monitord",
                                                          "support clamshell.",
                                                          "block"),
                                            G_DBUS_CALL_FLAGS_NONE,
                                            -1,
                                            NULL,
                                            NULL,
                                            &take_inhibit_cb,
                                            context);
    }
}

static void release_inhibit(Context *context)
{
    if(context->state.lid_inhibit_fd >= 0)
    {
        close(context->state.lid_inhibit_fd);
        context->state.lid_inhibit_fd = -1;
        return;
    }
}

void print_state(SystemState state)
{
    log_info("System state: bat: %d lid_i: %d, lid_p: %d lid_c: %d, ext_p: %d, ext_a: %d, int_p: %d, int_a: %d",
             state.on_battery,
             state.lid_inhibit_fd,
             state.lid_present,
             state.lid_closed,
             state.ext_display_present,
             state.ext_display_active,
             state.int_display_present,
             state.int_display_active);
}

void apply(Context *c)
{
    print_state(c->state);
    if(!c->state.lid_closed &&
       ((!c->state.ext_display_active && c->state.ext_display_present) ||
        (!c->state.int_display_active && c->state.int_display_present)))
    {
        log_info("Go to Dualhead");
        system("xrandr --output DVI1 --auto --above LVDS1 --output LVDS1 --auto");
    }
    else if (c->state.ext_display_active &&
             !c->state.ext_display_present)
    {
        log_info("Go to Internal Only");
        system("xrandr --output DVI1 --off --output LVDS1 --auto");
    }
    else if (c->state.ext_display_active &&
             c->state.ext_display_present &&
             c->state.lid_closed &&
             c->state.lid_inhibit_fd >= 0)
    {
        log_info("Go to External Only");
        system("xrandr --output LVDS1 --off");
    }

    if(!c->state.on_battery &
       c->state.lid_present &
       c->state.ext_display_present &
       c->state.ext_display_active)
    {
        log_info("Mask lid");
        take_inhibit(c);
    } else {
        log_info("Unmask lid");
        release_inhibit(c);
    }
}

void handle_output(Context *c, xcb_randr_get_output_info_reply_t *output)
{
    cleanup_free const char *name = ({
            uint8_t *name = xcb_randr_get_output_info_name(output);
            int len = xcb_randr_get_output_info_name_length(output);
            strndup((char *)name, len);
        });
    if(!strcmp(name, "DVI1")) {
        c->state.ext_display_present = output->connection == 0 ? TRUE : FALSE;
        c->state.ext_display_active = output->crtc ? TRUE : FALSE;
    } else if (!strcmp(name, "LVDS1")) {
        c->state.int_display_present = output->connection == 0 ? TRUE : FALSE;
        c->state.int_display_active = output->crtc ? TRUE : FALSE;
    }
}

static gboolean handle_xcb_event(xcb_generic_event_t *event,
                                 gpointer user_data)
{
    Context *context;
    assert(event);
    assert(user_data);

    context = (Context *)user_data;
    switch((event->response_type & ~0x80) - context->event_base) {
        case XCB_RANDR_NOTIFY_OUTPUT_CHANGE:
            log_info("xcb output changed");
            xcb_randr_get_output_info_cookie_t info;
            xcb_randr_get_output_info_reply_t *reply;
            xcb_randr_output_change_t *change = &((xcb_randr_notify_event_t *)event)->u.oc;
            info = xcb_randr_get_output_info(context->c, change->output, 0);
            reply = xcb_randr_get_output_info_reply(context->c, info, 0);
            handle_output(context, reply);
            free(reply);
            apply(context);
            break;
        default:
            log_info("Unknown xcb event.");
    }
    return TRUE;

error:
    return FALSE;
}

static void on_upower_changed(UpClient *client,
                              gpointer user_data)
{
    Context *context;

    assert(client);
    assert(user_data);

    context = (Context *)user_data;

    context->state.on_battery = up_client_get_on_battery(client);
    context->state.lid_present = up_client_get_lid_is_present(client);
    context->state.lid_closed = up_client_get_lid_is_closed(client);
    apply(context);
}

void randr_scan_outputs(Context *context, xcb_window_t root)
{
    xcb_connection_t *conn = context->c;
    xcb_randr_get_screen_resources_current_cookie_t r_cookie;
    xcb_randr_get_output_primary_cookie_t p_cookie;

    xcb_randr_get_output_primary_reply_t *primary;
    xcb_randr_get_screen_resources_current_reply_t *res;

    xcb_timestamp_t config_ts;

    xcb_randr_output_t *randr_outputs;

    r_cookie = xcb_randr_get_screen_resources_current(conn, root);
    p_cookie = xcb_randr_get_output_primary(conn, root);

    primary = xcb_randr_get_output_primary_reply(conn, p_cookie, NULL);
    if(!primary) {
        log_err("Couldn't get RandR primary output");
        return;
    }

    res = xcb_randr_get_screen_resources_current_reply(conn, r_cookie, NULL);
    if(!res){
        log_err("Couldn't get RandR screen resources");
    }
    config_ts = res->config_timestamp;

    int len;
    len = xcb_randr_get_screen_resources_current_outputs_length(res);
    randr_outputs = xcb_randr_get_screen_resources_current_outputs(res);

    xcb_randr_get_output_info_cookie_t out_cookies[len];
    for(int i = 0; i < len; i++) {
        out_cookies[i] = xcb_randr_get_output_info(conn,
                                                   randr_outputs[i],
                                                   config_ts);
    }

    for(int i = 0; i < len; i++) {
        xcb_randr_get_output_info_reply_t *output;

        output = xcb_randr_get_output_info_reply(conn, out_cookies[i], NULL);
        if(!output) {
            continue;
        }
        handle_output(context, output);
        free(output);
    }
    free(res);
    free(primary);
}

int main(int argc, char *argv[])
{
    GMainLoop *loop;

    GError *error;

    PDXcbSource *xcb;

    cleanup_free xcb_connection_t *c;
    cleanup_free xcb_randr_query_version_reply_t *rr_version;
    cleanup_free const xcb_query_extension_reply_t *rr_extn;
    cleanup_free xcb_screen_t *root;

    int screen;
    int r;

    Context context;

    UpClient *client;

    loop = g_main_loop_new(NULL, FALSE);
    check(loop, "Failed to create g_main_loop");

    // Setup logind DBus proxy;
    error = NULL;
    context.proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                                                  G_DBUS_PROXY_FLAGS_NONE,
                                                  NULL,
                                                  "org.freedesktop.login1",
                                                  "/org/freedesktop/login1",
                                                  "org.freedesktop.login1.Manager",
                                                  NULL,
                                                  &error);
    if(!context.proxy) {
        log_err("Error creating DBus proxy: %s\n", error->message);
        g_error_free(error);
        goto error;
    }
    context.state.lid_inhibit_fd = -1;

    // Setup xcb event monitoring
    c = xcb_connect(NULL, &screen);
    r = xcb_connection_has_error(c);
    check(r == 0, "xcb_connect failed with error %d", r);

    rr_extn = xcb_get_extension_data(c, &xcb_randr_id);
    check(rr_extn, "Failed to retrive Randr extension data");
    context.event_base = rr_extn->first_event;
    context.c = c;

    root = xcb_aux_get_screen(c, screen);
    check(root, "Failed to retrive X11 root");

    xcb = pd_xcb_source_new(NULL,
                            c,
                            &handle_xcb_event,
                            &context,
                            NULL);
    check(xcb, "Failed to init xcb g_source");

    xcb_randr_select_input(c,root->root,XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE);
    xcb_flush(c);

    // Setup UPower signals
    client = up_client_new();
    check(client, "Failed to init UPower client");
    g_signal_connect(client, "changed", G_CALLBACK(on_upower_changed), &context);

    // Initialise to the current state
    context.state.on_battery = up_client_get_on_battery(client);
    context.state.lid_present = up_client_get_lid_is_present(client);
    context.state.lid_closed = up_client_get_lid_is_closed(client);

    // Probe display status at startup rather than assuming.
    randr_scan_outputs(&context, root->root);

    apply(&context);

    g_main_loop_run(loop);

    pd_xcb_source_unref(xcb);
    g_main_loop_unref(loop);
    xcb_disconnect(c);
    return EXIT_SUCCESS;

error:
    return EXIT_FAILURE;
}
