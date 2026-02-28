/*
 * $Id: VTqueue.c,v 1.12 2001/11/13 18:31:33 alex Exp $
 *
 * (C) 2001 Void Technologies
 * Authors:
 *   Alex Fiori <fiorix@gmail.com>
 *   Thiago Camargo <thiagocmc@proton.me>
 */

#include "VTqueue.h"
#include <sys/time.h>
#include <limits.h>

static int debug = 0;

static void VT_command_init(VTCommand *cmd)
{
    if(!cmd) return;
    memset(cmd, 0, sizeof(VTCommand));
    cmd->idx = -1;
}

static int VT_build_command_string(VTCommand *cmd, char *buf, int size)
{
    if(!cmd || !buf || !size) return -1;

    memset(buf, 0, size);
    switch(cmd->cmd) {
        case ADD:
            snprintf(buf, size, "%d %s;%d", 
                    COMMAND_INSERT, cmd->uri, cmd->idx);
            break;
        case REM:
            snprintf(buf, size, "%d %d", COMMAND_REMOVE, cmd->idx);
            break;
        case LIST:
            snprintf(buf, size, "1");
            break;
    }

    return 0;
}

static int VT_send_command(VTCommand *cmd)
{
    int r;
    /* Expanded buffer to handle full path length + IPC overhead */
    char buffer[2048];
    FILE *fp;
    int fd;
    char *p;
    struct sockaddr_un s;

    r = VT_build_command_string(cmd, buffer, sizeof(buffer));
    if(r < 0)
        return -1;

    s.sun_family = AF_UNIX;
    snprintf(s.sun_path, sizeof(s.sun_path), UNIX_PATH);

    if((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    if(connect(fd, (struct sockaddr *) &s, sizeof(s)) < 0) {
        perror("connect");
        exit(1);
    }

    /* 
     * Configure native socket receive timeout.
     * This replaces the manual select() logic in cmd.c and prevents
     * deadlocks with stdio buffering.
     */
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
        perror("setsockopt");
        /* Continue even if timeout setup fails, though behavior may block. */
    }

    if(send_cmd(fd, buffer) <= 0)
        fprintf(stderr, "error: ");

    if(!(fp = fdopen(fd, "r"))) {
        perror("fdopen");
        exit(1);
    }

    while((p = get_cmd_result(fp)) != NULL)
        printf("%s", p);

    //sleep(1);
    shutdown(fd, 2);
    /* Fix: Remove double-close. fclose(fp) handles the fd. */
    fclose(fp);

    return 0;
}

/* 
 * Moved from nested function in main() to file scope for C99 compliance.
 */
static void show_help(const char *progname) {
    fprintf(stdout, "use: %s OPTIONS\n"
            "NOTES  : There are a _lot_ of commands to come.\n"
            "OPTIONS:\n"
            "\t--add,      -a URI       Add URI to server's play queue\n"
            "\t--remove,   -r IDX       Remove IDX from server's play queue\n"
            "\t--position, -p IDX       Queue's index to remove or add the URI into\n"
            "\t--list,     -l           list URIs on the server's queue\n"
            "\t--debug,    -d           run de debug mode\n"
            "\t--help,     -h           this help\n", progname);

    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
    VTCommand cmd;
    int c, optind = 0;
    const char *opts = "a:r:p:ldh";
    const struct option optl[] = {
        { "add",      1, 0, 'a' },
        { "remove",   1, 0, 'r' },
        { "position", 1, 0, 'p' },
        { "list",     0, 0, 'l' },
        { "debug",    0, 0, 'd' },
        { "help",     0, 0, 'h' },
        { 0, 0, 0, 0 }
    };

    VT_command_init(&cmd);
    while((c = getopt_long(argc, argv, opts, optl, &optind)) != -1) {
        switch(c) {
            case 'a':
                cmd.cmd = ADD;
                if(optarg == NULL)
                    show_help(argv[0]);

                /* 
                 * Robust Path Resolution & Validation:
                 * 1. Resolve relative paths to absolute to ensure daemon can find file.
                 * 2. Validate length to prevent silent truncation.
                 */
                if (strstr(optarg, "://")) {
                    /* It's already a URI (e.g. http://), use as is */
                    if (strlen(optarg) >= sizeof(cmd.uri)) {
                        fprintf(stderr, "Error: URI too long (max %zu bytes).\n", sizeof(cmd.uri) - 1);
                        exit(EXIT_FAILURE);
                    }
                    snprintf(cmd.uri, sizeof(cmd.uri), "%s", optarg);
                } else {
                    /* Local file path - resolve absolute path */
                    char resolved_path[PATH_MAX];
                    if (realpath(optarg, resolved_path) == NULL) {
                        perror("realpath");
                        exit(EXIT_FAILURE);
                    }
                    if (strlen(resolved_path) >= sizeof(cmd.uri)) {
                        fprintf(stderr, "Error: Resolved path too long (max %zu bytes).\n", sizeof(cmd.uri) - 1);
                        exit(EXIT_FAILURE);
                    }
                    snprintf(cmd.uri, sizeof(cmd.uri), "%s", resolved_path);
                }
                break;
            case 'r':
                cmd.cmd = REM;
                if(optarg == NULL)
                    show_help(argv[0]);
                cmd.idx = atoi(optarg);
                break;
            case 'p':
                if(optarg == NULL)
                    show_help(argv[0]);
                cmd.idx = atol(optarg);
                break;
            case 'l':
                cmd.cmd = LIST;
                break;
            case 'd':
                debug = 1;
                break;
            case 'h':
                show_help(argv[0]);
                break;
            default:
                show_help(argv[0]);
        }
    }

    /* Validation: ADD commands only require a URI (default idx is -1).
       REM commands require a valid position index. */
    if((cmd.cmd == ADD && strlen(cmd.uri) < 2) || 
            (cmd.cmd == REM && cmd.idx <= 0))
        show_help(argv[0]);

    VT_send_command(&cmd);
    return EXIT_SUCCESS;
}
