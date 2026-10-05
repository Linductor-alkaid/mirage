disposition: fix
Inline substitution: finish-reviewer role performed in this turn using degraded instructions; no independent reviewer was spawned. Scope: user-authorized font replacement and conversation baseline only.

## persistence
pass — app.cpp loads shipped assets/NotoSansSC-Regular.otf; CMake verifies/extracts the complete OTF archive; Linux/Windows package lists include font and OFL. Typeface provenance is committed, not an external machine-only font.

## fidelity
- Typeface: adaptation, explicitly authorized by the user's font replacement request; Noto Sans SC covers Chinese/Latin/numerals in the provided native frames.
- Markdown ordinary/emphasis/link text: adaptation, EUI-20261005-006 common line boxes replace per-segment ink centering; mixed-normal-light/dark show aligned text, minimum captures preserve wrapping.
- Reading column, body16/24, composer, neutral light/dark palette, sidebar and Mira artwork: match incumbent contract.
- Input: match, mixed captures and editing-last-input retain readable text and controls.
- Code/independent weight faces: existing system monospace/regular-only resolution retained; no new guarantee asserted.

## ceiling
reached for this narrow typography correction; no new visual identity or motion requested. Existing link spacing and fallback-font limitations remain documented.

## material_fixes
1. Noto SC appears markedly smaller than the intended16EM, because EUI normalizes by ascender-descender; restore coherent EM sizes for rendering and measurement.
2. Reconcile the design record with actual EM conversion, input/heading line heights and the platform-dependent ZCode stack.
Reviewed initial Noto batch: mixed-normal-light/dark, mixed-minimum-light/dark and editing-last-input.

## keep
Keep the full official Chinese face, common text baseline, existing 16/24 rhythm, neutral palette and original Mira artwork. Windows/IME/high-DPI claims require target-platform evidence.
