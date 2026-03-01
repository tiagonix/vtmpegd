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

static void *unix_loop   (void *arg);
static void  unix_client (int fd);

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

    if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        return 0;
    }
    if (bind(fd, (struct sockaddr *) &s, sizeof(s)) < 0) {
        perror("bind");
        close(fd);
        return 0;
    }
    if (listen(fd, 1) < 0) {
        perror("listen");
        close(fd);
        return 0;
    }

    chmod(unix_sockname(), 0666);

    server_fd = fd;
    pthread_create(&th, NULL, unix_loop, NULL);

    unlink(UNIX_PATH);
    if (symlink(unix_sockname(), UNIX_PATH) < 0)
        perror("symlink");

    return 1;
}

void unix_finish (void)
{
    shutdown(server_fd, 2);
    close(server_fd);
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
            if ((cfd = accept(fd, (struct sockaddr *) &s, &len)) < 0) {
                /*
                 * RESILIENCE: Log the error but do NOT exit. 
                 * Transient network errors (EINTR, ECONNABORTED) must not kill the daemon.
                 */
                perror("accept");
                continue;
            }
            
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
    char temp[PATH_MAX + 128];
    ssize_t bytes_read;

    memset(temp, 0, sizeof(temp));
    
    /* Securely read from socket, ensuring null termination. */
    /* This blocks, so we MUST NOT hold the mutex here. */
    bytes_read = read(fd, temp, sizeof(temp) - 1);
    if (bytes_read <= 0) return;
    temp[bytes_read] = '\0';

    /* Process command - all locking is now handled inside command_process */
    char *response = command_process(temp);

    /* Perform socket I/O */
    if (response) {
        dprintf(fd, "%s", response);
        g_free(response);
    }
}
