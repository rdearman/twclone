// server_envelope.h
#ifndef SERVER_ENVELOPE_H
#define SERVER_ENVELOPE_H
#include <jansson.h>

void iso8601_utc (char out[32]);	// if you use timestamps in envelopes
const char *next_msg_id (void);	// if you auto-number messages

void send_enveloped_ok (int fd, json_t * req, const char *type,
			json_t * data);
void send_enveloped_error (int fd, json_t * req, int code, const char *msg);
void send_enveloped_refused (int fd, json_t * req, int code, const char *msg,
			     json_t * data_opt);

#endif
