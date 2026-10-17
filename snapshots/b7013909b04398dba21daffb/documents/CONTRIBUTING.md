# Contributing to Forge

Thank you for helping improve Forge. This project is open source and welcomes contributions from anyone — solo developers, teams, and AI-assisted workflows included.

Please read this guide before opening a pull request.

## Quick start

For the active compiler implementation, use
[forge-language/forge](https://github.com/forge-language/forge):

```sh
git clone https://github.com/forge-language/forge.git
cd forge
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Read the [architecture](ARCHITECTURE.md), [current language specification](LANGUAGE_SPEC.md),
[roadmap](ROADMAP.md), [RFC process](RFC_PROCESS.md) and [code of conduct](CODE_OF_CONDUCT.md).
The [first contribution candidates](GOOD_FIRST_ISSUES.md) are drafts for maintainers
to triage, not a promise that all are currently open GitHub issues.

AI agents may plan, implement, test and review. Human maintainers make the final
design and merge decisions. Safety-sensitive compiler/runtime/ownership changes
must include reproducible evidence and human review; agent approval alone is insufficient.

1. Fork [forge-language/forge](https://github.com/forge-language/forge)
2. Create a branch from `main`
3. Make your changes and verify them locally
4. Push and open a pull request against `main`
5. Respond to review feedback

For build instructions, see the [README](README.md).

## Pull request rules

### Scope

- **One logical change per PR.** Bug fix, feature, docs update, or refactor — not all at once.
- **Keep diffs reviewable.** Prefer several small PRs over one huge PR.
- **Do not mix unrelated reformatting** with functional changes unless the PR is explicitly for formatting.
- **Link related issues** with `Fixes #123` or `Refs #123` when applicable.

### Branch naming

Use a short prefix that describes the change:

| Prefix | Use for |
|--------|---------|
| `fix/` | Bug fixes |
| `feat/` | New features |
| `docs/` | Documentation only |
| `lean/` | Lean proofs |
| `ci/` | CI / GitHub Actions |
| `refactor/` | Code restructuring without behavior change |
| `chore/` | Tooling, deps, housekeeping |

Examples: `fix/optimizer-div-zero`, `lean/optimize-correctness`, `docs/match-example`

### Commit messages

Follow the style already used in this repository:

- Use the **imperative mood**: `Add`, `Fix`, `Prove`, `Update` — not `Added` or `Adds`
- **First line ≤ 72 characters**, focused on *why* when possible
- Optional body for non-obvious context

```
Add GitHub Actions CI for Lean proofs and Linux build.

Run lake build in forge-proofs/ and verify the C compiler builds on Ubuntu.
```

### Before you open a PR

| Check | Requirement |
|-------|-------------|
| CI | All GitHub Actions checks must pass |
| C changes | `cmake -B build && cmake --build build` succeeds on your machine |
| Lean changes | `cd ../forge-proofs && lake build` succeeds with **no `sorry`** |
| Docs | Update README or `docs/` when behavior or usage changes |
| Examples | Add or update `examples/` when introducing user-facing features |

### PR description

Use the [pull request template](.github/pull_request_template.md). At minimum include:

1. **What** changed
2. **Why** it was needed
3. **How** you verified it (commands run, platforms tested)

Draft PRs are welcome for early feedback.

### Review process

- At least **one approving review** from a maintainer is required before merge (when branch protection is enabled).
- Address review comments or explain why you disagree — discussion is expected.
- Maintainers may request changes, squash commits, or rebase before merge.
- If a PR is inactive for a long time, a maintainer may close it with a note; you can reopen when ready.

### What we look for in review

- Correctness and clarity over cleverness
- Consistency with existing C / Lean / docs style in the repo
- Minimal scope — no drive-by refactors
- Lean proofs must actually compile and close the stated goal
- No secrets, credentials, or generated build artifacts (`.lake/`, `build/`, etc.)

## AI-assisted contributions

**AI assistance is welcome.** Most contributors use tools like Cursor, Copilot, Claude, or ChatGPT — that is fine.

We only ask for good judgment and accountability:

| Do | Don't |
|----|-------|
| Review and understand every line you submit | Paste large unreviewed AI output |
| Run builds/tests locally before opening a PR | Submit code you cannot explain in review |
| Mention AI help in the PR when it was substantial | Hide that a PR was mostly machine-generated |
| Fix CI failures and review feedback yourself | Expect reviewers to debug AI mistakes for you |

For Lean proofs: AI-generated proofs are acceptable, but they must **`lake build` cleanly with no `sorry`** unless you explicitly discuss an incomplete proof in the PR.

## Repository areas

| Path | Notes |
|------|-------|
| `compiler/` | Compiler implementation and parser/codegen regressions |
| `examples/`, `docs/` | Compiler fixtures and language documentation |
| `bootstrap/` | Self-hosting compiler — verify the fixed point |
| `tests/` | Compiler and integrated SDK regressions |
| `cmake/` | Component pins and installed SDK build helpers |

Other component paths are listed in [repository layout](docs/repository-layout.md).

## Reporting bugs and requesting features

Use [GitHub Issues](https://github.com/forge-language/forge/issues) with the provided templates:

- **Bug report** — reproduction steps, expected vs actual behavior, environment
- **Feature request** — problem statement, proposed solution, alternatives considered

For security vulnerabilities, please do **not** open a public issue. Contact the maintainers privately.

## Code of conduct

Be respectful and constructive. Disagree on technical merits, not people. Maintainers may moderate or remove contributions that harm collaboration.

## For maintainers

Repository settings that keep `main` healthy for multi-contributor open source work. Requires **Admin** access on [forge-language/forge](https://github.com/forge-language/forge).

### Branch protection for `main`

1. Open **Settings → Branches → Add branch ruleset** (or *Add classic branch protection rule*)
2. Target branch: `main`
3. Enable:

| Setting | Recommended value |
|---------|-------------------|
| Require a pull request before merging | On |
| Required approvals | **1** |
| Dismiss stale pull request approvals when new commits are pushed | On |
| Require status checks to pass before merging | On |
| Status checks that are required | `Lean proofs`, `Build (Linux)` |
| Require branches to be up to date before merging | On (optional but recommended) |
| Do not allow bypassing the above settings | On for non-emergency repos |
| Restrict who can push to matching branches | On — limit to maintainers only |
| Allow force pushes | **Off** |
| Allow deletions | **Off** |

4. Save the ruleset.

After this, all changes reach `main` only through reviewed, CI-green PRs.

### Issue labels

Create these labels under **Issues → Labels** so templates work cleanly:

| Label | Color (suggested) | Use for |
|-------|-------------------|---------|
| `bug` | `#d73a4a` | Incorrect behavior |
| `enhancement` | `#a2eeef` | Features and improvements |
| `documentation` | `#0075ca` | Docs-only changes |
| `good first issue` | `#7057ff` | Small, newcomer-friendly tasks |
| `help wanted` | `#008672` | Maintainers welcome outside help |

### Merge strategy

- Prefer **Squash and merge** for contributor PRs (one commit per change on `main`)
- Use **Rebase and merge** only when commit history on the branch is already clean
- Avoid **Merge commit** unless a release branch needs preserved history

### CODEOWNERS (optional)

To auto-request review from specific people or teams, add `.github/CODEOWNERS`:

```
# Default reviewers for the whole repo
*       @forge-language/maintainers

# Area experts (replace with real handles)
/compiler/  @forge-language/maintainers
```

Teams must exist in the org and have repository access.

## License

By contributing, you agree that your contributions will be licensed under the [Apache License 2.0](LICENSE) that covers this project.

## Component repositories

See [repository layout](docs/repository-layout.md). Submit library, server, editor,
benchmark and proof changes to the repository that owns those files. Compiler
changes run the core build, CTest and self-host fixed-point check. Library updates
require an explicit pinned revision update in `cmake/Dependencies.cmake`.
