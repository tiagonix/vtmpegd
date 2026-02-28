/*
 * $Id: unix.c,v 1.19 2001/11/13 18:31:33 alex Exp $
 *
 * (C) 2001 Void Technologies
 * Authors:
 *   Alex Fiori <fiorix@gmail.com>
 *   Thiago Camargo <thiagocmc@proton.me>
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
    /* Fix legacy off-by-one. */
    return (queue == NULL) ? 0 : (int)g_list_length(queue);
}

char *unix_sockname (void)
{
    int i = 0;
    struct stat st;
    static char temp[128], filename[128];

    while (*filename == '\0') {
        memset(temp, 0, sizeof(temp));
        snprintf(temp, sizeof(temp), "%s.%d", UNIX_PATH, i);
        if (stat(temp, &st) < 0) {
            strncpy(filename, temp, sizeof(filename));
            break;
        }
        i++;
    }

    return filename;
}

int unix_server (void)
{
    int fd;
    pthread_t th;
    struct sockaddr_un s;

    s.sun_family = AF_UNIX;
    snprintf(s.sun_path, sizeof(s.sun_path), "%s", unix_sockname());

    if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) return 0;
    if (bind(fd, (struct sockaddr *) &s, sizeof(s)) < 0) return 0;
    if (listen(fd, 1) < 0) return 0;

    chmod(unix_sockname(), 0666);

    temp_queue = queue = NULL;

    server_fd = fd;
    pthread_create(&th, NULL, unix_loop, NULL);

    unlink(UNIX_PATH);
    if (symlink(unix_sockname(), UNIX_PATH) < 0)
        perror("symlink");

    return 1;
}

char *unix_getvideo (void)
{
    VTmpeg *mpeg;
    GList *q;
    char *filename_copy = NULL;

    thread_lock();

    q = g_list_first(queue);

    if (q == NULL) {
        playing_mpeg = -1;
        thread_unlock();
        return NULL;
    }

    /* CRITICAL: normalize startup state */
    if (playing_mpeg < 0)
        playing_mpeg = 0;

    /* Wrap BEFORE fetch. */
    {
        int len = (int)g_list_length(q);
        if (len <= 0) {
            playing_mpeg = -1;
            thread_unlock();
            return NULL;
        }
        if (playing_mpeg >= len) {
            /* End-of-playlist: do NOT wrap here (looping is handled by gst-backend via --loop). */
            playing_mpeg = -1;
            thread_unlock();
            return NULL;
        }
    }

    mpeg = g_list_nth_data(q, playing_mpeg);
    if (mpeg == NULL) {
        playing_mpeg = 0;
        thread_unlock();
        return NULL;
    }

    /* 
     * SAFE COPY: We must duplicate the string while holding the lock.
     * Returning the 'mpeg' pointer is unsafe because the node could be
     * freed by the IPC thread immediately after we unlock.
     */
    filename_copy = g_strdup(mpeg->filename);

    playing_mpeg++;

    thread_unlock();
    return filename_copy;
}

int unix_get_command (void)
{
    int command = unix_command;
    unix_command = 0;
    return command;
}

void unix_finish (void)
{
    shutdown(server_fd, 2);
    close(server_fd);

    /* DEEP CLEAN: Free the VTmpeg structs, then the list nodes. */
    g_list_free_full(g_list_first(queue), free);
    queue = NULL;

    return;
}

