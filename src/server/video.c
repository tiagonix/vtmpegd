/*
 * Modern GStreamer Video Widget (GTK 3 + GstVideoOverlay)
 * Replaces legacy custom widget.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "video.h"
#include <gdk/gdkx.h>
#include <gst/video/videooverlay.h>

/* Callback to pass the XID/Window Handle to GStreamer once the widget is realized */
static void realize_cb (GtkWidget *widget, gpointer data)
{
    GdkWindow *window = gtk_widget_get_window(widget);
    GstElement *playbin = GST_ELEMENT(data);
    guintptr window_handle;

    if (!gdk_window_ensure_native(window))
        g_error("Couldn't create native window needed for GstVideoOverlay!");

    window_handle = GDK_WINDOW_XID(window);
    
    /* Pass the window handle to the GStreamer element implementing GstVideoOverlay */
    gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(playbin), window_handle);
}

GtkWidget *gst_player_video_new (GstElement *playbin)
{
    GtkWidget *area = gtk_drawing_area_new();
    
    /* Connect to realize signal to pass XID to GStreamer */
    g_signal_connect(area, "realize", G_CALLBACK(realize_cb), playbin);
    
    /* Double buffering is handled by the compositor in GTK3+ */
    
    /* Set a reasonable default size */
    gtk_widget_set_size_request(area, 640, 480);
    
    /* Allow the widget to expand */
    gtk_widget_set_hexpand(area, TRUE);
    gtk_widget_set_vexpand(area, TRUE);

    return area;
}
