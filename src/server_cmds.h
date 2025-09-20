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


  typedef enum
  { DEC_OK = 0, DEC_REFUSED = 1, DEC_ERROR = 2 } decision_status_t;

  typedef struct
  {
    decision_status_t status;
    int code;			/* for REFUSED/ERROR */
    const char *message;	/* short msg */
  } decision_t;

  static inline decision_t ok (void)
  {
    return (decision_t)
    {
    DEC_OK, 0, NULL};
  }
  static inline decision_t refused (int code, const char *m)
  {
    return (decision_t)
    {
    DEC_REFUSED, code, m};
  }
  static inline decision_t err (int code, const char *m)
  {
    return (decision_t)
    {
    DEC_ERROR, code, m};
  }



#ifdef __cplusplus
}
#endif

#endif /* SERVER_CMDS_H */
