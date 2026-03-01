/*
 * $Id: main.c,v 1.26 2001/11/13 18:31:33 alex Exp $
 *
 * (C) 2001 Void Technologies
 * Authors:
 *   Alex Fiori <fiorix@gmail.com>
 *   Thiago Camargo <thiagocmc@proton.me>
 */

#include "VTserver.h"
#include <glib-unix.h>

static void finish  (void);
static int already_finished = 0;

void show_copyright(void)
{
    g_printerr(PROGRAM_DESCRIPTION "\n");
    g_printerr(PROGRAM_AUTHORS "\n");
}

/*
 * Main-thread callback to start playback safely.
 * Triggered via g_idle_add from the IPC thread.
 */
static gboolean idle_start_playback(gpointer data)
{
    (void)data;

    /*
     * Robust Idle Check:
     * Only start if the pipeline is explicitly in GST_STATE_NULL.
     */
    if (md_gst_is_stopped()) {
        char *filename = command_get_next_video();
        if (filename) {
            g_printerr("Starting playback (event-driven): %s\n", filename);
            md_gst_play(filename);
            g_free(filename);
        }
    }

    return FALSE; /* Run once */
}

static gboolean idle_pause_playback(gpointer data)
{
    (void)data;
    md_gst_pause();
    return FALSE;
}

static gboolean idle_resume_playback(gpointer data)
{
    (void)data;
    md_gst_resume();
    return FALSE;
}

static gboolean idle_stop_playback(gpointer data)
{
    (void)data;
    md_gst_stop();
    return FALSE;
}

/* Public helper called from commands.c */
void start_playback_request(void)
{
    g_idle_add(idle_start_playback, NULL);
}

void pause_playback_request(void)
{
    g_idle_add(idle_pause_playback, NULL);
}

void resume_playback_request(void)
{
    g_idle_add(idle_resume_playback, NULL);
}

void stop_playback_request(void)
{
    g_idle_add(idle_stop_playback, NULL);
}

/*
 * GLib Unix Signal Handler
 * safely handles SIGINT/SIGTERM from the main loop
 */
static gboolean sig_handler(gpointer user_data)
{
    (void)user_data;
    g_printerr("VTmpegd: Received termination signal, exiting.\n");
    finish();
    return G_SOURCE_REMOVE;
}

int main (int argc, char **argv)
{
    GtkWidget *win;
    gint r;
    int c;
    int loop_enabled = 0;
    int watermark_enabled = 0;

    gtk_init(&argc, &argv);

    struct option long_options[] = {
        {"loop",      no_argument, 0, 'l'},
        {"watermark", no_argument, 0, 'w'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "lw", long_options, NULL)) != -1) {
        switch (c) {
            case 'l': loop_enabled = 1; break;
            case 'w': watermark_enabled = 1; break;
            default: break; /* ignore unknowns */
        }
    }

    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "Video Daemon");
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    g_signal_connect(G_OBJECT(win), "delete_event", G_CALLBACK(finish), NULL);
    gtk_widget_set_size_request(GTK_WIDGET(win), 720, 480);
    gtk_window_move(GTK_WINDOW(win), 0, 0);

    /* Show early so XID exists for overlay path */
    gtk_widget_show_all(win);

    r = md_gst_init(&argc, &argv, win, loop_enabled, watermark_enabled);
    if (r < 0) {
        g_printerr("md_gst_init() failed, aborting.\n");
        gtk_widget_destroy(GTK_WIDGET(win));
        exit(EXIT_SUCCESS);
    }

    /* Modern GLib signal handling (Main Loop Safe) */
    g_unix_signal_add(SIGINT, sig_handler, NULL);
    g_unix_signal_add(SIGTERM, sig_handler, NULL);

    /* Ignore signals that are not useful or handled elsewhere */
    signal (SIGHUP,  SIG_IGN);
    signal (SIGPIPE, SIG_IGN);

    show_copyright();

    /* Initialize Command Layer state */
    commands_init(loop_enabled);

    if (!unix_server(loop_enabled)) {
        fprintf(stderr, "VTmpegd: Cannot create the server.\n");
        return 0;
    }

    gtk_main();
    return 1;
}

void finish (void)
{
    thread_lock();

    if (already_finished) {
        thread_unlock();
        exit(EXIT_SUCCESS);
    }

    unix_finish();
    unlink(unix_sockname());

    g_printerr("Goodbye.\n");
    already_finished = 1;

    thread_unlock();

    md_gst_finish();
    gtk_main_quit();

    exit(EXIT_SUCCESS);
}
