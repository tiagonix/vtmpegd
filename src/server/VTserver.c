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

/* roda como daemon */
static void go_background ()
{
    switch (fork ()) {
        case -1:
            g_printerr("fork(): %s\n", strerror(errno));
            exit(EXIT_FAILURE);
            break;
        case 0:
            break;
        default:
            _exit (1);
    }

    return;
}

int videoloop(VTmpeg *mpeg)
{
    int r = 0;
    do {
        thread_lock();
        r = md_gst_is_playing();
        thread_unlock();
        usleep(400000);
    } while(r);

    if ((mpeg = unix_getvideo ()) != NULL) {
        g_printerr("Now playing: %s\n", mpeg->filename);
        md_gst_play(mpeg->filename);
        //md_gst_play_loop(mpeg->filename);
    }
    return 1;
}

int main (int argc, char **argv)
{
    VTmpeg *mpeg = NULL;
    GtkWidget *win;
    gint r;

    gtk_init(&argc, &argv);

    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "Video Daemon");
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    g_signal_connect(G_OBJECT(win), "delete_event", G_CALLBACK(finish), NULL);
    gtk_widget_set_size_request(GTK_WIDGET(win), 720, 480);
    gtk_window_move(GTK_WINDOW(win), 0, 0);
    gtk_widget_show(win);

    r = md_gst_init(argc, argv, win);
    if(r < 0) {
        g_printerr("md_gst_play() failed, aborting.\n");
        gtk_widget_destroy(GTK_WIDGET(win));
        exit(EXIT_SUCCESS);

	/* try it once again */
    } else if(r == 1) {
    	r = md_gst_init(argc, argv, win);
	if(r < 0) {
		g_printerr("md_gst_play() failed, aborting.\n");
		gtk_widget_destroy(GTK_WIDGET(win));
		exit(EXIT_SUCCESS);
	}
    }

    g_print("Video PAUSED. Waiting 1 second... ");
    sleep(1);
    g_print("now playing.\n");
    //md_gst_play();

    signal (SIGINT,  sfinish);
    signal (SIGTERM, sfinish);
    signal (SIGSEGV, sfinish);
    signal (SIGHUP,  SIG_IGN);
    signal (SIGPIPE, SIG_IGN);

    /* pra deixar o programa mais 'verboso', como diz
       o bacana do Thiago */
    show_copyright ();

    /* vai pra background */
    //go_background ();
    //daemon(0, 0); FIXME: can't run properly going to background

    /* cria o server de unix domain sockets */
    if (!unix_server ()) {
        fprintf (stderr, "VTmpegd: Cannot create the server.\n");
        return 0;
    }

    g_timeout_add(500, (GSourceFunc)videoloop, mpeg);
    gtk_main();

#if 0
    /* o processo principal fica em um loop eterno, passando
       os mpegs da lista no widget */
    for (;;) {
        if ((mpeg = unix_getvideo ()) != NULL) player_play (mpeg);
        else sleep (1);
    }
#endif

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
