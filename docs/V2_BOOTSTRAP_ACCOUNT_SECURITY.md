# V2 bootstrap and QA accounts

## System and Federation Administrator

Fresh PostgreSQL and MySQL installs seed System and Federation Administrator
with the shared Argon2id format. The SQL source contains encoded hashes only;
it does not ship usable plaintext bootstrap credentials. Treat those seed
values as requiring provisioning before production access.

Before opening a production universe to players, an operator must generate a
unique high-entropy password for each account in an approved password manager,
hash each with the shared Argon2id implementation, and update the two
`players.passwd` values using parameterized statements while the server is
stopped. Read secrets with a no-echo prompt or secret-manager integration;
never put them in SQL files, command arguments, shell history, source control,
test output, or deployment logs. The provisioning operation must print only
success/failure, not the secret or encoded hash. Store the credentials in the
production secret manager and deliver them out of band to the authorized
operator. Authenticate through a protected operator channel, then change both
passwords through the normal password-change path before opening the universe
to players. Use the backend's native connection driver for the parameterized
updates; the password format and Argon2id helper are shared across PostgreSQL
and MySQL.

## Optional QA accounts

PostgreSQL seed 095 includes `newguy` and `ai_qa_bot` as optional QA fixtures.
MySQL seed 095 omits them. They are not production bootstrap requirements and
must not be added to production MySQL solely for seed parity. Both PostgreSQL
fixtures are ordinary login-capable players with the standard player type and
starter ship; `ai_qa_bot` retains its existing username and Argon2id hash.
Neither account should be used as a production operator identity.

The Orion captains and Fer are NPC placeholder rows in both seed sets. They
have Argon2id-formatted stored values to satisfy hash-only storage, but are not
login-capable accounts. Their fixture values are not credentials to provision.

Disposable QA accounts created after migration 119 must use the Argon2id
fixture helper or normal registration code. Do not call `register_player`
with a plaintext password.
