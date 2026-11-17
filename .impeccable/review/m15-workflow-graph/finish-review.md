disposition: fix

Code-led native extension: no approved comp or comp-diff expected; the packet supplies the quality bar. Product documentation synchronization is still pending its documenter handoff. Review used source and supplied captures only; no product execution, network, detector, or implementation edits.

## persistence

Pass for the inherited design system and review evidence. PRODUCT.md and apps/native/DESIGN.md exist; the latter's Noto Sans SC, neutral #f8f8f8/#161616 grounds, restrained rose accent and native controls match the overall build. No replacement visual world or approval-dependent comp round is claimed. All **26 required PNGs** exist and their actual dimensions and SHA256 match captures.json. All 26 were visually inspected through the supplied contact sheets, with native-workflow-call-860-light.png and native-workflow-loop-1180-dark.png also opened individually. Captures show the named scenes, normal top chrome, valid viewport dimensions and actual content; scrolled catalog evidence is correctly named. These are valid synthetic renderer evidence, not real desktop execution or Windows proof. Source and compatibility records correctly keep full M15 and cross-restart catalog persistence open; do not close them from this review.

## fidelity

| Element / promise | Assessment | Evidence |
| --- | --- | --- |
| Sidebar workspace switch | Match | 会话 / 工作流 occupies the upper-left sidebar; graph and Run views share it. Collapsed chrome retains a return path. |
| State graph and inspection | Acceptable adaptation | Vertical station rail, node selection, conditional/verification labels and bounded loop trace are appropriate adaptations of the user's ZCode reference to the real sequential Mira IR and 860px minimum window. A parallel graph is not invented. |
| Independent workflow calls | Match at reviewed scope | Call nodes say 最新发布, provide a direct 进入 action and an inspector entry; source dispatch resolves the latest target by ID and checks call cycles before publication. No parent ownership is imposed. Runtime tests are reported separately from these images. |
| Reusable segment selection | Contradicted | Selection markers are global editor state rather than scoped to the loaded document, and survive document transitions; see F5. |
| TYPE | Match | Shipped Noto Sans SC and the inherited hierarchy are maintained, without a new display-face claim. |
| MATERIAL | Match | Crisp semantic geometry and native input/control surfaces suit the Operate brief; no physical material is faked. Repeated node rectangles represent actual graph steps rather than decorative feature cards. |
| GROUND and state contrast | Contradicted locally | The main light/dark grounds match DESIGN.md, but default-constructed colors produce opaque white unselected asset backgrounds in dark captures; selected light asset status also misses the contrast floor. |
| Minimum-window call action | Contradicted | native-workflow-call-860-light.png visibly combines the kind metadata and 进入 text. Their horizontal regions overlap in source; see F3. |
| Product copy and storage truth | Contradicted locally | An upstream placeholder appears in the loop inspector, and the actual service-local retention boundary is absent from the direct publish path; see F2/F4. |

## ceiling

Not reached for this Operate increment. The graph/inspector topology and typography can be retained; a rebuild is unnecessary. The remaining gap is state craft and task clarity: readable inactive assets, restrained disabled treatment, collision-free call metadata/actions, intentional empty input copy, visible retention scope and document-scoped segment selection. No decorative motion, imagery, node dragging, parameter designer or new brand is required to fix these findings. Atom picker, extraction and call-return behavior were reviewed in source; the supplied images do not constitute a visual walkthrough of every one of those interactions.

## material_fixes

1. **F1 — Repair native state backgrounds and asset contrast.** In `apps/native/workflow_canvas.hpp:68` and `:109`, replace `eui::Color{}` where transparency was intended with an explicit transparent or theme-appropriate color. In dark mode, the unselected asset is currently white with #eeeeee title text (**1.16:1**) and #a1a1a1 status (**2.58:1**), visible in native-workflow-loop-1180-dark.png and dark Run captures. Also fix the asset status at `:81`: #6d6d6d on selected #e2e2e2 is **3.99:1**, below 4.5:1. Verify selected/unselected/hover/disabled states in both themes; preserve the primary action distinction.
2. **F2 — Remove upstream placeholder copy from product fields.** The shared inspector input helper at `apps/native/workflow_canvas.hpp:392` never sets an intentional placeholder. Empty recovery shows `Hello EUI-NEO` plus an emoji in native-workflow-loop-1180-dark.png. Explicitly set empty or field-specific Chinese placeholders for the helper and other new inputs, including multiline arguments. An empty optional value must remain visibly optional, without implying a configured task or showing library demo copy.
3. **F3 — Reserve a non-overlapping region for direct call navigation.** At `apps/native/workflow_canvas.hpp:255–264`, metadata starts at x=58 and is fitted to w−132, ending at w−74, while the 52px 进入 button starts at w−108 on the same line: a 34px collision. Derive the metadata fit width **and text frame** from the actual button left edge minus a gap; keep 进入 distinct at 860×620 and with longer state metadata. Add a geometry check for the metadata/action separation and re-read the existing call captures.
4. **F4 — Expose the real retention boundary on the publish path.** `apps/native/workflow_editor_model.hpp:247–249` warns about cross-service restart loss only after Save, while direct 验证发布 reports reusable publication without that boundary; the initial graph description at `workflow_canvas.hpp:141–145` also omits it. Make the existing current-service-only catalog scope visible before authors rely on saving/publishing and in publication acknowledgement. This is a truthful-copy fix; implementing catalog persistence is outside this review's requested increment. Keep independent durable learning storage clearly separate.
5. **F5 — Scope segment markers to the current document.** `segment_marked`/`segment_first` (`apps/native/workflow_editor_model.hpp:21–22`) are cleared only after extract; open acknowledgement (`:200–216`), create (`:92–102`), discard (`:104–114`) and return navigation (`:161–174`) leave them intact. Marking a range in A and opening/creating B can leave 提取 enabled with A's index, yielding the wrong range or an invalid-range error. Clear or restore markers explicitly with document navigation; preserve them only while selecting the end node in the same document. Verify mark-A → open-B/new/discard/back and normal same-document range extraction.

## keep

Keep the upper-left workspace switch, real IR ownership, independent workflow calls with latest-target dispatch and cycle rejection, direct call navigation, bounded request revisions/CAS/old-service read-only behavior, preserved drafts, separate Run/recovery controls, inherited neutral themes and explicit Linux/synthetic evidence limits.
