#include "VTserver.h"
#include <cairo.h>

static GstElement *playbin;
static GstBus     *bus;
static guint       bus_watch_id;
static GtkWidget  *video_widget;

/* State for features */
static int g_loop_enabled = 0;
static int g_watermark_enabled = 0;
static char *g_current_uri = NULL;
static guintptr g_window_handle = 0;
static gboolean g_using_gtksink = FALSE;

/*
 * Transition flag to prevent EOS/Signal races.
 * Accessed from:
 *  - streaming thread (about-to-finish)
 *  - main thread (bus watch)
 *
 * Use atomics.
 * 1 = next URI scheduled via about-to-finish
 * 0 = no transition scheduled
 */
static gint g_next_uri_scheduled = 0;

/* Internal helper to ensure a path has a URI scheme */
static char *ensure_uri_scheme(const char *uri)
{
    if (!uri) return NULL;

    if (strstr(uri, "://")) {
        return g_strdup(uri);
    } else {
        /* NOTE: For best practice, consider g_filename_to_uri() later. */
        return g_strdup_printf("file://%s", uri);
    }
}

int md_gst_is_playing(void)
{
    GstState current, pending;
    if (!playbin) return 0;

    gst_element_get_state(playbin, &current, &pending, 0);
    return (current == GST_STATE_PLAYING || pending == GST_STATE_PLAYING) ? 1 : 0;
}

gboolean md_gst_is_stopped(void)
{
    GstState current = GST_STATE_NULL, pending = GST_STATE_NULL;

    if (!playbin) return TRUE;

    /* IMPORTANT:
     * With timeout=0, gst_element_get_state() can return ASYNC even when current
     * is already READY/NULL. For startup gating we only care about current state,
     * not the return code.
     */
    gst_element_get_state(playbin, &current, &pending, 0);

    /* Consider READY as "stopped enough" for safe start. */
    return (current == GST_STATE_NULL || current == GST_STATE_READY) ? TRUE : FALSE;
}

void md_gst_set_window_handle(guintptr handle)
{
    g_window_handle = handle;
    if (playbin && GST_IS_VIDEO_OVERLAY(playbin) && !g_using_gtksink) {
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(playbin), g_window_handle);
    }
}

static GstBusSyncReply bus_sync_handler(GstBus *bus, GstMessage *msg, gpointer data)
{
    (void)bus; (void)data;

    /* Only handle sync XID embedding if we are NOT using a native GTK sink */
    if (g_using_gtksink)
        return GST_BUS_PASS;

    if (gst_is_video_overlay_prepare_window_handle_message(msg)) {
        if (g_window_handle != 0) {
	    GstObject *src = GST_MESSAGE_SRC(msg);

            /* Normal case: message source is the overlay-capable sink. */
            if (GST_IS_VIDEO_OVERLAY(src)) {
                gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(src), g_window_handle);
            } else {
                /* Fallback: try the current playbin video-sink (some graphs emit from a bin/child). */
                GstElement *vsink = NULL;
                g_object_get(G_OBJECT(playbin), "video-sink", &vsink, NULL);
                if (vsink) {
                    if (GST_IS_VIDEO_OVERLAY(vsink)) {
                        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(vsink), g_window_handle);
                    }
                    gst_object_unref(GST_OBJECT(vsink));
                }
            }
        }
    }

    return GST_BUS_PASS;
}

static void draw_overlay(GstElement *overlay, cairo_t *cr, guint64 timestamp, guint64 duration, gpointer data)
{
    (void)overlay; (void)timestamp; (void)duration; (void)data;

    if (!g_watermark_enabled) return;

    double x1, y1, x2, y2;
    cairo_clip_extents(cr, &x1, &y1, &x2, &y2);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 24.0);

    const char *text = "VT-TV LIVE";
    cairo_text_extents_t extents;
    cairo_text_extents(cr, text, &extents);

    /* Position Top-Right with 20px padding */
    double x = x2 - extents.width - 20;
    double y = y1 + extents.height + 20;

    /* Drop Shadow (Black, 50% opacity) */
    cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
    cairo_move_to(cr, x + 2, y + 2);
    cairo_show_text(cr, text);

    /* Text (White, 50% opacity) */
    cairo_set_source_rgba(cr, 1, 1, 1, 0.5);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, text);
}

