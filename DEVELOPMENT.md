# Development Workflow

Syzygy uses specification-driven development (SDD). A feature is not ready for implementation until its observable behavior, constraints, and acceptance criteria are understood.

## Required sequence

1. **Frame the change** — State the user problem, intended outcome, non-goals, and compatibility constraints.
2. **Specify** — Add or update requirements and uniquely identified acceptance criteria in SPEC.md (or a focused feature spec under docs/ for larger subsystems).
3. **Design** — Record affected components, interfaces, data/control flow, failure behavior, security boundaries, and alternatives in DESIGN.md.
4. **Plan** — Break the work into small ordered tasks in TASKS.md. Link each task to requirements and tests.
5. **Review the plan** — Resolve ambiguity and get approval before substantial or destructive implementation.
6. **Implement** — Keep commits focused. Do not mix unrelated refactors with feature work.
7. **Verify** — Add tests before or with implementation. Run focused tests, full CI, package checks, and hardware tests where applicable.
8. **Reconcile** — Update spec, design, task status, testing evidence, and CHANGELOG.md in the same change set.
9. **Report** — State what changed, what passed, what remains unverified, and the exact commit/PR.

## Traceability

Every acceptance criterion must map to:
- One or more implementation tasks.
- At least one verification method.
- A recorded result before it can be marked complete.

Use stable IDs such as FR-01 and AC-01. Tests should mention the requirement ID where practical, or the traceability table should map the test name to the criterion.

## Change rules

- Do not infer missing requirements from implementation convenience.
- Do not silently remove, weaken, or change existing behavior.
- Do not change public CLI/configuration semantics without documenting migration and compatibility.
- Do not perform broad rewrites when a narrow extension preserves established patterns.
- Do not claim runtime success from compilation or CI alone.
- Treat software-rendered CI and real GPU/compositor validation as separate evidence.
- Treat security boundaries, cleanup ownership, and failure paths as first-class requirements.
- Ask before destructive changes, credential/key migration, force-pushes, merging, or other irreversible actions.

## Definition of done

A task is done only when:
- Its acceptance criteria are satisfied.
- Automated tests pass.
- Relevant regression tests pass.
- Documentation matches actual behavior.
- Known limitations and deviations are recorded.
- Required hardware acceptance is complete, or the feature is explicitly labelled as awaiting hardware validation.

## Repository artifacts

- SPEC.md — requirements and acceptance criteria.
- DESIGN.md — architecture, interfaces, invariants, and design decisions.
- TASKS.md — ordered implementation and review checklist.
- TESTING.md — verification matrix and results.
- CHANGELOG.md — user-visible and process changes.