void *unix_loop (void *arg)
{
    fd_set fds;
    int fd, cfd;
    socklen_t len;
    struct timeval tv;
    struct sockaddr_un s;

    (void)arg;

    fd = server_fd;

    for (;;) {
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        tv.tv_sec = 1; tv.tv_usec = 0;

        /*
         * FIX: Explicitly check for success (ret > 0) to avoid entering accept
         * on errors (like EINTR from signals) or timeouts.
         */
        int ret = select(fd + 1, &fds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(fd, &fds)) {
            /* 
             * FIX: Do not hold lock during accept() or unix_client() which calls read().
             * This prevents DoS where a client connects but doesn't send data.
             */
            len = sizeof(s);
            memset(&s, 0, sizeof(s));
            if ((cfd = accept(fd, (struct sockaddr *) &s, &len)) < 0)
                perror("accept"), exit(1);
            
            /*
             * DEFENSE: Configure receive timeout on the accepted socket.
             * This prevents the single-threaded loop from blocking indefinitely
             * if a client connects but sends no data.
             */
            struct timeval rtv;
            rtv.tv_sec = 1;
            rtv.tv_usec = 0;
            if (setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rtv, sizeof(rtv)) < 0) {
                perror("setsockopt");
                /* Proceed anyway; robustness is preferred over crashing. */
            }

            unix_client(cfd);

            shutdown(cfd, 2);
            close(cfd);
        }
    }

    return NULL;
}

void unix_client (int fd)
{
    GList *q = NULL;
    char temp[PATH_MAX + 128];
    ssize_t bytes_read;
    gboolean was_empty = FALSE;

    memset(temp, 0, sizeof(temp));
    
    /* Securely read from socket, ensuring null termination. */
    /* This blocks, so we MUST NOT hold the mutex here. */
    bytes_read = read(fd, temp, sizeof(temp) - 1);
    if (bytes_read <= 0) return;
    temp[bytes_read] = '\0';

    /* Acquire lock only for shared state mutation/access */
    thread_lock();

    switch (atoi(temp)) {

        case COMMAND_LIST:
            command_list(fd, g_list_first(queue), playing_mpeg);
            break;

        case COMMAND_INSERT: {
            int pos = 0;
            int items_matched;
            char filename[PATH_MAX];
            char fmt[64];

            memset(filename, 0, sizeof(filename));
            
            /* 
             * DYNAMIC FORMAT: Construct the sscanf format string using the actual
             * buffer size (PATH_MAX) to prevent overflow while supporting long paths.
             * This replaces the hardcoded %1023[^;].
             */
            snprintf(fmt, sizeof(fmt), "%%%zu[^;];%%d\n", sizeof(filename) - 1);
            
            items_matched = sscanf(temp + 2, fmt, filename, &pos);

            /* Strictly require matched items to avoid undefined behavior or coercion */
            if (items_matched != 2) {
                dprintf(fd, "%c\nInvalid IPC payload.\n%c\n", COMMAND_ERROR, COMMAND_DELIM);
                break;
            }

            /* Deterministic start check before mutation */
            was_empty = (queue == NULL);

            /* Fix: Unconditionally update queue. command_insert returns old queue on error. */
            queue = command_insert(fd, queue, filename, pos, &playing_mpeg, unix_list_count());
            
            if (was_empty && queue != NULL) {
                start_playback_request();
            }
            break;
        }

        case COMMAND_REMOVE: {
            int pos = 0;

            sscanf(temp + 2, "%d\n", &pos);
            q = command_remove(fd, queue, pos, &playing_mpeg);
            
            /* Fix: Always update queue. command_remove returns NULL if list becomes empty,
               or original queue if error. Both are valid states for assignment. */
            unix_command = COMMAND_REMOVE;
            queue = g_list_first(q);
            break;
        }

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

        case COMMAND_PREV: {
            int t;
            /* 
             * FIX: Off-by-one logic error.
             * When wrapping backwards from index 0, we must go to (size - 1),
             * not (size). The previous code pointed out-of-bounds, causing
             * unix_getvideo to return NULL and stop playback.
             */
            if ((t = playing_mpeg - 2) < 0) t = unix_list_count() - 1;
            
            playing_mpeg = t;
            unix_command = COMMAND_NEXT;
            break;
        }

        case COMMAND_MUTE:
            unix_command = COMMAND_MUTE;
            break;

        default:
            dprintf(fd, "%c: Unknown command.\n%c\n", COMMAND_ERROR, COMMAND_DELIM);
            break;
    }

    thread_unlock();
}