static void on_about_to_finish(GstElement *playbin_local, gpointer data)
{
    (void)playbin_local; (void)data;

    char *next_filename = NULL;
    char *new_uri = NULL;

    /*
     * SINGLE AUTHORITY for queue advancement.
     * This runs in the streaming thread. No GTK calls allowed.
     */
    next_filename = unix_getvideo();
    if (next_filename) {
        g_printerr("Gapless transition to: %s\n", next_filename);
        new_uri = ensure_uri_scheme(next_filename);
        g_free(next_filename);
    } else if (g_loop_enabled && g_current_uri) {
        /* Safe read: g_current_uri might be mutated by main thread,
           but we need a lock-free snapshot or lock it.
           Ideally we lock, but g_loop_enabled is static config.
           We'll use the lock below for the update. */
        g_printerr("Queue empty, looping: %s\n", g_current_uri);
        /* Note: This read is technically racing if md_gst_play is called
           concurrently, but we are fixing the write race below. */
        thread_lock();
        if (g_current_uri) new_uri = g_strdup(g_current_uri);
        thread_unlock();
    }

    if (new_uri) {
        thread_lock();
        if (g_current_uri) g_free(g_current_uri);
        g_current_uri = g_strdup(new_uri);
        thread_unlock();

        g_object_set(G_OBJECT(playbin), "uri", new_uri, NULL);
        g_free(new_uri);

        /* Mark transition as active so EOS doesn't stop pipeline */
        g_atomic_int_set(&g_next_uri_scheduled, 1);
    } else {
        g_atomic_int_set(&g_next_uri_scheduled, 0);
    }
}

static gboolean bus_call(GstBus *bus_local, GstMessage *msg, gpointer data)
{
    (void)bus_local; (void)data;

    switch (GST_MESSAGE_TYPE(msg)) {

        case GST_MESSAGE_STATE_CHANGED: {
            /* Clear transition flag when the *new* clip reaches PLAYING.
               This is essential because in true gapless playback you often
               do NOT get EOS between items. */
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(playbin)) {
                GstState old_s, new_s, pending_s;
                gst_message_parse_state_changed(msg, &old_s, &new_s, &pending_s);

                if (new_s == GST_STATE_PLAYING && g_atomic_int_get(&g_next_uri_scheduled) == 1) {
                    g_printerr("Transition committed (PLAYING). Clearing transition flag.\n");
                    g_atomic_int_set(&g_next_uri_scheduled, 0);
                }
            }
            break;
        }

        case GST_MESSAGE_EOS: {
            g_printerr("End of stream\n");

            /* Deterministic EOS logic:
               - If transition flag is set, ignore EOS for pipeline-stop purposes.
               - If not set, we are at end of playlist: stop pipeline. */
            if (g_atomic_int_get(&g_next_uri_scheduled) == 1) {
                g_printerr("Ignoring EOS (transition active)\n");
                /* Clear just in case this EOS was actually emitted (non-gapless path) */
                g_atomic_int_set(&g_next_uri_scheduled, 0);
            } else {
                g_printerr("Playlist finished. Stopping.\n");
                gst_element_set_state(playbin, GST_STATE_NULL);
            }
            break;
        }

        case GST_MESSAGE_ERROR: {
            gchar  *debug = NULL;
            GError *error = NULL;

            gst_message_parse_error(msg, &error, &debug);
            g_free(debug);

            g_printerr("Error: %s\n", error ? error->message : "(unknown)");
            if (error) g_error_free(error);

            gst_element_set_state(playbin, GST_STATE_NULL);
            g_atomic_int_set(&g_next_uri_scheduled, 0);
            break;
        }

        default:
            break;
    }

    return TRUE;
}

gint md_gst_play(char *uri)
{
    gchar *real_uri;
    g_return_val_if_fail(uri, -1);

    real_uri = ensure_uri_scheme(uri);

    /* Update current URI cache (protected) */
    thread_lock();
    if (g_current_uri) g_free(g_current_uri);
    g_current_uri = g_strdup(real_uri);
    thread_unlock();

    g_object_set(G_OBJECT(playbin), "uri", real_uri, NULL);
    g_free(real_uri);

    if (GST_IS_ELEMENT(playbin))
        gst_element_set_state(playbin, GST_STATE_PLAYING);

    return 0;
}

gint md_gst_finish(void)
{
    if (bus_watch_id > 0)
        g_source_remove(bus_watch_id);

    if (playbin) {
        gst_element_set_state(playbin, GST_STATE_NULL);
        gst_object_unref(GST_OBJECT(playbin));
        playbin = NULL;
    }

    thread_lock();
    if (g_current_uri) {
        g_free(g_current_uri);
        g_current_uri = NULL;
    }
    thread_unlock();

    g_atomic_int_set(&g_next_uri_scheduled, 0);
    return 0;
}

