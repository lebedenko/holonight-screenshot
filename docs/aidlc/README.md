# AI-DLC workflow

Use this spec-driven development flow for feature, bug fix, and architecture work. Keep one Markdown record per change under `docs/aidlc/`, alongside the code in version control. Routine typo and formatting fixes can skip the record.

1. **Inception.** Copy the [change template](template.md) to `YYYY-MM-DD-short-name.md`. Capture the problem, scope, and observable acceptance criteria. Read the relevant code and tests before deciding the approach. Resolve scope or behavior decisions with the requester when their input is needed.
2. **Construction.** Set the status to `In progress` and outline the implementation in a short checklist. Update it as work lands and link the affected code and tests. If the implementation changes the agreed behavior, update the spec and record why.
3. **Verification and handoff.** Set the status to `Implemented` when the checklist is complete. Run `task test` for code changes and exercise relevant compositor behavior when acceptance depends on it. Record the commands or manual checks and their actual results. Set the status to `Verified` when all acceptance criteria have evidence. Record pending checks and blockers in the same file so the next contributor can resume.

Use `Draft` for a spec awaiting implementation. Keep long-lived behavior and design in the [capture design](../capture.md) and link to it from the change record. The [implementation baseline](baseline.md) tracks work shipped before this flow started.
