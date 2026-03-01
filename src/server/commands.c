/*
 * $Id: commands.c,v 1.22 2001/11/13 02:44:54 alex Exp $
 *
 * (C) 2001 Void Technologies
 * Authors:
 *   Alex Fiori <fiorix@gmail.com>
 *   Thiago Camargo <thiagocmc@proton.me>
 */

#include "VTserver.h"

/* State moved from unix.c to enforce Logic Layering (Invariant 3.4) */
static GList *queue = NULL;
static int playing_mpeg = -1;
static int g_loop_enabled = 0;

void commands_init(int loop_enabled)
{
    g_loop_enabled = loop_enabled;
    queue = NULL;
    playing_mpeg = -1;
}

static char *command_list (void)
{
    int i = 0;
    VTmpeg *mpeg;
    char temp[128];
    GList *iter = g_list_first(queue);
    GString *response = g_string_new(NULL);

    if (iter == NULL) {
        g_string_printf(response, "%c\nEmpty list.\n%c\n", COMMAND_ERROR, COMMAND_DELIM);
        return g_string_free(response, FALSE);
    }

    memset(temp, 0, sizeof(temp));

    g_string_append_printf(response, "%c\n", COMMAND_OK);
    g_string_append_printf(response, "VTmpeg queue list\n");

    while (iter != NULL) {
        mpeg = (VTmpeg *) iter->data;
        if (mpeg != NULL) {
            snprintf(temp, sizeof(temp), "- playing");

            g_string_append_printf(response, "%d%c%s%s\n",
                    (i + 1), COMMAND_DELIM, mpeg->filename,
                    (playing_mpeg - 1)==i ? temp : " ");
        }

        iter = g_list_next(iter);
        i++;
    }

    g_string_append_printf(response, "%c\n", COMMAND_DELIM);
    return g_string_free(response, FALSE);
}

static char *command_insert (const char *filename, int pos)
{
    VTmpeg *mpeg;
    int max_pos = g_list_length(queue) + 1;

    if (g_list_length(queue) >= MAX_QUEUE_LEN) {
        return g_strdup_printf("%c\nQueue is full (max %d items).\n%c\n", COMMAND_ERROR, MAX_QUEUE_LEN, COMMAND_DELIM);
    }

    if (!g_path_is_absolute(filename) && strstr(filename, "://") == NULL) {
        return g_strdup_printf("%c\nError: Path must be absolute or a valid URI.\n%c\n", COMMAND_ERROR, COMMAND_DELIM);
    }

    if (pos <= 0 || pos > max_pos) pos = 0;

    if (pos > 0 && playing_mpeg == pos) {
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
        queue = g_list_append(queue, mpeg);
    else {
        if (playing_mpeg >= pos) playing_mpeg += 1;
        queue = g_list_insert(queue, mpeg, (pos - 1));
    }

    if (queue == NULL) {
        free(mpeg);
        return g_strdup_printf("%c\nCannot %s on the list.\n%c\n",
                COMMAND_ERROR, !pos ? "append" : "insert", COMMAND_DELIM);
    }

    return g_strdup_printf("%c\nFilename %s OK\n%c\n", COMMAND_OK, filename, COMMAND_DELIM);
}

static char *command_remove (int pos)
{
    VTmpeg *mpeg;

    if (playing_mpeg == pos) {
        return g_strdup_printf("%c\nPosition busy.\n%c\n", COMMAND_ERROR, COMMAND_DELIM);
    } else if (pos <= 0 || (guint)pos > g_list_length(queue)) {
        return g_strdup_printf("%c\nInvalid position.\n%c\n", COMMAND_ERROR, COMMAND_DELIM);
    }

    mpeg = g_list_nth_data(queue, (pos - 1));
    if (mpeg) {
        queue = g_list_remove(queue, mpeg);
        free(mpeg);
    } else {
        return g_strdup_printf("%c\nInvalid position.\n%c\n", COMMAND_ERROR, COMMAND_DELIM);
    }

    if (playing_mpeg > pos) playing_mpeg -= 1;

    return g_strdup_printf("%c\nRemove position %d OK\n%c\n", COMMAND_OK, pos, COMMAND_DELIM);
}

char *command_get_next_video(void)
{
    VTmpeg *mpeg;
    char *filename_copy = NULL;

    thread_lock();

    if (queue == NULL) {
        playing_mpeg = -1;
        thread_unlock();
        return NULL;
    }

    if (g_loop_enabled) {
        /* LOOPING MODE: Cycle through the list using an index. */
        if (playing_mpeg < 0) playing_mpeg = 0;

        int len = (int)g_list_length(queue);
        if (playing_mpeg >= len) {
            /* Wrap around */
            playing_mpeg = 0;
        }

        mpeg = g_list_nth_data(queue, playing_mpeg);
        if (mpeg) {
            filename_copy = g_strdup(mpeg->filename);
            playing_mpeg++;
        }
    } else {
        /* FIFO MODE: Consume from the head of the list. */
        GList *head_link = g_list_first(queue);
        if (head_link) {
            mpeg = (VTmpeg *)head_link->data;
            filename_copy = g_strdup(mpeg->filename);

            /* Consume the item: remove from list and free memory */
            queue = g_list_remove(queue, mpeg);
            free(mpeg);
            playing_mpeg = 0;
        }
    }

    thread_unlock();
    return filename_copy;
}

char *command_process(const char *payload)
{
    int command_id = atoi(payload);
    char *response = NULL;
    gboolean was_empty = FALSE;

    /* Locking must be handled here to protect queue mutations */
    thread_lock();
    was_empty = (queue == NULL);

    switch (command_id) {
        case COMMAND_LIST:
            response = command_list();
            break;

        case COMMAND_INSERT: {
            int pos = 0;
            int items_matched;
            char filename[PATH_MAX];
            char fmt[64];

            memset(filename, 0, sizeof(filename));
            snprintf(fmt, sizeof(fmt), "%%%zu[^;];%%d\n", sizeof(filename) - 1);
            items_matched = sscanf(payload + 2, fmt, filename, &pos);

            if (items_matched != 2) {
                response = g_strdup_printf("%c\nInvalid IPC payload for INSERT.\n%c\n", COMMAND_ERROR, COMMAND_DELIM);
            } else {
                response = command_insert(filename, pos);
            }
            break;
        }

        case COMMAND_REMOVE: {
            int pos = 0;
            sscanf(payload + 2, "%d\n", &pos);
            response = command_remove(pos);
            break;
        }

        case COMMAND_PLAY:
        case COMMAND_NEXT:
            /* Start or Resume playback */
            resume_playback_request();
            /* If it was empty, start_playback_request below will handle it too. */
            break;

        case COMMAND_PAUSE:
            pause_playback_request();
            break;

        case COMMAND_STOP:
            stop_playback_request();
            break;

        case COMMAND_PREV: {
            int t;
            if ((t = playing_mpeg - 2) < 0) t = g_list_length(queue) - 1;
            playing_mpeg = t;
            resume_playback_request(); 
            break;
        }

        case COMMAND_MUTE:
            /* Not implemented in backend yet */
            break;

        default:
            response = g_strdup_printf("%c: Unknown command.\n%c\n", COMMAND_ERROR, COMMAND_DELIM);
            break;
    }

    if (was_empty && queue != NULL) {
        start_playback_request();
    }

    thread_unlock();
    
    return response;
}
