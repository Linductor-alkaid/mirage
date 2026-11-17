# Native Workflow controls finish packet

User request: “提交，继续推进”, continuing Mirage Workflow experience learning, visual memory,
Agent-supervised RPA. Existing increment was committed as 89fc251. Current increment M15-20
adds native operation entry to the existing desktop window. This is a local Operate extension,
not a redesign, new brand, asset editor or automatic recovery feature. Scope is frozen in DEC-149.

## Direction and quality bar

Inherit apps/native/DESIGN.md and current app.cpp: neutral light/dark, Noto Sans SC, existing
EUI controls, 800px content column, 1180×800 regular and 860×620 minimum desktop. Preserve
chat drafts/history and existing settings. One shared header entry remains reachable with the
sidebar hidden. No new imagery, decorative motion or visual identity.

First viewport: selected Run name/ID/state, distinct recovery-attempt state, explicit controls.
Catalog uses only acknowledged published/runnable versions and default parameters. Run state,
model attempt settlement and Lesson persistence must remain separate. A disabled operation must
look disabled. Long names may truncate visually while actual IDs remain unchanged. Scroll must
reach the catalog and learning/limit information. At small widths controls fit, text wraps, no
horizontal overflow; keyboard activation uses existing controls. Stale/failed/disconnected snapshots
stay visibly stale and cannot authorize mutations. Reads/controls are explicit through the existing
RuntimeBridge/Executor path; no private lifecycle or poller. M15-19 single-attempt/model-restart
restriction is preserved. No Windows, live desktop, online-model or full M15 closure claim.

## Required evidence

All 14 PNGs in captures.json are REQUIRED. Eight cover regular/minimum × light/dark × sidebar
expanded/collapsed waiting state; six additional minimum scenes cover completed, pending,
disconnected, actual rejected-control projection, scrolled catalog, empty. They are real OpenGL
product renderer captures using synthetic test state, settled animations and shipped fonts/assets.
Parent inspected all files once after one correction batch. No web detector ran: native EUI C++
is outside its HTML/CSS scope. No approved comp or comp-diff exists for this local code-led extension.

## Source and verification

Read apps/native/app.cpp, apps/native/workflow_model.hpp,
tests/desktop/native_workflow_model_test.cpp,
tests/runtime/native_workflow_integration_test.cpp,
tests/desktop/native_conversation_view_test.cpp,
docs/decisions/DEC-149-native-workflow-controls.md.
Runtime service contract: DEC-148 and runtime/service/src/workflow_recovery_control.cpp.
Current verification logs: build/m15-native-debug-test.log; build/m15-native-*-test-final.log;
build/m15-native-render-final-test.log; build/m15-native-capture-final.log. Native ASAN/UBSAN
renderer verification passed 3/3 each before the reviewer fixes; the reviewer fix builds are re-running.
Incumbent context: PRODUCT.md, apps/native/DESIGN.md, docs/design/native-agent-frontend.md.
Craft floor: /home/linductor/.agents/skills/impeccable/reference/craft-floor.md.

## Review boundary

Independent finish review only; no source edits, network requests, model calls or product launch.
Write the review in .impeccable/review/m15-workflow/finish-review.md. Return a disposition of
ship / fix / recapture / rebuild, an evidence/coverage assessment, material findings with exact
source/screenshot references and a concrete fix list if needed. Ignore pre-existing unrelated
visual/context drift; do not repair it. No review of unrelated chat redesign or speculative features.

## Reviewer fix return

The two original findings have one correction batch: row state uses warning/text mix and IDs use
text foreground across selection/press surfaces; rejected control notices now lead with required
refresh, with a projection regression. All 14 same captures are regenerated and rehashed.
Review only these fixes for the verdict; preserve the original full review. No other UI polishing.

## Post-verdict verification closure

Native ASAN and UBSAN renderer rebuilds after the two fixes have completed: each 3/3 CTest,
34215 renderer checks, zero failures and no sanitizer report. Debug renderer/capture is the same
34215-check final source. Projection/actual bridge affected tests after fixes are 4/4 in each of
Debug/ASAN/UBSAN/TSAN, 110/71 checks. The visual verdict remains scoped to its two findings.
