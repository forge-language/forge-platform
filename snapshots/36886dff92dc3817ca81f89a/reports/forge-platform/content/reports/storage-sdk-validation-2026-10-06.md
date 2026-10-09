# Installed Forge Storage SDK verification — 2026-10-06

Passed at 2026-10-06T13:03:20Z using the actual Forge `0.3.0-preview.5` Linux x86_64 SDK and Ubuntu 22.04 development container.

SDK SHA-256: `d00412621b9eededb5dee692589c8b52f500066d0a55c05c7afc8223ab9f8147`.

The installer downloaded and verified the release from an isolated loopback origin. The installed package manager used its builtin snapshot to download the published library archives from `storage.forge-lang.org`; source checkout build products were not used.

| Module | Version | Git commit |
| --- | --- | --- |
| forge-web | 0.1.3 | `496b33aeafbafeb09cf061647f761e75922b62f7` |
| forge-storage | 0.1.1 | `4b6dda70ffb54ce86c5cebfe245d6b09a1df890d` |

Verified the following behavior:

- `forge pkg add forge-storage 0.1.1` resolves the exact Forge Web dependency and installs SHA-256-verified archives.
- `forge build` compiles both native libraries and links their mutual references with the installed compiler and runtime.
- The compiled library example uploads to a disposable local Forge Storage server, reads metadata, downloads with hash verification, and produces identical bytes.
- Hash strings remain distinct when a second file is hashed; a download with a mismatched expected hash is rejected and does not create a destination.
- Repeated package installation preserves verified cache marker timestamps, creates no Git checkout, and leaves no staging files.
- Repeated build and upload/download operations succeed.

The native wrong-byte regression also fails against the previous client implementation and passes against `0.1.1`, including an expected hash borrowed directly from `fs_digest`.

Reproduce with:

```sh
python3 tests/storage-sdk.py
```

The fixture uses a random private test credential and is removed afterward. Production writer credentials and storage data are not accessed.
