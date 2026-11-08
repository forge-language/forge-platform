# Forge Platform site performance verification — 2026-10-06

Measured the isolated deployment at `http://127.0.0.1:18103` with the real
Nginx/API stack. The comparison reads immutable Docker image contents; HTTP and
browser evidence was collected from the new test deployment. Commands used Python
`urllib` with `Accept-Encoding: gzip`, `docker run --entrypoint cat`, and Chromium
through Playwright. Raw evidence: [site-performance-2026-10-06.json](site-performance-2026-10-06.json).

## Bundle comparison

| Asset | Previous image | New image | Change |
| --- | ---: | ---: | ---: |
| Initial JavaScript | 261,656 bytes | 252,609 bytes | 3.46% smaller |
| Stylesheet | 189,003 bytes | 189,833 bytes | 0.44% larger |

Previous: `forge-platform-site:rollback-storage-20261006`.
New: `forge-platform-site:storage-test`.

Playground, viewer, syntax documentation and site pages now load through separate
JavaScript chunks. An initial homepage visit made **zero Playground or compiler
asset requests**. Navigating to Playground then requested its JavaScript chunk,
runner HTML and runner JavaScript. The compiler remains loaded on demand when code
is compiled. The report does not claim a homepage latency reduction from timing a
single local visit. Styles grew slightly with the added interfaces; they were not
reduced by this change.

## Actual HTTP compression and caching

| Asset | Uncompressed bytes | Gzip response body bytes | Reduction |
| --- | ---: | ---: | ---: |
| Initial JavaScript | 252,609 | 80,477 | 68.14% |
| Stylesheet | 189,833 | 57,006 | 69.97% |
| Homepage HTML | 1,009 | 526 | 47.87% |

All these responses included `Content-Encoding: gzip` and
`Vary: Accept-Encoding`. Versioned assets use
`Cache-Control: public,max-age=31536000,immutable`. Homepage HTML and the
repository-backed `activity.json`, `benchmarks.json` and materials use `no-cache`
so browsers revalidate current information. Freshness timestamps distinguish a
successful update from a failed repository poll. The benchmark and material routes
returned real files with HTTP 200 and the standard content security policy.

The compressed sizes are response body bytes, excluding transport headers. They
are not Internet bandwidth or Cloudflare latency measurements. No old deployment
HTTP response was measured, so the table does not imply a before/after network
comparison.

## Functional verification

All **8 Playwright tests passed in 17.0 seconds** against the same Nginx fixture:
installation and real package search, compiler Playground including UTF-8/int64,
syntax errors and timeout recovery, iframe isolation, mobile navigation without
horizontal overflow, anonymous publishing, persistent English/Korean selection,
syntax documentation, repository evidence, and safe Markdown/JSON viewing.

The mobile test covers twelve routes including registry search and its publishing
button, `/docs/syntax` and `/view` at 390px viewport width. The viewer rejects
traversal paths, displays raw HTML as text, blocks unsafe links and limits documents
to 1 MiB. GitHub snapshot outage retention was verified separately: failed polls
retain prior PRs/reports and their last successful timestamp.
