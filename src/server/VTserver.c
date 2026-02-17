/*
 * $Id: main.c,v 1.26 2001/11/13 18:31:33 alex Exp $
 *
 * (C) 2001 Void Technologies
 * Author: Alex Fiori <alex@void.com.br>
 */

#include "VTserver.h"

static void finish  (void);
static void sfinish (int sig);
static int already_finished = 0;

void show_copyright(void)
{
    g_printerr(PROGRAM_DESCRIPTION "\n");
    g_printerr(PROGRAM_AUTHORS "\n");
}

/* Lightweight poller to start playback if stopped */
static gboolean check_queue_startup(gpointer data)
{
    VTmpeg *mpeg;

    /* If we are not playing, check the queue */
    if (!md_gst_is_playing()) {
        if ((mpeg = unix_getvideo()) != NULL) {
            g_printerr("Starting playback: %s\n", mpeg->filename);
            md_gst_play(mpeg->filename);
        }
    }

    /* Continue polling */
    return TRUE; 
}

int main (int argc, char **argv)
{
    GtkWidget *win;
    gint r;

    /* Initialize GTK and GStreamer */
    /* Handled inside md_gst_init or prior to it */
    /* Actually better to init them here or pass pointers */
    /* md_gst_init will call gst_init */
    
    gtk_init(&argc, &argv);

    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "Video Daemon");
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    g_signal_connect(G_OBJECT(win), "delete_event", G_CALLBACK(finish), NULL);
    gtk_widget_set_size_request(GTK_WIDGET(win), 720, 480);
    gtk_window_move(GTK_WINDOW(win), 0, 0);
    
    /* Show before init to ensure window XID is available if needed */
    gtk_widget_show_all(win);

    r = md_gst_init(&argc, &argv, win);
    if(r < 0) {
        g_printerr("md_gst_init() failed, aborting.\n");
        gtk_widget_destroy(GTK_WIDGET(win));
        exit(EXIT_SUCCESS);
    }

    signal (SIGINT,  sfinish);
    signal (SIGTERM, sfinish);
    signal (SIGSEGV, sfinish);
    signal (SIGHUP,  SIG_IGN);
    signal (SIGPIPE, SIG_IGN);

    show_copyright ();

    /* cria o server de unix domain sockets */
    if (!unix_server ()) {
        fprintf (stderr, "VTmpegd: Cannot create the server.\n");
        return 0;
    }

    /* Use a lightweight poller for startup only/idle check */
    g_timeout_add_seconds(1, (GSourceFunc)check_queue_startup, NULL);

    gtk_main();

    return 1;
}

/* finaliza o processo */
void finish (void) 
{
    thread_lock ();

    /* para não ser feito por todos
       os processos (main e thread) */
    if (already_finished) {
        thread_unlock ();
        exit (EXIT_SUCCESS);
    }

    /* se tiver algum video passando, da stop 
       e já limpa a lista */
    unix_finish ();

    /* remove o socket */
    unlink (unix_sockname ());

    g_printerr("Goodbye.\n");
    already_finished = 1;

    thread_unlock ();

    md_gst_finish();
    gtk_main_quit();

    exit(EXIT_SUCCESS);
}

void sfinish (int sig)
{
    fprintf (stderr, "VTmpegd: Received signal %d, exiting.\n", sig);
    close (0); close (1); close (2);
    finish ();
}
