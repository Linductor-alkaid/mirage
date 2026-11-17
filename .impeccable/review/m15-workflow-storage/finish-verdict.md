# Workflow storage F1 verdict

disposition: ship

This verdict scores only F1 from `finish-review.md`. It is not a new whole-surface, persistence-database, M15, Windows, or live-model acceptance review.

## persistence

F1 is resolved. `WorkflowEditorModel::capability_confirmed` starts false, is cleared together with durability/edit support on invalidation, and becomes true only after an accepted atom-catalog response. It is independent of `safe_workflow_editor`, so a confirmed service without editing support is no longer confused with an unknown service. The canvas checks confirmation first, displaying “持久化状态待服务确认。” until capability is known, then selects the existing durable or service-only message.

The model test covers initial unknown, confirmed unsupported/non-durable, confirmed durable, and invalidation states. The renderer test asserts the unknown sentence. Source and packet evidence were inspected; this reviewer did not rerun the reported Debug/sanitizer/renderer tests.

## fidelity

Independently opened all six updated PNGs. Every file matches its manifest dimensions and SHA256. The original four durable captures and one legacy capture are byte-identical to the first review's valid images, preserving both previously reviewed capability branches and their layout.

The sixth capture, `native-workflow-storage-unknown-860-light.png`, visibly shows “正在读取服务能力。 持久化状态待服务确认。” at 860×620. The sentence fits, remains legible, and does not overlap the title, actions, graph, or inspector. The captured controls reflect unavailable editing capability. The evidence is complete for F1; no recapture is required.

## ceiling

F1 required a truthful capability-state distinction, which is now implemented and demonstrated. This verdict does not reopen visual design or broaden the original scope. Native conventions remain the reference; no web detector was used.

## material_fixes

| ID | Score | Evidence |
| --- | --- | --- |
| F1 | resolved | Independent confirmation state, initial/invalidation reset, accepted-response update, model assertions, rendered unknown sentence, and all six valid captures. |

The sole listed material fix is resolved. No further work is owed by this verdict pass.

## keep

Keep the independent capability-confirmation state, the qualified saved-workflow recovery promise, and the existing durable/legacy wording. Preserve the inherited native graph/editor system and the distinction between saved definitions and Run/task execution recovery.
