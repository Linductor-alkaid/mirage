## verdict

All ten original required captures were reopened at their exact original paths, plus native-zcode-reference-preview-light.png. All eleven are valid at the stated normal/minimum 2× viewports. The input capture depicts a multiline Chinese draft; reply captures depict actual conversation tails, and the long-input capture follows the caret. This is a verdict pass over the two original findings, not a renewed full-surface audit.

1. **Reference inspection — resolved.** The reference capture now shows a count pill, and the additional preview capture visibly opens a bounded popover above it. The popover shows the actual quoted Markdown text, “Mira · 消息 #6”, an individual trash control, and internal scrolling. The scoped app.cpp read confirms iteration over the existing bounded reference snapshots, source role/message ID, actual reference.text, and per-source removal. This restores ZCode's inspectable-context pattern through the accepted native click adaptation.
2. **Fallback “Button” text — resolved.** Reference, context, long-input and preview captures contain no default label spilling outside the pill or trash control. The scoped source confirms explicit empty text on icon-only copy, quote and preview-removal buttons. Normal/minimum light/dark reply captures retain clean message actions.

No visible fix-introduced regression was found in the supplied captures. Clipboard/removal execution and Debug/Release/ASAN results are parent-reported factual validation; this read-only reviewer did not rerun them. The source-only ZCode comparison and prior native interaction/platform limitations remain unchanged.

## remaining

clear — ship covers the two scored fixes only. The original full-review scope and its evidence limitations remain in native-zcode-finish-verdict.md; planned native DESIGN/sidecar and acceptance-document synchronization remain the parent's completion work.

disposition: ship
