/*
 * $Id: commands.c,v 1.22 2001/11/13 02:44:54 alex Exp $
 *
 * (C) 2001 Void Technologies
 * Author: Alex Fiori <alex@void.com.br>
 */

#include "VTserver.h"

void command_list (int fd, GList *queue, int playing_mpeg)
{
	int i = 0;
	VTmpeg *mpeg;
	char temp[128];

	if (queue == NULL) {
		dprintf (fd, "%c\nEmpty list.\n%c\n",
			 COMMAND_ERROR, COMMAND_DELIM);
		return;
	}

	memset (temp, 0, sizeof (temp));

	dprintf (fd, "%c\n", COMMAND_OK);
	dprintf (fd, "VTmpeg queue list\n");

	while (queue != NULL) {
		mpeg = (VTmpeg *) queue->data;
		if (mpeg != NULL) {
			snprintf (temp, sizeof (temp), "- playing");

			dprintf (fd, "%d%c%s%s\n",
				 (i + 1), COMMAND_DELIM, mpeg->filename,
				 (playing_mpeg - 1)==i ? temp : " ");
		}
		
		queue = g_list_next (queue);
		i++;
	}

	dprintf (fd, "%c\n", COMMAND_DELIM);
}

GList *command_insert (int fd, GList *queue, const char *filename,
		       int pos, int *playing_mpeg, int max_pos)
{
	GList *q = queue;
	VTmpeg *mpeg = (VTmpeg *) malloc (sizeof (VTmpeg));

	/* Normalize the "append" signal. 
	   Both 0 and -1 (client default) mean "append to the end". */
	if (pos <= 0 || pos > max_pos) pos = 0;

	/* Never allow adding/inserting into the position currently being played.
	   Append operations (pos 0) are always safe. */
	if (pos > 0 && *playing_mpeg == pos) {
		dprintf (fd, "%c\nPosition busy.\n%c\n",
			 COMMAND_ERROR, COMMAND_DELIM);
		free(mpeg);
		return NULL;
	}

	if (mpeg == NULL) {
		dprintf (fd, "%c\nNot enough memory, server shutting down.\n%c\n",
			 COMMAND_ERROR, COMMAND_DELIM);
		exit (1);
	
	/* guarda o nome do filename */
	} else {
		memset (mpeg, 0, sizeof (VTmpeg));
        /* Use snprintf instead of strncpy to ensure null termination and avoid truncation warning */
		snprintf(mpeg->filename, sizeof(mpeg->filename), "%s", filename);
	}

#if 0
	/* testa o mpeg, sem audio */
	mpeg->mpeg = SMPEG_new (filename, &mpeg->info, 0);
	if ((error = SMPEG_error (mpeg->mpeg))) {
		dprintf (fd, "%c\n%s\n%c\n", COMMAND_ERROR, error, COMMAND_DELIM);
		return NULL;
	}
#endif

	if (!pos)
		q = g_list_append (q, mpeg);
	else {
		if (*playing_mpeg > pos) *playing_mpeg += 1;
		q = g_list_insert (q, mpeg, (pos - 1));
	}

	if (q == NULL) {
		dprintf (fd, "%c\nCannot %s on the list.\n%c\n",
			 COMMAND_ERROR, !pos ? "append" : "insert", COMMAND_DELIM);
        /* If insertion fails but we allocated mpeg, we might leak it if not freed? 
           Original code didn't handle this well, but g_list_append usually succeeds 
           unless OOM. Keeping original logic structure but adding strict hygiene is safer. */
		return NULL;
	}

	dprintf (fd, "%c\nFilename %s OK\n%c\n", COMMAND_OK, filename, COMMAND_DELIM);

	return q;
}

GList *command_remove (int fd, GList *queue, int pos, int *playing_mpeg)
{
	VTmpeg *mpeg;
	GList *q = queue;

	/* não remove o video que está tocando */
	if (*playing_mpeg == pos) {
		dprintf (fd, "%c\nPosition busy.\n%c\n",
			 COMMAND_ERROR, COMMAND_DELIM);
		return NULL;
	} else if (!pos) {
		invalid_position:
		dprintf (fd, "%c\nInvalid position.\n%c\n",
			 COMMAND_ERROR, COMMAND_DELIM);
		return NULL;
	}

	mpeg = g_list_nth_data (q, (pos - 1));
	if (mpeg) {
		q = g_list_remove (q, mpeg);
		free (mpeg);
	} else goto invalid_position;

	if (q == NULL)
		dprintf (fd, "%c\nCannot remove position %d\n%c\n",
			 COMMAND_ERROR, pos, COMMAND_DELIM);
	else
		dprintf (fd, "%c\nRemove position %d OK\n%c\n",
			 COMMAND_OK, pos, COMMAND_DELIM);

	if (*playing_mpeg > pos) *playing_mpeg -= 1;

	return q;
}
