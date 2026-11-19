# Forge RFC process

Use an RFC for changes to syntax, types, ownership, scheduling, safety guarantees, ABI, module policy or developer commands.

1. Search existing issues and the roadmap. Open an issue with the RFC template.
2. State the problem and current behavior. Include an executable example where possible.
3. Describe the proposal, alternatives, rejected approaches, compatibility/migration and runtime costs.
4. State a validation plan: accepted/rejected compiler programs, runtime regressions, failure cases and representative measurements.
5. Discuss with maintainers before undertaking a large implementation. An agent may help draft, implement and review, but a human maintainer records the decision.
6. After acceptance, implement in scoped PRs and link evidence. If new findings change the design, update the RFC.
7. Merge requires human maintainer review under repository policy. Agents do not approve and merge each other's safety changes automatically.

Suggested status text: Draft → Discussing → Accepted / Rejected / Deferred → Implemented. These are documentation conventions, not claimed repository automation.

An RFC should name an owner, unanswered questions and evidence needed for acceptance. Record failed experiments as well as successful ones.
