disposition: fix

## persistence

Pass for the review gate: PRODUCT.md and the native direction contract exist; DEC-035 identifies M6-05 and the exact ZCode source revision. This is a user-pinned, code-led native surface, so no comp approval, QUALITY BAR card, or random concept roll is owed. Native DESIGN.md / sidecar and the acceptance record still require the planned post-review documenter update; completion must not precede that update. Root DESIGN.md describes the retained legacy surface and is not this surface's authority.

All ten required captures were opened individually in the batched image read. Normal captures are 2360×1600 physical / 1180×800 logical at 2×; minimum captures are 1720×1240 / 860×620. They contain the named states and usable native content. Reply captures show a scrollable tail, not full history; differing scroll positions in minimum light/dark captures are valid. The long-input capture intentionally follows the caret to the end. No recapture is required for evidence validity.

## fidelity

Authority: ZCode source at 29628c9acdb81b703bbd4080c207a0e7ce5e276e, the explicit user reference, PRODUCT.md, native-agent-frontend.md, and DEC-035. Running Wayland ZCode refused ScreenshotWindow with AccessDenied; this verdict is source/layout comparison and native screenshot review, not a live ZCode pixel comparison. No HTML/CSS detector ran; native Linux GLFW conventions apply.

| Element | Verdict | Evidence |
| --- | --- | --- |
| TYPE | acceptable adaptation | Chinese system sans, 16px body/input, smaller toolbar, distinct heading scale and monospace code fit the documented native readability adaptation. ZCode defaults text-ui-base to 14px. Captures establish legible hierarchy; precise rendered bold-face fidelity is not independently established by these images. |
| MATERIAL | match | Neutral flat page, thin borders, light user surface, unboxed assistant text, rounded white/dark composer; no invented physical texture or decorative depth. Font Awesome is the native direction contract's explicit icon family adaptation. |
| GROUND | match | app.cpp names #f8f8f8 light and #161616 dark page fields, exactly the Zai Light/Dark source values; screenshot fields remain neutral without cream or slate drift. |
| Empty state | match | Centered greeting and bounded input, no unrelated suggestion grid; 672px cap and height-relative placement follow the referenced empty-layout structure. |
| Timeline and message roles | match | Reading column, right-aligned bounded user bubble, unboxed assistant Markdown and bottom composer preserve ZCode's ordering and core topology. Native uniformly rounded user corners are a minor geometry adaptation to the current rectangle primitive, not a topology replacement. |
| Composer | match | Text above the add/mode/model/context/send row, 16px shell radius, 12px input inset, multiline growth and bounded internal scrolling are visible. Minimum captures retain all toolbar controls. |
| Context truth | acceptable adaptation | The panel reports loaded turns, reference count and actual text bytes; absent service token usage is explicitly unavailable per DEC-035. It does not invent a percentage. |
| Reference inspection | missing | ZCode ConversationSelectionReferenceChip / ContextAttachmentPill expose reference.text and source in an inspectable popover. Native chips expose only the role label and removal; the context panel shows counts, leaving same-role messages indistinguishable. |
| Reference remove control | contradicted | Reference, context and long-input screenshots show the literal fallback text “Button” outside the chip, after its X. The icon-only remove builder in app.cpp omits an explicit empty text. |
| Product identity and retained settings | acceptable adaptation | Mira image, Mirage name, adjustable sidebar and real model entry are explicit product requirements; they must stay even where ZCode branding differs. |

Native interaction limits remain explicit: these screenshots cannot certify hover, focus order, real IME composition, Windows behavior, link activation, or fine text selection. DEC-035 records no Markdown selection/link callback in the current public component; whole-message copying and whole-message references are the supported interaction. Real-model Markdown evidence is accepted at the factual scope documented by the parent; no synthetic-screen or full-history claim is made.

## ceiling

The committed ZCode world needs quiet typography, inspectable context, compact controls and neutral material rather than novel decoration. Its meaningful unused device is the reference preview: the user must be able to see what context will be sent. No additional art, motion treatment, marketing framing or generic attractiveness pass is owed. The supplied native captures do not establish interactive-state craft beyond their depicted states.

## material_fixes

1. Missing reference inspection / context promise: add a bounded inspectable reference preview showing the selected text and source/role, with removal, so up to four same-role references can be distinguished before send. ZCode's count-pill/popover pattern is the reference; click and keyboard inspection are a valid native adaptation. Reuse UI-thread state and the current capacity limits.
2. Contradicted remove control / copy floor: explicitly clear the icon-only remove button's fallback text so “Button” cannot paint beyond the chip; confirm reference, context and long-input captures with the same files after rebuilding.

## keep

Keep the ZCode conversation topology, neutral light/dark grounds, unobstructed minimum-window composer, Mira identity, real model/harness controls, and honest token/interaction limitations while fixing the two reference-state defects.
