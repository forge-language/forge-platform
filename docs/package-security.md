# Package source and execution trust

Registry metadata and a pinned commit identify the requested source. They do not prove that source is safe to execute.

The package manager checks every downloaded or cached checkout before install, trust, and build. The commit and canonical GitHub origin must match the lock. Modified tracked files, untracked source (including ignored source), symlinks, submodules, special files, hardlinks, alternate Git object stores, Git-directory redirects, replacement refs, custom filters, configuration includes, and index assume-unchanged/skip-worktree flags are rejected. Package directory ancestors must be real directories. Generated root `build/`, `node_modules/`, and `.forge-npm-fingerprint` files are excluded from untracked-source rejection; their cache fingerprints are verified separately. Git routing environment variables cannot redirect validation to another checkout.

For packages delivered through Forge Storage, the manager retains the verified source archive inside the cache. Before reuse, trust, or build it verifies the archive SHA-256 and byte count against the pinned manifest, performs a bounded extraction into temporary staging, and compares the normalized source tree with the cached source. A marker file cannot authorize changed source. Source archives cannot pre-populate the generated root `build/` or `node_modules/` directories or reserved archive metadata. This adds local verification work to cached builds; it does not fetch the archive again when its pinned bytes are intact.

A failed install preserves the previous `forge.lock`. Remove a damaged package cache directory and run `forge install` to fetch its pinned source again. Updating a dependency requires a new pinned commit, not editing files inside the downloaded cache.

## Native dependencies

CMake configuration and build scripts can execute arbitrary commands with your account's permissions. Native dependencies, including transitive dependencies and official packages, require an explicit grant before any dependency CMake invocation:

```sh
forge install
# Review the repository at the exact commit recorded in forge.lock.
forge trust package-name
forge build
```

The command prints the repository, commit and permission being granted. It records the grant in the project's `forge.json`:

```json
{
  "trust": {
    "package-name": {
      "repository_url": "https://github.com/owner/repository",
      "git_commit": "0123456789012345678901234567890123456789",
      "native": true
    }
  }
}
```

Repository names are canonicalized to lowercase without an optional `.git` suffix. A repository or commit change invalidates the grant. Remove the relevant `trust` entry to revoke it. Project configuration is part of the execution policy: review trust changes when accepting a project's configuration.

## JavaScript dependencies

Browser dependency installation uses `npm ci --ignore-scripts --no-bin-links` by default. If a reviewed pinned package needs lifecycle scripts, grant that permission separately:

```sh
forge trust package-name --npm-scripts
forge build --emit-js
```

This records `npm_scripts: true` for that exact repository and commit and switches npm to `--ignore-scripts=false`. Native permission alone never enables npm scripts. The npm installation fingerprint includes this policy so changing permission cannot reuse an installation made under the previous policy. Lifecycle scripts of transitive npm dependencies may also run once this permission is enabled.

These checks protect package identity, common checkout substitutions and accidental dependency build execution. A trusted native build or enabled npm lifecycle can still access your files and environment. The manager does not provide an OS sandbox and source inspection cannot guarantee the absence of malicious behavior.

## Publishing executable bridges

When the registry requires explicit source review for a native or JavaScript bridge, publish the pinned release manifest with:

```sh
forge publish module.json --acknowledge-review
```

The flag sends `acknowledge_review: true` in the registration request after local manifest validation. It acknowledges that the source was reviewed; it does not bypass the registry's pinned-repository verification or security policy, and it does not grant installation/build trust to consumers. Other extra publishing arguments are rejected.
