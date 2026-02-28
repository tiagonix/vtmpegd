/*
 * $Id: cmd.c,v 1.5 2001/11/10 00:13:31 flv Exp $
 *
 * (C) 2001 Void Technologies
 * Authors:
 *   Alex Fiori <fiorix@gmail.com>
 *   Flávio Mendes <flavio.ayra@gmail.com>
 *   Thiago Camargo <thiagocmc@proton.me>
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
   	char buf[2];
	
   	if (!cmd)
	   	return -1;
	
    /* SECURITY FIX: Use %s to prevent format string attacks */
	if (dprintf(fd, "%s", cmd) < 0)
	   	return -1;
	//usleep(500);

    /* 
     * Simplified receive logic:
     * Rely on SO_RCVTIMEO set in VTqueue.c instead of manual select().
     */
    memset(buf, 0, sizeof(buf));
    if (read(fd, buf, sizeof(buf)) <= 0)
        return -1;

    if (*buf == COMMAND_ERROR) {
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
    /* 
     * Simplified receive logic:
     * Rely on SO_RCVTIMEO set in VTqueue.c instead of manual select().
     * This allows fgets to buffer correctly without race conditions.
     */
    memset(result_buf, 0, MAX_RESULT_LINE_LEN);	
    if (!fgets(result_buf, MAX_RESULT_LINE_LEN, fp))
        return NULL;

    if (*result_buf == COMMAND_DELIM)
        return NULL;

    return result_buf;
}
