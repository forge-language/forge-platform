# Registry API

`GET /api/health`, `GET /api/stats`, `GET /api/me` (Bearer JWT).
`GET /api/packages?q=<search>` returns up to 100 latest releases.
`GET /api/packages/<name>` returns owner and versions sorted numerically.
`GET /api/packages/<name>/versions/<x.y.z>` returns an immutable manifest.
`POST /api/packages` publishes a release (Bearer JWT, JSON body).
GitHub OAuth: `GET /api/auth/github/login?state=/publish` and callback.

Manifest: `name`, `version`, `description`, `repository_url` (GitHub HTTPS),
`git_commit` (40 lowercase hex), `module` (relative .fg path), `license`,
`dependencies` (object of exact stable versions), optional `readme`.
Native bridge descriptor: `native: {library,cmake_target,pkg_config:[]}`.
Browser bridge descriptor: `javascript: {entry,package_file}`.
See backend/seed.json for actual published descriptors.

Unauthorized=401, ownership violation=403, unknown resource=404, invalid
manifest=400, duplicate release=409, publish rate limit=429, database unavailable=503.
Tokens must have user/admin role and valid expiry/signature. OAuth state has a
signed random nonce bound to a HttpOnly/SameSite cookie. SQL values are bound
parameters. PostgreSQL advisory locking serializes first ownership/duplicate
version decisions inside a transaction.

Version ranges, yanking, ownership transfer, arbitrary uploaded archives and
non-GitHub repositories are not supported by this preview.
