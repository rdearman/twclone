#ifndef SERVER_AUTH_H
#define SERVER_AUTH_H
#include <jansson.h>
#include "common.h"             // client_ctx_t
#include "server_envelope.h"    // send_enveloped_* prototypes
#include "database.h"           // play_login, user_create, db_session_*, etc. (when you paste bodies)
#ifdef __cplusplus
extern "C"
{
#endif
int cmd_auth_login (client_ctx_t *ctx, json_t *root);           // "auth.login", legacy "login"
int cmd_auth_register (client_ctx_t *ctx, json_t *root);        // "auth.register"
int cmd_auth_logout (client_ctx_t *ctx, json_t *root);          // "auth.logout"
int cmd_user_create (client_ctx_t *ctx, json_t *root);          // "user.create", legacy "new.user"
int cmd_auth_refresh (client_ctx_t *ctx, json_t *root);
const char *get_welcome_message (int player_id);
int cmd_auth_mfa_totp_verify (client_ctx_t *ctx, json_t *root);
#ifdef __cplusplus
}
#endif
#endif                          /* SERVER_AUTH_H */
