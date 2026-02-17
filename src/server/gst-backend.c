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

int md_gst_is_playing(void)
{
    GstState current, pending;
    if (!playbin) return 0;
    
    gst_element_get_state(playbin, &current, &pending, 0);
    return (current == GST_STATE_PLAYING || pending == GST_STATE_PLAYING) ? 1 : 0;
}

static void draw_overlay(GstElement *overlay, cairo_t *cr, guint64 timestamp, guint64 duration, gpointer data)
{
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

static void on_about_to_finish(GstElement *playbin, gpointer data)
{
    VTmpeg *mpeg;
    
    /* This signal is emitted from a streaming thread. */
    mpeg = unix_getvideo();
    if (mpeg) {
        g_printerr("Gapless transition to: %s\n", mpeg->filename);
        /* Store new URI as current */
        if (g_current_uri) g_free(g_current_uri);
        g_current_uri = g_strdup(mpeg->filename);
        g_object_set(G_OBJECT(playbin), "uri", g_current_uri, NULL);
    } else if (g_loop_enabled && g_current_uri) {
        /* Loop the current video if queue is empty */
        g_printerr("Queue empty, looping: %s\n", g_current_uri);
        g_object_set(G_OBJECT(playbin), "uri", g_current_uri, NULL);
    }
}

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data)
{
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS: {
            VTmpeg *mpeg;
            g_printerr("End of stream\n");
            
            /* If about-to-finish didn't catch it, handle transition or stop */
            mpeg = unix_getvideo();
            if (mpeg) {
                g_printerr("Playing next: %s\n", mpeg->filename);
                md_gst_play(mpeg->filename);
            } else if (g_loop_enabled && g_current_uri) {
                g_printerr("Queue empty, looping (EOS): %s\n", g_current_uri);
                md_gst_play(g_current_uri);
            } else {
                gst_element_set_state(playbin, GST_STATE_NULL);
            }
            break;
        }
        case GST_MESSAGE_ERROR: {
            gchar  *debug;
            GError *error;

            gst_message_parse_error(msg, &error, &debug);
            g_free(debug);

            g_printerr("Error: %s\n", error->message);
            g_error_free(error);

            gst_element_set_state(playbin, GST_STATE_NULL);
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

    /* Update current URI cache */
    if (g_current_uri) g_free(g_current_uri);
    g_current_uri = g_strdup(uri);

    /* Basic check to see if it's already a URI protocol */
    if (strstr(uri, "://")) {
        real_uri = g_strdup(uri);
    } else {
        real_uri = g_strdup_printf("file://%s", uri);
    }

    g_object_set(G_OBJECT(playbin), "uri", real_uri, NULL);
    g_free(real_uri);

    if(GST_IS_ELEMENT(playbin))
        gst_element_set_state(playbin, GST_STATE_PLAYING);
    return 0;
}

gint md_gst_finish(void)
{
    if (bus_watch_id > 0)
        g_source_remove(bus_watch_id);
    
    gst_element_set_state(playbin, GST_STATE_NULL);
    gst_object_unref(GST_OBJECT(playbin));
    
    if (g_current_uri) {
        g_free(g_current_uri);
        g_current_uri = NULL;
    }
    return 0;
}

gint md_gst_init(gint *argc, gchar ***argv, GtkWidget *win, int loop_enabled, int watermark_enabled)
{
    /* Store feature flags */
    g_loop_enabled = loop_enabled;
    g_watermark_enabled = watermark_enabled;

    /* init GStreamer */
    gst_init(argc, argv);

    playbin = gst_element_factory_make("playbin", "play");
    if (!playbin) {
        g_printerr("Failed to create 'playbin' element.\n");
        return -1;
    }

    /* Configure Overlay if enabled */
    if (g_watermark_enabled) {
        GstElement *video_sink_bin;
        
        /* Create a bin with cairooverlay and autovideosink */
        video_sink_bin = gst_parse_bin_from_description("cairooverlay name=overlay ! autovideosink", TRUE, NULL);
        if (video_sink_bin) {
            GstElement *overlay = gst_bin_get_by_name(GST_BIN(video_sink_bin), "overlay");
            if (overlay) {
                g_signal_connect(overlay, "draw", G_CALLBACK(draw_overlay), NULL);
                gst_object_unref(overlay);
                
                /* Set the custom bin as the video sink for playbin */
                g_object_set(G_OBJECT(playbin), "video-sink", video_sink_bin, NULL);
            } else {
                g_printerr("Warning: Could not find 'overlay' in sink bin. Watermark disabled.\n");
            }
        } else {
            g_printerr("Warning: Failed to create overlay sink bin (missing plugins?). Watermark disabled.\n");
        }
    }

    /* Set up bus watch */
    bus = gst_pipeline_get_bus(GST_PIPELINE(playbin));
    bus_watch_id = gst_bus_add_watch(bus, bus_call, NULL);
    gst_object_unref(bus);

    /* Signal for gapless playback */
    g_signal_connect(playbin, "about-to-finish", G_CALLBACK(on_about_to_finish), NULL);

    /* Create video widget */
    video_widget = gst_player_video_new(playbin);
    if (!video_widget) {
        g_printerr("Failed to create video widget.\n");
        return -1;
    }
    
    gtk_container_add(GTK_CONTAINER(win), video_widget);
    gtk_widget_show(video_widget);

    return 0;
}
