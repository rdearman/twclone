#include "password_hash.h"
#include <stdio.h>
#include <string.h>

int
main (void)
{
  char first[TW_PASSWORD_HASH_MAX];
  char second[TW_PASSWORD_HASH_MAX];
  if (tw_password_hash ("correct horse battery staple", first, sizeof first)
      || tw_password_hash ("correct horse battery staple", second, sizeof second))
    return 1;
  if (strcmp (first, second) == 0)
    return 2;
  if (tw_password_verify (first, "correct horse battery staple") != 1)
    return 3;
  if (tw_password_verify (first, "wrong password") != 0)
    return 4;
  if (tw_password_verify ("$argon2id$malformed", "anything") != 0)
    return 5;
  puts ("Argon2id hash, verification, unique salts and malformed hash checks passed.");
  return 0;
}
