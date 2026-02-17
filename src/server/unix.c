/*
 * $Id: unix.c,v 1.19 2001/11/13 18:31:33 alex Exp $
 *
 * (C) 2001 Void Technologies
 * Author: Alex Fiori <fiorix@gmail.com>
 */

#include "VTserver.h"

static int   server_fd;
static int   unix_command = 0;
static int   playing_mpeg = -1;
static GList *queue, *temp_queue;

static void *unix_loop   (void *arg);
static void  unix_client (int fd);

static int unix_list_count (void)
{
	int count = 0;
	GList *q = g_list_first (queue);

	while ((q = g_list_next (q)) != NULL) count++;
	return count;
}

/* guarda o nome do unix domain socket */
char *unix_sockname (void)
{
	int i = 0;
	struct stat st;
	static char temp[128], filename[128];

	while (*filename == '\0') {
		memset (temp, 0, sizeof (temp));
		snprintf (temp, sizeof (temp), "%s.%d", UNIX_PATH, i);
		if (stat (temp, &st) < 0) {
			strncpy (filename, temp, sizeof (filename));
			break;
		}

		i++;
	}

	return filename;
}

/* cria o server (unix domain sockets)
   pro client poder acessar o queue */
int unix_server (void)
{
	int fd;
	pthread_t th;
	struct sockaddr_un s;

	s.sun_family = AF_UNIX;
	snprintf (s.sun_path, sizeof (s.sun_path), "%s", unix_sockname ()); 

	if ((fd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0) return 0;
	if (bind (fd, (struct sockaddr *) &s, sizeof (s)) < 0) return 0;
	if (listen (fd, 1) < 0) return 0;

	chmod (unix_sockname (), 0666);

	/* cria a lista */
	temp_queue = queue = NULL;

	server_fd = fd;
	pthread_create (&th, NULL, unix_loop, NULL);

	/* cria o symlink pra esse server */
	unlink (UNIX_PATH);
	if (symlink (unix_sockname (), UNIX_PATH) < 0)
        perror("symlink");

	return 1;
}

VTmpeg *unix_getvideo (void)
{
	VTmpeg *mpeg;
	GList *q = g_list_first (queue);

	thread_lock ();

	if (playing_mpeg > unix_list_count ()) playing_mpeg = 0;

	/* se não tiver o primeiro video... */
	if (q == NULL) {
		playing_mpeg = -1;
		thread_unlock ();
		return NULL;
	}

	/* se tiver, o playing_mpeg é 'pelo menos' zero */
	mpeg = g_list_nth_data (q, playing_mpeg);
	if (mpeg == NULL) {
		playing_mpeg = 0;
		thread_unlock ();
		return NULL;
    }
#if 0
	if (mpeg->mpeg == NULL) {
		fulerzeu:

		playing_mpeg = 0;
		thread_unlock ();
		return NULL;
	} else 
#endif
        playing_mpeg++;

	thread_unlock ();

	return mpeg;
}

/* informa o atual comando executado pelo queue */
int unix_get_command (void)
{
	int command;

	//thread_lock ();
	command = unix_command;
	unix_command = 0;
	//thread_unlock ();

	return command;
}

/* fecha o server e mata a lista */
void unix_finish (void)
{
	shutdown (server_fd, 2);
	close (server_fd);

	g_list_free (g_list_first (queue));
	return;
}

/* atende os clientes */
void *unix_loop (void *arg)
{
	fd_set fds;
	int fd, cfd;
    socklen_t len;
	struct timeval tv;
	struct sockaddr_un s;

	fd = server_fd;

	for (;;) {
		FD_ZERO (&fds);
		FD_SET (fd, &fds);
		tv.tv_sec = 1; tv.tv_usec = 0;

		if (select (fd + 1, &fds, NULL, NULL, &tv)) {
			thread_lock ();

			len = sizeof (s);
			memset (&s, 0, sizeof (s));
			if ((cfd = accept (fd, (struct sockaddr *) &s, &len)) < 0)
				perror ("accept"), exit (1);

			/* trata os comandos */
			unix_client (cfd);

			shutdown (cfd, 2);
			close (cfd);

			thread_unlock ();
		}
	}

	return NULL;
}

void unix_client (int fd)
{
	GList *q = NULL;
	char temp[2048];

	memset (temp, 0, sizeof (temp));
	if (read (fd, temp, sizeof (temp)) < 0) return;

	switch (atoi (temp)) {
	case COMMAND_LIST:
		command_list (fd, g_list_first (queue), playing_mpeg);
		break;

	case COMMAND_INSERT:
	{
		int pos;
		char filename[1024];

		memset (filename, 0, sizeof (filename));
		sscanf (temp + 2, "%[^];];%d\n", filename, &pos);

		q = command_insert (fd, queue, filename,
				    pos, &playing_mpeg, unix_list_count ());
		if (q != NULL) queue = g_list_first (q);
	}
		break;

	case COMMAND_REMOVE:
	{
		int pos = 0;

		sscanf (temp + 2, "%d\n", &pos);
		q = command_remove (fd, queue, pos, &playing_mpeg);
		if (q != NULL) {
			unix_command = COMMAND_REMOVE;
			queue = g_list_first (q);
		}
	}
		break;

	case COMMAND_PLAY:
		unix_command = COMMAND_PLAY;
		break;
	
	case COMMAND_PAUSE:
		unix_command = COMMAND_PAUSE;
		break;
	
	case COMMAND_STOP:
		unix_command = COMMAND_STOP;
		break;

	case COMMAND_NEXT:
		unix_command = COMMAND_NEXT;
		break;
	
	case COMMAND_PREV:
	{
		int temp;

		if ((temp = playing_mpeg - 2) < 0) temp = unix_list_count ();
		playing_mpeg = temp;

		unix_command = COMMAND_NEXT;
	}
		break;

	case COMMAND_MUTE:
		unix_command = COMMAND_MUTE;
		break;

	default:
		dprintf (fd, "%c: Unknown command.\n%c\n",
			 COMMAND_ERROR, COMMAND_DELIM);
	}

	return;
}
