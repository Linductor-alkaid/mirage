## verdict

1. **Persistence — resolved.** Source documentation now names native desktop and local-preview limits in PRODUCT.md; root DESIGN.md and its sidecar explicitly scope the old rules to legacy Web/CEF and route to the token-bearing apps/native/DESIGN.md and apps/native/.impeccable/design.json. The native record matches the recaptured neutral palette, typography, sidebar, composer, and 52px header.
2. **Protected modal interaction — resolved.** Source guards Ctrl+N and composer key/text input during modals, captures clear_target on opening, and confirms through clear_session(id). The recaptured clear dialog preserves the intended session, unsent message, and draft; the parent reports this capture followed Ctrl+N/Ctrl+Enter attempts. Source verification establishes the protection; a static capture alone cannot prove keystroke handling.
3. **Craft-floor eyebrow — resolved.** The same 1180, 860, collapsed, and maximized empty-state capture paths visibly contain no MIRAGE eyebrow. The title now leads, the reserved gap is removed, and suggestions/composer retain clear spacing.

## remaining

Clear. All eight required recaptures are valid. No regressions from this fix batch are visible. This ship verdict covers only the three scored fixes, not a new review of the whole surface or backend integration. Code and running UI were not mutated during this verdict pass.

disposition: ship
