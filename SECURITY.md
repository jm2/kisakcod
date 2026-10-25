# Security policy

KisakCOD is a port of a 2007 game engine whose network code and asset loaders were written for trusted peers. Expect bugs that a malicious server, client or fast file could reach.

## Reporting a vulnerability

Please report it privately, not in a public issue, pull request or discussion:

- use **Report a vulnerability** on the repository's **Security** tab (GitHub private vulnerability reporting), or
- contact the maintainer, **@jm2**, through GitHub.

Include:
- the affected build: commit, target and width;
- what an attacker controls: a server, a client, a fast file, or a script;
- the steps to reproduce it.

Don't include retail game data or files derived from it.

Reports are fixed in a private fork and published as GitHub security advisories once the fix lands. Merged fixes so far are listed on the repository's advisories page.

## Supported versions

Only `master` is supported. Fixes go to `master`, and there are no maintained release branches yet.
