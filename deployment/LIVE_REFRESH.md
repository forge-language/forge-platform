# Public source and performance refresh

The source branches, deployed application image, historical evidence and current
measurements are distinct. Historical raw JSON is retained exactly; the refresh
process never executes fetched repository files or changes a production stack.

## Cadence and source selection

- `forge-benchmarks` measures bounded scheduler, string and parser workloads on
  `main` pushes, manual dispatch and a 15-minute schedule. It checks out the
  compiler, runtime and standard library `main` references and records their
  exact resolved commit SHAs before building. This job is never run with write
  access on pull requests. Every successful run uploads raw observations as a
  90-day artifact and appends them to the public `benchmark-data` branch.
- The platform public-data workflow refreshes on relevant `main` pushes, manual
  dispatch and a five-minute schedule. It uses the standard workflow token to
  write only this repository's `public-data` branch. Cross-repository dispatch
  and a new personal token are not required.
- The optional host user timer checks every 60 seconds. Raw benchmark branch
  changes can be observed on that cadence. Public GitHub organization metadata
  is cached for five minutes and PRs for ten minutes; the unauthenticated API
  budget is bounded at 40 requests/hour. GitHub schedules can be delayed. This
  is periodic refresh, not an instantaneous update guarantee.
- The frontend checks manifest/activity every 60 seconds and retains the last
  successful display on temporary failures. It shows measurement and update
  timestamps separately. CI hardware is shared and can vary between runs;
  these numbers describe fixed API/parser workloads, not full application RPS.

Public metadata includes every repository whose owner is exactly `forge-language`, including newly added repositories such as `forge-storage`. Document/code content fetching remains restricted to the fixed canonical source allowlist. Metadata from private repositories is ignored. Only fixed
`api.github.com` and `raw.githubusercontent.com` HTTPS paths are accepted;
redirects are rejected. Optional `GH_TOKEN` is sent only to the API host and is
never serialized into public data or logs.

## Data format and atomic publication

`scripts/refresh-public-data.py` obtains public repository metadata and last
compiler PRs, fetches documentation at immutable compiler/benchmark commit
SHAs, and validates measured `benchmark-data/latest.json`. Source documents are
bounded by file count and byte size. Each content group is updated only after
all files pass validation; failures retain the complete prior group.

`public-data/current` is atomically switched to an immutable
`snapshots/<content_id>/` directory. The manifest lists original document/report
hashes, exact source commits and immutable URLs. An unchanged content hash does
not create another tree or redownload documents. `refresh-status.json` records
the latest check time and bounded errors; `activity.json` retains the existing
public activity endpoint. Public directories use mode 0755 and data files 0644
so Nginx UID 101 can read them; the internal cache stays 0600. Previous snapshots
are kept for up to seven days or 64 trees, whichever limit is reached first; the
current tree is always protected. The refresh lock prevents overlapping local runs.

Nginx routes use the existing read-only `/srv/activity` mount:

- `/live/manifest.json` → `current/manifest.json`
- `/live/benchmarks/latest.json` → `current/benchmarks/latest.json`
- `/live/refresh-status.json` → `refresh-status.json`
- `/live/snapshots/` → `snapshots/`

Do not expose the whole `public-data` root; its hidden cache and lock are internal.
Legacy `/benchmarks.json` and `/materials/` viewer endpoints are generated from the same validated snapshot. Material records use immutable source commits; public report content fetching is restricted to the compiler, benchmark, platform and storage repositories. Reviewed local reports absent from those repositories remain explicitly identified as historical snapshots.

Existing fallback documents under `content/` are copied for offline development
by `scripts/sync-content.py`. Initial missing benchmark data is reported as
unavailable, not as a measured success.

## Review and installation

Generate into a disposable directory first:

```sh
python3 tests/public-refresh.py
python3 scripts/refresh-public-data.py --offline --output /tmp/forge-public-offline
python3 scripts/refresh-public-data.py --output /tmp/forge-public-live
```

After reviewing the actual output and application tests, the explicitly invoked
installer configures only a user timer:

```sh
sh scripts/install-refresh-timer.sh
systemctl --user status forge-public-refresh.timer
```

Templates are in `deployment/systemd/`. Installation does not pull/reset the
working repository, rebuild/restart containers, alter credentials or replace
registry data. An older activity timer should be disabled separately after the
new combined timer is verified, avoiding duplicate refresh requests.

A site code change still needs its own reviewed image deployment. Refreshing
read-only content does not restart the website. The new workflow files must be
published on their repositories' default branches before GitHub can schedule
or manually dispatch them.
