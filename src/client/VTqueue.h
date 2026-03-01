#ifndef _VTQUEUE_H
#define _VTQUEUE_H

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <limits.h>

#include "config.h"

typedef enum {
    ADD = 0,
    REM,
    LIST,
    PAUSE_CMD,
    STOP_CMD,
    RESUME_CMD
} VTCommandType;

typedef struct {
    VTCommandType cmd;
    char          uri[PATH_MAX];
    int           idx;
} VTCommand;

extern int send_cmd(int fd, const char *cmd);
extern char *get_cmd_result(FILE *fp);

#endif