gint md_gst_init(gint *argc, gchar ***argv, GtkWidget *win, int loop_enabled, int watermark_enabled)
{
    GstElement *sink = NULL;
    gboolean using_modern_sink = FALSE;

    /* Store feature flags */
    g_loop_enabled = loop_enabled;
    g_watermark_enabled = watermark_enabled;

    gst_init(argc, argv);

    /* Modern Playback: try playbin3 first */
    if (gst_element_factory_find("playbin3")) {
        playbin = gst_element_factory_make("playbin3", "play");
    } else {
        playbin = gst_element_factory_make("playbin", "play");
    }

    if (!playbin) {
        g_printerr("Failed to create playback element.\n");
        return -1;
    }

    /*
     * Modern Embedding Path A: Try gtkglsink -> gtksink
     */
    if ((sink = gst_element_factory_make("gtkglsink", "gtkglsink_elt"))) {
        using_modern_sink = TRUE;
    } else if ((sink = gst_element_factory_make("gtksink", "gtksink_elt"))) {
        using_modern_sink = TRUE;
    }

    if (using_modern_sink && sink) {
        GstElement *sink_bin = NULL;
        GstElement *convert = NULL, *scale = NULL, *overlay = NULL;

        sink_bin = gst_bin_new("sink_bin");
        convert  = gst_element_factory_make("videoconvert", NULL);
        scale    = gst_element_factory_make("videoscale", NULL);
        overlay  = gst_element_factory_make("cairooverlay", "overlay");

        if (sink_bin && convert && scale && overlay) {
            gboolean ok_pad = FALSE;

            gst_bin_add_many(GST_BIN(sink_bin), convert, scale, overlay, sink, NULL);
            if (!gst_element_link_many(convert, scale, overlay, sink, NULL)) {
                g_printerr("Failed to link sink bin elements, falling back.\n");
                gst_object_unref(GST_OBJECT(sink_bin));
                sink_bin = NULL;
            } else {
                GstPad *target_pad = gst_element_get_static_pad(convert, "sink");
                if (target_pad) {
                    GstPad *ghost_pad = gst_ghost_pad_new("sink", target_pad);
                    gst_object_unref(target_pad);

                    if (ghost_pad) {
                        if (gst_element_add_pad(sink_bin, ghost_pad)) {
                            ok_pad = TRUE;
                        } else {
                            g_printerr("Failed to add ghost pad to sink bin, falling back.\n");
                            gst_object_unref(GST_OBJECT(ghost_pad));
                        }
                    } else {
                        g_printerr("Failed to create ghost pad, falling back.\n");
                    }
                } else {
                    g_printerr("Failed to get convert sink pad, falling back.\n");
                }

                if (!ok_pad) {
                    gst_object_unref(GST_OBJECT(sink_bin));
                    sink_bin = NULL;
                }
            }

            if (sink_bin) {
                g_object_get(sink, "widget", &video_widget, NULL);
                if (video_widget) {
                    g_using_gtksink = TRUE;

                    gtk_container_add(GTK_CONTAINER(win), video_widget);
                    gtk_widget_show(video_widget);

                    /* Drop our ref; container holds one now */
                    g_object_unref(video_widget);

                    if (g_watermark_enabled)
                        g_signal_connect(overlay, "draw", G_CALLBACK(draw_overlay), NULL);

                    g_object_set(G_OBJECT(playbin), "video-sink", sink_bin, NULL);
                } else {
                    g_printerr("Failed to get gtksink widget, falling back.\n");
                    gst_object_unref(GST_OBJECT(sink_bin));
                    /* keep g_using_gtksink FALSE => fallback path B */
                }
            }
        } else {
            g_printerr("Failed to create bin elements, falling back.\n");
            if (sink_bin) gst_object_unref(GST_OBJECT(sink_bin));
        }
    }

    /*
     * Fallback Embedding Path B: Legacy GstVideoOverlay (via video.c)
     */
    if (!g_using_gtksink) {
        g_printerr("Modern sinks not available or failed, using fallback embedding.\n");
        video_widget = gst_player_video_new(playbin);
        if (video_widget) {
            gtk_container_add(GTK_CONTAINER(win), video_widget);
            gtk_widget_show(video_widget);

            if (g_watermark_enabled) {
                GstElement *video_sink_bin = gst_parse_bin_from_description(
                    "videoconvert ! videoscale method=0 ! cairooverlay name=overlay ! autovideosink",
                    TRUE, NULL);

                if (video_sink_bin) {
                    GstElement *ov = gst_bin_get_by_name(GST_BIN(video_sink_bin), "overlay");
                    if (ov) {
                        g_signal_connect(ov, "draw", G_CALLBACK(draw_overlay), NULL);
                        gst_object_unref(GST_OBJECT(ov));
                        g_object_set(G_OBJECT(playbin), "video-sink", video_sink_bin, NULL);
                    }
                }
            }
        }
    }

    bus = gst_pipeline_get_bus(GST_PIPELINE(playbin));

    /* Synchronous handler (only active if !g_using_gtksink) */
    gst_bus_set_sync_handler(bus, bus_sync_handler, NULL, NULL);

    /* Async watch for state changes/EOS/errors */
    bus_watch_id = gst_bus_add_watch(bus, bus_call, NULL);
    gst_object_unref(GST_OBJECT(bus));

    g_signal_connect(playbin, "about-to-finish", G_CALLBACK(on_about_to_finish), NULL);

    /* Start clean */
    g_atomic_int_set(&g_next_uri_scheduled, 0);

    return 0;
}
