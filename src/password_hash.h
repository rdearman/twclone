#ifndef TW_PASSWORD_HASH_H
#define TW_PASSWORD_HASH_H

#include <stddef.h>

#define TW_PASSWORD_HASH_MAX 256

/* Encodes passwords with Argon2id. The encoded string includes its salt and
 * work parameters. Returns 0 on success and -1 on failure. */
int tw_password_hash(const char *password, char *encoded, size_t encoded_size);

/* Returns 1 for a match, 0 for a mismatch or malformed hash, and -1 if the
 * Argon2id runtime library is unavailable. */
int tw_password_verify(const char *encoded, const char *password);

#endif
