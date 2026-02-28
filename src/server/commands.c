/*
 * $Id: commands.c,v 1.22 2001/11/13 02:44:54 alex Exp $
 *
 * (C) 2001 Void Technologies
 * Authors:
 *   Alex Fiori <fiorix@gmail.com>
 *   Thiago Camargo <thiagocmc@proton.me>
 */

#include "VTserver.h"

static char *command_list (GList *queue, int playing_mpeg)
{
    int i = 0;
    VTmpeg *mpeg;
    char temp[128];
    GString *response = g_string_new(NULL);

    if (queue == NULL) {
        g_string_printf(response, "%c\nEmpty list.\n%c\n", COMMAND_ERROR, COMMAND_DELIM);
        return g_string_free(response, FALSE);
    }

    memset(temp, 0, sizeof(temp));

    g_string_append_printf(response, "%c\n", COMMAND_OK);
    g_string_append_printf(response, "VTmpeg queue list\n");

    while (queue != NULL) {
        mpeg = (VTmpeg *) queue->data;
        if (mpeg != NULL) {
            snprintf(temp, sizeof(temp), "- playing");

            g_string_append_printf(response, "%d%c%s%s\n",
                    (i + 1), COMMAND_DELIM, mpeg->filename,
                    (playing_mpeg - 1)==i ? temp : " ");
        }

        queue = g_list_next(queue);
        i++;
    }

    g_string_append_printf(response, "%c\n", COMMAND_DELIM);
    return g_string_free(response, FALSE);
}

static char *command_insert (GList **p_queue, const char *filename,
                             int pos, int *playing_mpeg)
{
    GList *queue = *p_queue;
    VTmpeg *mpeg;
    int max_pos = g_list_length(queue) + 1;

    if (!g_path_is_absolute(filename)) {
        return g_strdup_printf("%c\nError: Path must be absolute.\n%c\n", COMMAND_ERROR, COMMAND_DELIM);
    }

    if (pos <= 0 || pos > max_pos) pos = 0;

    if (pos > 0 && *playing_mpeg == pos) {
        return g_strdup_printf("%c\nPosition busy.\n%c\n", COMMAND_ERROR, COMMAND_DELIM);
    }
    
    mpeg = (VTmpeg *) malloc(sizeof(VTmpeg));
    if (mpeg == NULL) {
        /* This is a fatal error for the server */
        g_printerr("Not enough memory, server shutting down.\n");
        exit(1);
    }

    memset(mpeg, 0, sizeof(VTmpeg));
    snprintf(mpeg->filename, sizeof(mpeg->filename), "%s", filename);

    if (!pos)
        *p_queue = g_list_append(queue, mpeg);
    else {
        if (*playing_mpeg >= pos) *playing_mpeg += 1;
        *p_queue = g_list_insert(queue, mpeg, (pos - 1));
    }

    if (*p_queue == NULL) {
        free(mpeg);
        return g_strdup_printf("%c\nCannot %s on the list.\n%c\n",
                COMMAND_ERROR, !pos ? "append" : "insert", COMMAND_DELIM);
    }

    return g_strdup_printf("%c\nFilename %s OK\n%c\n", COMMAND_OK, filename, COMMAND_DELIM);
}

static char *command_remove (GList **p_queue, int pos, int *playing_mpeg)
{
    VTmpeg *mpeg;
    GList *queue = *p_queue;

    if (*playing_mpeg == pos) {
        return g_strdup_printf("%c\nPosition busy.\n%c\n", COMMAND_ERROR, COMMAND_DELIM);
    } else if (pos <= 0 || (guint)pos > g_list_length(queue)) {
        return g_strdup_printf("%c\nInvalid position.\n%c\n", COMMAND_ERROR, COMMAND_DELIM);
    }

    mpeg = g_list_nth_data(queue, (pos - 1));
    if (mpeg) {
        *p_queue = g_list_remove(queue, mpeg);
        free(mpeg);
    } else {
        /* Should not happen due to length check above, but defensive. */
        return g_strdup_printf("%c\nInvalid position.\n%c\n", COMMAND_ERROR, COMMAND_DELIM);
    }

    if (*playing_mpeg > pos) *playing_mpeg -= 1;

    return g_strdup_printf("%c\nRemove position %d OK\n%c\n", COMMAND_OK, pos, COMMAND_DELIM);
}


char *command_process(const char *payload, GList **queue, int *playing_mpeg, int *cmd_effect)
{
    int command_id = atoi(payload);
    *cmd_effect = 0;

    switch (command_id) {
        case COMMAND_LIST:
            return command_list(g_list_first(*queue), *playing_mpeg);

        case COMMAND_INSERT: {
            int pos = 0;
            int items_matched;
            char filename[PATH_MAX];
            char fmt[64];

            memset(filename, 0, sizeof(filename));
            snprintf(fmt, sizeof(fmt), "%%%zu[^;];%%d\n", sizeof(filename) - 1);
            items_matched = sscanf(payload + 2, fmt, filename, &pos);

            if (items_matched != 2) {
                return g_strdup_printf("%c\nInvalid IPC payload for INSERT.\n%c\n", COMMAND_ERROR, COMMAND_DELIM);
            }
            return command_insert(queue, filename, pos, playing_mpeg);
        }

        case COMMAND_REMOVE: {
            int pos = 0;
            sscanf(payload + 2, "%d\n", &pos);
            *cmd_effect = COMMAND_REMOVE;
            return command_remove(queue, pos, playing_mpeg);
        }

        case COMMAND_PLAY:
        case COMMAND_PAUSE:
        case COMMAND_STOP:
        case COMMAND_NEXT:
        case COMMAND_MUTE:
            *cmd_effect = command_id;
            break;

        case COMMAND_PREV: {
            int t;
            if ((t = *playing_mpeg - 2) < 0) t = g_list_length(*queue) - 1;
            *playing_mpeg = t;
            *cmd_effect = COMMAND_NEXT; /* Signal to backend to start playing */
            break;
        }

        default:
            return g_strdup_printf("%c: Unknown command.\n%c\n", COMMAND_ERROR, COMMAND_DELIM);
    }
    
    return NULL; /* No string response for simple state commands */
}
