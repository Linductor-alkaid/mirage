# Workflow graph and calls finish packet

User asks for a chat/workflow switch at the top-left sidebar, a clickable state-machine graph,
node conditions and atomic-task editing, reusable workflow segments, and navigating called workflows.
Their explicit correction overrides the initial design: workflows are independent, connected by calls;
use the latest published callee at dispatch, reject direct/indirect call cycles. No parent ownership or
fixed-version call semantics. Local ZCode was clean and fast-forwarded to fec97590 (v3.18.2);
reference its workflow-timeline station/rail/loop language and title switch, preserve Mirage's system.

## Direction / quality bar

Code-led Operate extension of apps/native/DESIGN.md. Native EUI C++, neutral light/dark, shipped
Noto Sans SC and Phosphor, inherited palette and controls. No approved comp or new brand. Sidebar
workspace switch, asset list, independent graph/inspector scroll areas; 1180x800 and 860x620, collapsed
sidebar supported. User must see graph selection, edit conditions/atomic arguments, directly enter
called workflows, and reuse a contiguous segment. Existing Run/recovery controls remain reachable.
State graph is Mira's sequential IR with bounded Control loop and fallback edges, not an invented
parallel scheduler. Status has words. Inputs, disabled states, keyboard activation, stale/failed writes
and draft preservation matter. Dark/light text contrast must hold also on selected/hovered rows.

Service calls are dynamic by workflow ID with cycle checks and explicit version-checked writes.
`safe_workflow_editor` capability gates old services read-only. Current catalog is service-local;
no cross-restart catalog persistence claim. Default learning metadata store is independently durable.
No Windows/live desktop/online-model quality/full-M15 completion claim. See DEC-150 and scoped
compatibility evidence. Existing UI defaults and unrelated chat are outside redesign scope.

## Evidence

All 26 PNGs in captures.json are required. These are actual EUI/OpenGL product renderings on
isolated Xvfb, synthetic states, settled frames, shipped fonts/icons. 14 Run scenes cover waiting,
completed, pending, disconnected, errors, empty, catalog scroll, both sizes/themes/sidebar states.
12 new scenes cover graph and selected call at both sizes/themes, two collapsed graphs, local
argument error and bounded-loop inspector. No raster imagery was generated. Source is authoritative
for interaction and runtime semantics; screenshots do not prove real desktop actions.

Builder ran one visual inspection and one combined correction/confirmation. Final confirmation
renderer: 40637 checks, 0 failures; build/m15-workflow-graph-confirm-capture.log. Initial failures
were an obsolete header-button selector and missing product input inset. Functional call/update/
cycle/concurrency/cancel suites passed in Debug/ASAN/UBSAN/TSAN. Latest full matrix failed ONLY
an outdated editor-test stale assertion; its expectation has now been corrected and is being rebuilt.
Known observations at confirmation that require independent assessment: the loop scene shows an
upstream default placeholder in an empty recovery input; an unselected sidebar asset appears white
in dark mode. Do not treat a passing geometry test as visual sign-off.

## Review handoff

Project: /home/linductor/.codex/worktrees/supervised-rpa-experience/mirage
Artifact: apps/native/app.cpp, workflow_canvas.hpp, workflow_editor_model.hpp, workflow_model.hpp.
Contracts: integration/mira/include/mirage/integration/workflow_document.hpp and implementation;
runtime/mira_host/src/mira_host.cpp; runtime/ipc and runtime/service edit/call handlers.
Tests: native_workflow_editor_test, workflow_composition_service_test,
native_workflow_integration_test, native_conversation_view_test.
Context: PRODUCT.md, apps/native/DESIGN.md, docs/design/native-agent-frontend.md,
docs/decisions/DEC-150-workflow-graph-composition.md,
docs/compatibility/workflow-graph-calls-20261116.md.
Craft floor: /home/linductor/.agents/skills/impeccable/reference/craft-floor.md.

Fresh independent finish review only, no code edits or network/model/UI execution. Inspect every
required capture and relevant source. Write finish-review.md here with ship/fix/recapture/rebuild,
evidence validity/coverage, concrete material findings and fix list, separating inherited constraints
from regressions. Native C++ is outside the web detector scope; no HTML detector/comp diff exists.

## F1–F5 correction / verdict packet

All five named fixes were applied as one batch, without redesign:
F1 explicit transparent idle/disabled surfaces; selected asset status uses foreground.
F2 empty or intentional Chinese placeholders on every new input.
F3 metadata fit width AND frame end 8px before the call button; compact wording at minimum width.
F4 current-service retention notice is appended to all canvas descriptions/acknowledgements before
editing, including direct publish; durable learning remains a separate path.
F5 markers reset on document changes, opening, creating, discarding, returning and invalidation;
selecting the end node within the same document preserves the marker. Added navigation regression
cases, call metadata/button geometry and retention checks.

Same 26 PNG paths were recaptured; captures.json regenerated and dimensions/hash verified.
Correction renderer: **40657 checks, 0 failures**, build/m15-workflow-graph-review-capture.log.
Debug functional **8/8**; ASAN/UBSAN functional+native renderer **9/9** each; TSAN functional
**8/8** with setarch, no GPU TSAN. Logs build/m15-workflow-graph-review-*-test.log.
Format and public-header boundary checks passed in build/m15-workflow-graph-review-build.log.

Verdict pass only: score F1–F5 resolved/partial/unresolved against the recaptured files and source.
Write finish-verdict.md here. Do not reopen unrelated design work or treat this scoped score as a
new whole-product review. No implementation edits. Documentation handoff follows the verdict.

### Verdict round 2: remaining F1 hover

Round 1 is preserved as finish-verdict-round1.md (F2–F5 resolved; F1 hover partial).
The sole remaining change uses p.text for asset status in every state, covering unselected light
hover too. Same 26 PNG files recaptured and manifest regenerated. Final native renderer remains
40657 checks, 0 failures; build/m15-workflow-graph-hover-capture.log. Format/boundary pass in
build/m15-workflow-graph-hover-build.log. This is a color-only change after the recorded sanitizer
matrix, so unrelated functional/sanitizer tests were not repeated. Score the remaining F1 issue;
carry unchanged resolved F2–F5 at their actual prior scope. Do not turn this into a whole-product audit.
