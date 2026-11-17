# Workflow storage finish review

disposition: fix

Scope: fresh, read-only review of DEC-151 / M15-22 capability-driven retention copy and Save acknowledgement in the existing native workflow editor. This is not a full M15 certification or a database implementation audit.

## persistence

The retained product claim is appropriately limited to **saved workflows** recovering after service restart. `workflow_editor_model.hpp` reads `durable_catalog` from the service catalog; the service advertises it only when its workflow store exists. The default configuration enables the store, restores the catalog before exposing IPC, and fails startup on restore failure. The IPC decoder treats an omitted field as false, preserving compatibility with earlier services. Durable Save acknowledgement says a draft is saved but still requires validation/publication before running; the non-durable acknowledgement says it is synchronized to the current service. Those distinctions agree with DEC-151.

One material state gap remains: `apps/native/workflow_canvas.hpp:146` currently selects its retention statement using only `editor.durable`. Both initial and invalidated editor state set `durable=false`, so an unconfirmed service is asserted to have a non-persistent catalog. The packet promises the distinct “持久化状态待服务确认。” state, but that copy is absent from the source reviewed. This is a truthfulness gap in the capability message, not a reason to broaden the persistence design.

The screenshots establish the displayed copy and layout only. Backend source and the packet's functional test evidence support the storage claim; this review did not rerun tests, evaluate sanitizer completion, certify Windows, or prove live model acceptance. It makes no claim that Run IDs, model attempts, unsaved local edits, active tasks, or automatic desktop actions recover.

## fidelity

Independently opened every required PNG and checked all five dimensions and SHA256 values against `captures.json`; all matched. All captures show a complete native renderer state with the advertised scene and viewport, rather than blank, unfinished, or mismatched evidence.

The four durable captures cover 1180×800 and 860×620 in both light and dark themes. Their retention sentence remains legible and within the available width at 860; it does not collide with the title, controls, graph, or inspector. “有未保存的修改” remains visible alongside the qualified saved-workflow claim, so the depicted local draft is not silently described as saved. Both themes retain the existing neutral hierarchy and pink graph selection. At the small size the graph and inspector use their existing independent scroll areas; content below the visible region is not evidence of a new layout defect.

The 860×620 legacy light capture correctly retains “当前目录不跨服务重启保留。” and the independent called-workflow node with “调用 · 最新” and “进入”. Existing rail, cards, controls, typography, spacing, and graph geometry are preserved. No unrelated redesign is needed. There is no approved visual comp; this is a code-led extension of the incumbent native system. A web HTML/CSS detector is inapplicable and was not run.

## ceiling

The bounded copy change is visually ready within the supplied size/theme matrix. The remaining issue is one unconfirmed-capability state, not general visual ambition, graph architecture, or a need for a new workflow IDE. Do not turn this review into a new whole-M15 acceptance exercise. Documentation synchronization remains the subsequent documenter/parent task noted in the packet.

## material_fixes

| ID | Severity | Required result | Verification |
| --- | --- | --- | --- |
| F1 | Material | Distinguish capability not yet confirmed (including after connection invalidation) from a confirmed service without durable catalog support. Show the promised pending-confirmation wording until the capability is known; retain durable and legacy wording after confirmation. | Inspect the state selection and its initial/invalidation behavior. Recapture the same five required views, and include a focused unknown-capability render or equivalent rendered-state assertion so the new state is demonstrated. Confirm that both existing capability branches remain unchanged. |

No other material fixes are owed by this review. A fix verdict pass should score F1 only, using updated source and valid capture evidence.

## keep

Keep the precise “已保存的工作流” qualifier, the separate Save versus validation/publication language, the conservative legacy-service message, and the independent workflow call semantics. Keep the inherited native light/dark system, small-window scroll behavior, and existing graph/editor composition. Keep recovery limited to definitions and published call entry points; do not advertise Run restoration or automatic execution.
