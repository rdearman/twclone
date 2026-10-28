"""Shared libargon2 helpers used by offline password migration tools."""

import base64
import ctypes
import ctypes.util
import re
import secrets

HASH_PATTERN = re.compile(
    rb"^\$argon2id\$v=19\$m=[0-9]+,t=[0-9]+,p=[0-9]+"
    rb"\$[A-Za-z0-9+/]+\$[A-Za-z0-9+/]+$"
)
ARGON2_VERIFY_MISMATCH = -35


def load_argon2():
    library = ctypes.util.find_library("argon2") or "libargon2.so.1"
    api = ctypes.CDLL(library)
    api.argon2id_hash_encoded.argtypes = [
        ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32,
        ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t,
        ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t,
    ]
    api.argon2id_hash_encoded.restype = ctypes.c_int
    api.argon2id_verify.argtypes = [ctypes.c_char_p, ctypes.c_void_p, ctypes.c_size_t]
    api.argon2id_verify.restype = ctypes.c_int
    return api


def encoded_hash(api, password):
    salt = secrets.token_bytes(16)
    output = ctypes.create_string_buffer(256)
    password_buf = ctypes.create_string_buffer(password, len(password) + 1)
    salt_buf = ctypes.create_string_buffer(salt, len(salt) + 1)
    rc = api.argon2id_hash_encoded(
        3, 65536, 1, password_buf, len(password), salt_buf, len(salt),
        32, output, len(output),
    )
    if rc != 0:
        raise RuntimeError("Argon2id hashing failed")
    return output.value.decode("ascii")


def is_valid_encoded(api, value):
    raw = value.encode("ascii", "strict")
    if not HASH_PATTERN.fullmatch(raw):
        return False
    for part in raw.rsplit(b"$", 2)[1:]:
        base64.b64decode(part + b"=" * ((4 - len(part) % 4) % 4), validate=True)
    rc = api.argon2id_verify(raw, None, 0)
    return rc in (0, ARGON2_VERIFY_MISMATCH)
