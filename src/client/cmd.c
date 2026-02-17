/*
 * $Id: cmd.c,v 1.5 2001/11/10 00:13:31 flv Exp $
 *
 * (C) 2001 Void Technologies
 * Author: Alex Fiori <alex@void.com.br>
 * 	   Flávio Mendes <flv@void.com.br>
 */
#include "VTqueue.h"

static char result_buf[MAX_RESULT_LINE_LEN];

/* return:
 * -1 on system error
 * 0 on error
 * 1 on success
 */
int send_cmd(int fd, const char *cmd)
{
   	fd_set fds;
   	char buf[2];
	struct timeval tv;
	
   	if (!cmd)
	   	return -1;
	
    /* SECURITY FIX: Use %s to prevent format string attacks */
	if (!dprintf(fd, "%s", cmd))
	   	return -1;
	//usleep(500);

	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	tv.tv_sec = 1;
	tv.tv_usec = 0;

	if (select(fd + 1, &fds, NULL, NULL, &tv)) {
		memset(buf, 0, sizeof(buf));
		if (!read(fd, buf, sizeof(buf)))
	   		return -1;

		if (*buf == COMMAND_ERROR)
	   		return 0;
	}
   
	return 1;
}

/* return
 * the result msg on success
 * NULL on error or end of msg
 */
char *get_cmd_result(FILE *fp)
{
   	fd_set fds;
   	int fd;
	struct timeval tv;
	
	fd = fileno(fp);
	
	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	tv.tv_sec = 1;
	tv.tv_usec = 0;

	if (select(fd + 1, &fds, NULL, NULL, &tv)) {
		memset(result_buf, 0, MAX_RESULT_LINE_LEN);	
		if (!fgets(result_buf, MAX_RESULT_LINE_LEN, fp))
	   		return NULL;

		if (*result_buf == COMMAND_DELIM)
	   		return NULL;
	}
		
	return result_buf;
}
