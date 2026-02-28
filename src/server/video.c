/*
 * Modern GStreamer Video Widget (GTK 3 + GstVideoOverlay)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "video.h"
#include "VTserver.h"
#include <gdk/gdkx.h>
#include <gst/video/videooverlay.h>
#include <cairo.h>

/* Callback to pass the XID/Window Handle to GStreamer once the widget is realized */
static void realize_cb (GtkWidget *widget, gpointer data)
{
    GdkWindow *window = gtk_widget_get_window(widget);
    guintptr window_handle;

    if (!gdk_window_ensure_native(window))
        g_error("Couldn't create native window needed for GstVideoOverlay!");

    /* NOTE: This logic is X11-dependent. Wayland support requires GstVideoOverlay 
       to use wayland-specific surface handles. */
    window_handle = GDK_WINDOW_XID(window);
    
    /* Pass the window handle to the backend. It will be stored and used
       in the synchronous bus handler whenever a new sink is created. */
    md_gst_set_window_handle(window_handle);
}

/* Standby screen drawing callback for when playback is idle */
static gboolean draw_cb (GtkWidget *widget, cairo_t *cr, gpointer data)
{
    /* If GStreamer is playing, let it handle the surface.
       Return TRUE to prevent GTK from clearing the background. */
    if (md_gst_is_playing())
        return TRUE;

    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);

    /* 1. Paint Background (Dark Grey) */
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    cairo_paint(cr);

    /* 2. Setup Typography */
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    
    cairo_text_extents_t ext;
    const char *logo = "VT-TV";
    const char *status = "STATION STANDBY - OFF AIR";

    /* 3. Draw Logo (Center) */
    cairo_set_font_size(cr, 64.0);
    cairo_text_extents(cr, logo, &ext);
    cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
    cairo_move_to(cr, (alloc.width / 2) - (ext.width / 2) - ext.x_bearing, 
                      (alloc.height / 2) - (ext.height / 2) - ext.y_bearing);
    cairo_show_text(cr, logo);

    /* 4. Draw Status (Bottom) */
    cairo_set_font_size(cr, 18.0);
    cairo_text_extents(cr, status, &ext);
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
    cairo_move_to(cr, (alloc.width / 2) - (ext.width / 2) - ext.x_bearing, 
                      alloc.height - 40);
    cairo_show_text(cr, status);

    /* Return TRUE to signal we've handled the drawing, preventing default clear */
    return TRUE;
}

GtkWidget *gst_player_video_new (GstElement *playbin)
{
    GtkWidget *area = gtk_drawing_area_new();
    
    /* Critical: Disable default background painting to allow GStreamer overlay */
    gtk_widget_set_app_paintable(area, TRUE);

    /* Connect to realize signal to capture XID */
    g_signal_connect(area, "realize", G_CALLBACK(realize_cb), playbin);
    
    /* Connect to draw signal to handle idle state (Off-Air screen) */
    g_signal_connect(area, "draw", G_CALLBACK(draw_cb), NULL);
    
    /* Set a reasonable default size */
    gtk_widget_set_size_request(area, 640, 480);
    
    /* Allow the widget to expand */
    gtk_widget_set_hexpand(area, TRUE);
    gtk_widget_set_vexpand(area, TRUE);

    return area;
}
