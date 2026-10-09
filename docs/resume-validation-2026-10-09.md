# Deployment verification — 2026-10-09

The resumed work verified the already deployed preview.6 services rather than replacing them with older build images.

- `https://forge-lang.org/api/health` and `https://storage.forge-lang.org/health` responded successfully. The public release manifest selects `0.3.0-preview.6`; Forge Storage versions `0.1.1` and `0.1.0` remain available.
- `sh scripts/test.sh` passed: 25 registry cases, 4 Redis cases, 8 security cases, 6 browser cases, and database-offline stateless checks. Containers and volumes were isolated and removed by the script.
- `python3 tests/storage-sdk.py` passed with the actual preview.6 SDK, SHA-256 `71bacfd50e635dc5b9edcd710e2365317eb31b9c6ccc23686fd7530b713f66ad`. The fixture explicitly trusts the reviewed Forge Web and Forge Storage native source pins and asserts those grants match the lockfile. Installation, compilation, upload/download byte equality, stable digests, wrong-digest rejection, and repeated cache reuse passed against disposable storage.
- Four public learning UI cases passed, exercising every lesson with the real WASM compiler, error recovery, English/Korean selection, mobile layout, source downloads, bounded execution, and unknown-lesson recovery.
- External GitHub Actions downloaded the public preview.6 SDK, ran Hello Forge, installed the pinned storage library, built its native dependencies with explicit trust, and read public object metadata successfully: https://github.com/forge-language/forge-storage/actions/runs/37885074144 . This runner does not use production writer credentials or upload objects.
- The public live manifest had no refresh errors and contained 17 repositories, 8 documents and 91 reports at the observed snapshot. `forge-public-refresh.timer` is the active user-systemd refresh timer; it supersedes the earlier activity timer. Cloudflare Tunnel is also a user service.

Only old Forge browser-test temporary directories and the labeled disposable storage test container were cleaned up. Production data, rollback images and unrelated application stacks were preserved. Disk capacity remains tight; no global Docker pruning was performed.

No production application source change or redeployment was needed for these verification fixes. GitHub OAuth application credentials remain unset; the deployed one-time GitHub-token login provides registry publishing. Forge Storage uses immutable hash-addressed objects on a persistent local volume; it does not implement the AWS S3 protocol.
