# V2 password change: client workflow and deployment boundary

The V2 server command is `auth.change_password`. It requires an authenticated
session and accepts exactly `old_password` and `new_password`. A successful
reply is `status: ok` with `data.success: true`. Error 1101 means the current
password was not accepted; 401 means no authenticated session; 1301 means a
required field is missing; 1103 is a database failure. The handler has no
confirmation field or password policy; clients ask for confirmation locally
and send only the two protocol fields. Keep confirmation matching exact and
do not trim password contents.

## Python terminal client

Start the client normally, log in, then choose **User Settings → Change
password**. The client prompts without echo for the current password, new
password, and confirmation. A mismatch or empty value is rejected locally.
After success, the current session remains active; log out and use the new
password on the next login. Do not pass credentials on the command line.

## Godot client

Open **Commands → Settings & Notes → Change password**. Enter the current
password, new password, and confirmation in the masked fields, then submit.
The dialog rejects empty or mismatched values locally, sends only the V2
`old_password` and `new_password` fields, and clears the inputs. After
confirmation, log out and use the new password at the next login.

## Storage compatibility warning

The V2 handler and repository in the committed release history update the
stored password using the legacy plaintext representation. The newer dirty
Argon2id implementation and hash-enforcement migration are separate deployment
work and are not evidence that the currently running executable/database use
hashes. Do not change a live account password until the operator has verified
that the matching Argon2id server build and database migration are deployed.
Never run this workflow against a live account for client qualification.

The client tests cover request shape, confirmation, refusals, safe diagnostics,
and a fresh-login sequence with generated test values. They do not replace an
isolated C-server/PostgreSQL integration test.
