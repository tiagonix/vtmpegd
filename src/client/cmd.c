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

/*
 * Sends a command to the server.
 * Returns:
 *  -1 on system/socket error
 *   1 on success (command sent)
 *
 * NOTE: This function ONLY sends. All response reading must be handled
 * by the caller using the stdio FILE* stream.
 */
int send_cmd(int fd, const char *cmd)
{
   	if (!cmd)
	   	return -1;
	
    /* SECURITY FIX: Use %s to prevent format string attacks */
	if (dprintf(fd, "%s", cmd) < 0)
	   	return -1;
	
    /*
     * The raw read() was removed to prevent mixing I/O paradigms.
     * The caller is now responsible for reading the entire response,
     * including status characters, via the buffered FILE* stream.
     */
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
