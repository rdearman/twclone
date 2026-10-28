#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "password_hash.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/random.h>
#endif

typedef int (*argon2_hash_encoded_fn) (uint32_t, uint32_t, uint32_t,
                                       const void *, size_t,
                                       const void *, size_t, size_t,
                                       char *, size_t);
typedef int (*argon2_verify_fn) (const char *, const void *, size_t);

static void *argon2_handle;
static argon2_hash_encoded_fn hash_encoded;
static argon2_verify_fn verify_encoded;
static pthread_once_t argon2_load_once = PTHREAD_ONCE_INIT;
static int argon2_loaded;

static void
load_argon2_once (void)
{
  argon2_handle = dlopen ("libargon2.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!argon2_handle)
    argon2_handle = dlopen ("libargon2.so", RTLD_NOW | RTLD_LOCAL);
  if (!argon2_handle)
    return;

  *(void **) (&hash_encoded) = dlsym (argon2_handle, "argon2id_hash_encoded");
  *(void **) (&verify_encoded) = dlsym (argon2_handle, "argon2id_verify");
  argon2_loaded = hash_encoded && verify_encoded;
}

static int
load_argon2 (void)
{
  if (pthread_once (&argon2_load_once, load_argon2_once) != 0)
    return -1;
  return argon2_loaded ? 0 : -1;
}

static int
random_salt (unsigned char *salt, size_t size)
{
  size_t done = 0;
#if defined(__linux__)
  while (done < size)
    {
      ssize_t got = getrandom (salt + done, size - done, 0);
      if (got > 0)
        {
          done += (size_t) got;
          continue;
        }
      if (got < 0 && errno == EINTR)
        continue;
      break;
    }
  if (done == size)
    return 0;
#endif

  int fd = open ("/dev/urandom", O_RDONLY
#ifdef O_CLOEXEC
                 | O_CLOEXEC
#endif
                 );
  if (fd < 0)
    return -1;
  done = 0;
  while (done < size)
    {
      ssize_t got = read (fd, salt + done, size - done);
      if (got > 0)
        done += (size_t) got;
      else if (got < 0 && errno == EINTR)
        continue;
      else
        {
          close (fd);
          return -1;
        }
    }
  close (fd);
  return 0;
}

int
tw_password_hash (const char *password, char *encoded, size_t encoded_size)
{
  if (!password || !encoded || encoded_size < TW_PASSWORD_HASH_MAX
      || load_argon2 () != 0)
    return -1;

  unsigned char salt[16];
  if (random_salt (salt, sizeof salt) != 0)
    return -1;

  /* 64 MiB, three passes, one lane: encoded for reproducible verification. */
  int rc = hash_encoded (3, 65536, 1, password, strlen (password), salt,
                         sizeof salt, 32, encoded, encoded_size);
  memset (salt, 0, sizeof salt);
  if (rc != 0)
    {
      encoded[0] = '\0';
      return -1;
    }
  return 0;
}

int
tw_password_verify (const char *encoded, const char *password)
{
  if (!encoded || !password || load_argon2 () != 0)
    return -1;
  if (strncmp (encoded, "$argon2id$", 9) != 0)
    return 0;
  return verify_encoded (encoded, password, strlen (password)) == 0 ? 1 : 0;
}
