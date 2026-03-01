#ifndef _CONFIG_H
#define _CONFIG_H 1

/* path pro arquivo que o server
   cria (unix domain socket) e
   o client se conecta pra mexer
   no queue */
#define UNIX_PATH "/tmp/VTmpegd"

/* tamanho máximo dos comandos
   entre o client e o server */
#define MAX_COMMAND_LEN 20

/* tamannho maximo da msg de result
   do servidor */
#define MAX_RESULT_LINE_LEN 2048

/* Hard limit on queue depth to prevent memory exhaustion DoS */
#define MAX_QUEUE_LEN 2048

/* definições do widget onde deverá passar o mpeg */
#define VIDEO_WIDTH	640
#define VIDEO_HEIGHT	480
#define VIDEO_DEPTH	16

/*
  IPC Protocol Specification

  The client sends a numeric command ID followed by optional arguments.
  The server responds with a status character (COMMAND_OK/COMMAND_ERROR)
  and an optional payload, terminated by COMMAND_DELIM.

  ID   Command   Arguments              Description
  --------------------------------------------------------------------
  1    LIST                             Lists the current video queue.
  2    INSERT    [filename];[pos]       Inserts a video at a given
                                        position (0 for end).
  3    REMOVE    [pos]                  Removes the video at the given
                                        position.
  4    PLAY                             Resumes playback.
  5    PAUSE                            Pauses playback.
  6    STOP                             Stops playback and returns to
                                        the standby screen.
  7    NEXT                             (Server-side only)
  8    PREV                             (Server-side only)
  9    MUTE                             (Not implemented)
  10   STATUS                           Gets current playback status
                                        and progress.
*/
#define COMMAND_OK	'S'
#define COMMAND_ERROR	'E'
#define COMMAND_DELIM	';'

#define COMMAND_LIST    1
#define COMMAND_INSERT  2
#define COMMAND_REMOVE  3
#define COMMAND_PLAY    4
#define COMMAND_PAUSE   5
#define COMMAND_STOP    6
#define COMMAND_NEXT    7
#define COMMAND_PREV    8
#define COMMAND_MUTE    9
#define COMMAND_STATUS  10

#endif /* config.h */
