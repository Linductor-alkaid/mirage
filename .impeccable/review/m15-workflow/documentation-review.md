# Native Workflow documentation handoff

> Date: 2026-11-16
> Scope: M15-20 / DEC-149; ordinary local EUI desktop Operate extension
> Result: capability documentation synchronized; incumbent visual-system files preserved

## Scope and source checks

Read [PRODUCT](../../../PRODUCT.md), [native README](../../../apps/native/README.md),
[native frontend design](../../../docs/design/native-agent-frontend.md),
[DEC-148](../../../docs/decisions/DEC-148-service-workflow-recovery.md),
[DEC-149](../../../docs/decisions/DEC-149-native-workflow-controls.md),
[finish packet](finish-packet.md), [full review](finish-review.md),
[fix verdict](finish-verdict.md) and the current
[verification record](../../../docs/compatibility/native-workflow-controls-20261116.md).
Applied the supplied `impeccable/reference/document.md` ordinary-extension path after the final
correction. No new visual world or system change was requested or found in this feature.

Compared [app.cpp](../../../apps/native/app.cpp) and
[WorkflowModel](../../../apps/native/workflow_model.hpp) against
[native DESIGN](../../../apps/native/DESIGN.md), [root DESIGN](../../../DESIGN.md) and
[design sidecar](../../design.json). Checked the projection regression's refresh-first rejection,
matching request/response and Lesson gates in
[native_workflow_model_test.cpp](../../../tests/desktop/native_workflow_model_test.cpp), and the
RuntimeBridge/isolated RuntimeService fixture in
[native_workflow_integration_test.cpp](../../../tests/runtime/native_workflow_integration_test.cpp).
These were source checks, not a new runtime test execution by the documenter.

## Compliance comparison

| Incumbent contract | Finished extension and evidence |
| --- | --- |
| Neutral light/dark native Palette; Noto Sans SC; flat surfaces and existing EUI controls | Workflow page uses the same Palette, text/control helpers and scroll component; no new image, font, shadow, motion or token. Run-row contrast fix derives a local warning/text mix and uses existing text foreground for IDs. |
| Shared window, sidebar and bounded centered content | Header Workflow/return entry remains available with sidebar collapsed; inherited 800px column, 1180×800 regular and 860×620 minimum sizes; long-name truncation and wrapped controls appear in final captures. |
| Explicit service facts and actionable failures | Run, recovery attempt and Lesson remain separate; refresh-first notice matches stale-state disabling. Catalog/default-parameter execution, cancel and recovery start/status/cancel/Lesson reuse RuntimeBridge/Executor. |
| Bounded UI projection, visible lifecycle | At most128 catalog entries/256 Runs; one group of up to three reads or one control. Revision/type/target checks reject late or mismatched replies; events invalidate snapshots instead of authorizing automatic actions. |
| Existing service preview limits | Every Run has at most one recovery attempt; first recovery binding fixes model configuration until service restart. Cancellation still requires settlement; no automatic recovery, retry or poller is documented. |

Checked all 14 file hashes against [captures.json](captures.json). Independently opened these four
representative final captures for the documentation comparison:

- [Regular light waiting](native-workflow-waiting-1180-light.png): shared header, incumbent hierarchy and flat selected row.
- [Minimum dark/collapsed waiting](native-workflow-waiting-860-dark-collapsed.png): return entry, inherited theme and wrapped controls.
- [Minimum rejection](native-workflow-error-min-light.png): refresh-first guidance, stale label and disabled actions.
- [Minimum scrolled catalog](native-workflow-catalog-min-light.png): default-parameter action, learning capacity and restart restriction.

The full independent reviewer opened the complete 14-capture matrix. Its subsequent `ship` verdict
scores only two findings resolved: selected-row text contrast and refresh-first rejection guidance.
It is not a new whole-surface audit or platform certification. Captures show synthetic Linux
Xvfb/OpenGL product rendering; isolated service/script-provider evidence does not establish online
model, live-desktop, Windows or full M15 acceptance. No model, network or product launch was used
for this documentation handoff.

## Documentation changes and preservation

Updated only [PRODUCT](../../../PRODUCT.md), [native README](../../../apps/native/README.md),
[native frontend design](../../../docs/design/native-agent-frontend.md) and this report. Current
capability statements now distinguish the delivered explicit Workflow entry from future asset
review, automatic selection/recovery and complete M15 work. Existing dated history is preserved.

Native/root DESIGN, sidecar, style tokens and source assets remain unchanged. Pre-existing context
drift remains: older frontend direction text names Font Awesome while native DESIGN names
Phosphor; the sidecar still carries older narrative metadata. No unrelated context refresh or
system regeneration was performed. Plans, compatibility evidence, code and tests are owned by
the parent task and were not edited by this handoff.

Documentation validation: one H1 per edited Markdown file; new relative link targets exist;
`git diff --check` passes; preserved system files have no diff. Recheck this comparison if later
feature edits change its visual or behavioral contract.
