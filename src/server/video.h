/*
 * Modern GStreamer Video Widget Header
 */

#ifndef __VIDEO_H__
#define __VIDEO_H__

#include <gtk/gtk.h>
#include <gst/gst.h>

/* Creates a GtkDrawingArea that handles GstVideoOverlay */
GtkWidget *gst_player_video_new (GstElement *playbin);

#endif /* __VIDEO_H__ */
