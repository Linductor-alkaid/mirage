# Workflow durability capability finish packet

User: “提交，继续”. Previous graph/call increment is committed as 26899ba. This continuation
implements DEC-151 / M15-22: default-on durable workflow catalog, separate editing head and latest
publication, checked call DAG restore, no automatic desktop/Strict Run replay. Existing independent
ID calls continue to resolve latest published targets. Startup revalidates using Mira DryRun.

This is a narrow code-led Operate extension of apps/native/DESIGN.md, not a visual redesign. The
ONLY product UI change is capability-driven retention copy and Save acknowledgement. New service
advertises durable_catalog; false/absent preserves current-service-only warning, unknown service
capability says status awaits service confirmation. Qualified wording “已保存的工作流可在服务重启后
恢复” must not imply that unsaved local edits, Run IDs, model attempts or active tasks are restored.
Palette/fonts/layout/controls stay inherited. No approved comp, web detector or new raster asset.

All 6 PNGs listed in captures.json are REQUIRED and root-inspected: four durable-capability scenes
1180x800 / 860x620, light/dark; one 860px legacy capability scene and one unknown-capability scene. Real product EUI/OpenGL renderer,
isolated Xvfb, shipped fonts/assets, synthetic state. No user desktop action or live model.
Current native renderer build/m15-catalog-final-render.log: 41426 checks, 0 failures. Debug functional
build/m15-catalog-accept-test.log: 10/10, including persistence/restart/update/cycle/delete/import/
failed writes and startup zero FileProvider actions; ASAN/UBSAN 11/11, TSAN10/10 passed; final copy affected ASAN/UBSAN5/5 and TSAN4/4 passed. Screenshots prove
copy/layout only. Runtime tests/source establish persistence. Windows remains unrun.

Project /home/linductor/.codex/worktrees/supervised-rpa-experience/mirage.
Inspect apps/native/workflow_canvas.hpp, workflow_editor_model.hpp, runtime/ipc protocol bool,
runtime/service persistence capability and docs/decisions/DEC-151-durable-workflow-catalog.md.
Read PRODUCT.md / apps/native/DESIGN.md for incumbent system. Backend source and pending docs
are outside visual redesign scope. docs sync will follow the review.

Fresh independent finish review ONLY; no implementation edits, app/model/network execution or
delegation. Read all six images and relevant copy/capability source. Write finish-review.md here
with ship/fix/recapture/rebuild, five sections persistence/fidelity/ceiling/material_fixes/keep.
Check truthful durable-vs-legacy/unknown states, copy fit/contrast and preserve unrelated graph
styling. No new whole-M15, Windows or live-online acceptance claim. Do not expand into a new graph
IDE or full database audit. Return concrete material findings if any.

F1 correction: independent capability_confirmed, initially false and cleared on invalidate; set only after accepted atom response, independent of safe_workflow_editor. Initial and confirmed unsupported/durable/invalidation states covered in native_workflow_editor_test. Updated all five original captures plus sixth unknown capture from final renderer. Final source guard is !editor.capability_confirmed. Request verdict pass scoring F1 only, no new redesign.
