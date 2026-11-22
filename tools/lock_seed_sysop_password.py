#!/usr/bin/env python3
"""Emit an Argon2id hash for a one-time random password, never the password."""

import secrets

from argon2id_passwords import encoded_hash, load_argon2


def main() -> int:
    password = secrets.token_bytes(32)
    print(encoded_hash(load_argon2(), password))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
