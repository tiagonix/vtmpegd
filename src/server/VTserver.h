/*
 * $Id: main.h,v 1.14 2001/11/13 04:44:48 alex Exp $
 *
 * (C) 2001 Void Technologies
 * Author: Alex Fiori <alex@void.com.br>
 */

#ifndef _MAIN_H
#define _MAIN_H 1

/* leia /usr/include/features.h */
#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <glib.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/un.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

/* GTK/gdk */
#include <gtk/gtk.h>
#include <gdk/gdk.h>

/* GStreamer */
#include <gst/gst.h>
#include <gst/video/videooverlay.h>

/* local */
#include "video.h"

/* informações tanto do server
   quanto do client */
#include "config.h"

typedef struct {
	char		filename[1024];
	int		played;
} VTmpeg;

/* gst-backend.c */
extern gint md_gst_init(gint *argc, gchar ***argv, GtkWidget *win);
extern gint md_gst_play(char *uri);
extern gint md_gst_finish(void);
extern int md_gst_is_playing(void);

/* unix.c */
extern char    *unix_sockname (void);
extern int	unix_server (void);
extern VTmpeg  *unix_getvideo (void);
extern int      unix_get_command (void);
extern void     unix_finish (void);

/* commands.c */
extern void     command_list (int fd, GList *queue, int playing_mpeg);
extern GList   *command_insert (int fd, GList *queue, const char *filename,
				int pos, int *playing_mpeg, int max_pos);
extern GList   *command_remove (int fd, GList *queue, int pos, int *playing_mpeg);

/* thread.c */
extern void thread_lock (void);
extern void thread_unlock (void);

/* copyright.c */
#define		PROGRAM_DESCRIPTION "oO VTmpeg - MPEG video player daemon for Linux Oo"
#define     PROGRAM_AUTHORS     "  Alexandre Fiori - <fiorix@gmail.com>\n" \
                                "  Arnaldo Pereira - <egghunt@gmail.com>\n"
extern		void show_copyright (void);

#endif /* VTserver.h */
