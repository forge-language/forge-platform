# Forge roadmap

Safe ownership. Massive concurrency. Native speed.
AI builds. AI tests. AI reviews. Humans decide.

This translates the Project, Branding & Community Strategy into deliverables. It does not claim the language's goals are complete. No dates or unsupported capacity numbers are promised.

| Phase | Available foundation | Next acceptance criteria |
|---|---|---|
| 1 Foundation | Compiler, runtime, examples, architecture/current spec, contribution guidance | Specify typed AST/diagnostics; define ownership, aliases and lifetimes through RFC; add accepted/rejected programs |
| 2 Proof | Compiler and actual API reports with raw results and commands | Spawn, message passing, context switch, TCP echo and allocation suites with equal workloads and repeatable hardware/toolchain manifests |
| 3 Experience | Native CLI, verified Linux installer, first-project tutorial, browser Playground subset | Formatter/test/bench UX RFC; real workload stability; accessible diagnostics; broader distribution |
| 4 Community | RFC process, conduct, 12 first-issue drafts, GitHub participation path | Maintainers publish/triage 10–20 scoped first issues; create Discord and verify invitation; contributor recognition with consent |
| 5 Ecosystem | PostgreSQL/Web/Browser modules, pinned package manager and registry | Verify package ownership/publishing with GitHub OAuth, expand test/CLI/crypto/network modules, migration/version policy |
| 6 Public launch | Strategy-aligned site and Cloudflare Tunnel deployment configuration | Verify forge-lang.org DNS + HTTPS + install flow; configure OAuth; complete launch checklist and human decision before announcements |

## Priority technical work

1. Correctness before safety claims: complete type rules, ownership transfer/aliasing/lifetimes, memory and scheduler tests.
2. Native concurrency: mailbox lifetime, cooperative fairness, worker synchronization and automatic supervisor recovery.
3. Measurements: workload parity, isolated resources, longer runs and raw observations; do not extrapolate a cached HTTP path to language-wide superiority.
4. Developer experience: document existing forge init / pkg add / build / run; discuss new / add / test / fmt / bench in RFC rather than advertising unsupported commands.
5. Registry: immutable releases, verified GitHub login ownership, clean install/build tests and backups.
6. Agent transparency: link real PRs and CI; add opt-in metadata before reporting agent-assisted totals or live execution status.

## Website and domain plan

| URL | Purpose | Current scope |
|---|---|---|
| forge-lang.org / | Explain goals and Learn / Try / Watch / Contribute | Current implementation and limits stay visible |
| /language, /docs | Language and 5-minute start | Real syntax and Linux distribution |
| /play | Run without installation | Real WASM compiler → JS; native concurrency unsupported |
| /benchmarks, /blog | Evidence and development reports | Environment, command, source and raw data |
| /agents | Public development | Actual GitHub PR snapshots, not fabricated agent status |
| /packages, /publish | Module ecosystem | Anonymous reads; publishing needs OAuth |
| /community, /contribute, /roadmap | Participation | Real GitHub links; no invented Discord invitation |
| packages.forge-lang.org, play.forge-lang.org | Optional future aliases | Main-domain routes work first; add DNS when wanted |

## Public launch gate

- Verify compiler, bootstrap fixed point, registry, browser and installer checks.
- Verify fresh installation and first execution from the public HTTPS origin.
- Keep all benchmark conditions/limits available and examples runnable.
- Configure GitHub OAuth callback at https://forge-lang.org/api/auth/github/callback.
- Publish small first issues and an RFC entry path.
- Verify community contact/invitations and conduct moderation responsibility.
- Human maintainers decide release, merge and public announcement scope.
- Prepare GeekNews/Show HN/DEV copy from current capabilities; do not send announcements automatically.
