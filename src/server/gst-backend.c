#include "VTserver.h"

static GstElement *playbin;
static GstBus     *bus;
static guint       bus_watch_id;
static GtkWidget  *video_widget;

int md_gst_is_playing(void)
{
    GstState current, pending;
    if (!playbin) return 0;
    
    gst_element_get_state(playbin, &current, &pending, 0);
    return (current == GST_STATE_PLAYING || pending == GST_STATE_PLAYING) ? 1 : 0;
}

static void on_about_to_finish(GstElement *playbin, gpointer data)
{
    VTmpeg *mpeg;
    
    /* This signal is emitted from a streaming thread.
       We need to be careful. unix_getvideo locks a mutex. */
    
    mpeg = unix_getvideo();
    if (mpeg) {
        g_printerr("Gapless transition to: %s\n", mpeg->filename);
        g_object_set(G_OBJECT(playbin), "uri", mpeg->filename, NULL);
    }
}

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data)
{
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS: {
            VTmpeg *mpeg;
            g_printerr("End of stream\n");
            
            /* If about-to-finish didn't catch a new video (or queue was empty then but not now),
               try to play next one manually or stop */
            mpeg = unix_getvideo();
            if (mpeg) {
                g_printerr("Playing next: %s\n", mpeg->filename);
                md_gst_play(mpeg->filename);
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
    //g_object_unref(vbox); // managed by GTK container
    return 0;
}

gint md_gst_init(gint *argc, gchar ***argv, GtkWidget *win)
{
    /* init GStreamer */
    gst_init(argc, argv);

    playbin = gst_element_factory_make("playbin", "play");
    if (!playbin) {
        g_printerr("Failed to create 'playbin' element.\n");
        return -1;
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
