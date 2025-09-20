#ifndef SERVER_CMDS_H
#define SERVER_CMDS_H

/* No JSON here on purpose: this layer does pure logic/DB.
   server_loop.c still builds/sends the envelopes. */

#ifdef __cplusplus
extern "C"
{
#endif

/* Return codes for auth/user ops (match your protocol doc where possible) */
  enum
  {
    AUTH_OK = 0,
    AUTH_ERR_BAD_REQUEST = 1301,	/* Missing required field(s) */
    AUTH_ERR_INVALID_CRED = 1220,	/* Invalid credentials */
    AUTH_ERR_NAME_TAKEN = 1210,	/* Username already exists */
    AUTH_ERR_DB = 1500		/* Database/internal error */
  };

/* Look up player by name, verify password, return player_id on success. */
  int play_login (const char *player_name,
		  const char *password, int *out_player_id);

/* Create a new player (auto-assigns legacy 'number' sequentially).
   Returns new player_id in out_player_id. */
  int user_create (const char *player_name,
		   const char *password, int *out_player_id);

#ifdef __cplusplus
}
#endif

#endif				/* SERVER_CMDS_H */
