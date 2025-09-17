#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include "msgqueue.h"
#include "parse.h"		// Added to resolve implicit function declaration warning

int
init_msgqueue ()
{
  int msgid;
  if ((msgid = msgget (rand (), IPC_CREAT | IPC_EXCL | 0600)) < 0)
    {
      perror ("Failure to initialize message queue: ");
      exit (-1);
    }
  return msgid;
}

void
clean_msgqueues (int msgidin, int msgidout, char *filename)
{
  FILE *msglock = NULL;
  char buffer[BUFF_SIZE];

  if (fork () == 0)
    {
      sprintf (buffer, "%d", msgidin);
      fprintf (stderr, "Killing message queue with id %s...", buffer);
      if (execlp ("ipcrm", "ipcrm", "msg", buffer, NULL) < 0)
	{
	  perror ("Unable to exec: ");
	  printf ("Please run 'ipcrm msg %d'\n", msgidin);
	}
      exit (0);
    }

  if (fork () == 0)
    {
      sprintf (buffer, "%d", msgidout);
      fprintf (stderr, "Killing message queue with id %s...", buffer);
      if (execlp ("ipcrm", "ipcrm", "msg", buffer, NULL) < 0)
	{
	  perror ("Unable to exec: ");
	  printf ("Please run 'ipcrm msg %d'\n", msgidout);
	}
      exit (0);
    }
}

// Retrieves a JSON message from the message queue.
char *
getmsg (int msgid, long mtype, int *n)
{
  struct json_msgbuffer msg_buffer;
  ssize_t received_size;

  // Receive message from the queue, blocking until one is available.
  // The message type is `mtype`, which corresponds to the sender ID.
  received_size =
    msgrcv (msgid, &msg_buffer, sizeof (msg_buffer.buffer), mtype, 0);
  if (received_size < 0)
    {
      perror ("getmsg: unable to receive message from queue: ");
      return NULL;
    }

  // Allocate memory for the JSON string and copy the data.
  char *json_string = (char *) malloc (received_size + 1);
  if (json_string == NULL)
    {
      perror ("getmsg: Failed to allocate memory for JSON string.");
      return NULL;
    }
  memcpy (json_string, msg_buffer.buffer, received_size);
  json_string[received_size] = '\0';

  // We don't have the original sender ID from the new API, so we'll use 
  // the mtype field of the message. In the `handle_player` function,
  // this will be the thread ID we assigned to the player.
  // Note: This might not be a robust way to handle multiple sender IDs.
  // A better approach would be to include the sender ID within the JSON payload.
  *n = mtype;

  return json_string;
}

// Sends a JSON message to the message queue.
void
sendmesg (int msgid, json_t *message, long mtype, long senderid)
{
  struct json_msgbuffer msg_buffer;
  const char *json_string;
  size_t string_len;

  // Serialize the JSON object to a compact string.
  json_string = json_dumps (message, JSON_COMPACT);
  if (!json_string)
    {
      fprintf (stderr, "sendmesg: Failed to serialize JSON object.\n");
      return;
    }

  string_len = strlen (json_string);
  if (string_len >= BUFF_SIZE)
    {
      fprintf (stderr,
	       "sendmesg: JSON message is too large for the buffer.\n");
      free ((void *) json_string);
      return;
    }

  // Populate the message buffer and send.
  msg_buffer.mtype = mtype;
  strncpy (msg_buffer.buffer, json_string, string_len);
  msg_buffer.buffer[string_len] = '\0';

  if (msgsnd (msgid, &msg_buffer, string_len + sizeof (long), 0) < 0)
    {
      perror ("sendmesg: unable to send message to queue: ");
    }

  free ((void *) json_string);
}
