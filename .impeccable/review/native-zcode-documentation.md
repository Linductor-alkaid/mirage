# Native ZCode documentation extraction

日期：2026-10-04。工作项 M6-05，决策 DEC-035；范围只包含 apps/native 的原生会话视觉系统及 PRODUCT.md 的对应当前事实。用户已授权整页参考 ZCode 并刷新合并最终原生文档；本次为 code-led / user-zcode-native / Operate 提取，不另设概念或批准阶段。

## Authority and scope

方向契约：`docs/design/native-agent-frontend.md`。产品上下文：`PRODUCT.md`。参考事实：DEC-035 与 `docs/compatibility/native-zcode-conversation-20261004.md`，ZCode main 29628c9acdb81b703bbd4080c207a0e7ce5e276e。遵循 impeccable `reference/document.md` 的 token-bearing DESIGN + schemaVersion 2 JSON sidecar。保留根目录 legacy DESIGN.md / .impeccable/design.json；未改根 README、计划、兼容记录、代码或素材。

原生 Linux GLFW/OpenGL，不运行 HTML detector，不套用 iOS/Android 条件。GNOME Wayland 的运行中 ZCode ScreenshotWindow 返回 AccessDenied，因此依据 ZCode 源码/布局和 Mirage Linux captures，不能宣称实时 ZCode 像素复刻。

## Extraction evidence

| Fact | Source |
| --- | --- |
| Light #f8f8f8 / dark #161616 page, neutral sidebar/surface/hover/selected/text/border/action roles | apps/native/app.cpp:21–33 Palette |
| Default text line height ×1.5, native icon line height equals size, no button shadow / press scale 1; icon-only fallback text cleared | app.cpp:200–245 |
| Empty centered max672; active max896 and 864px width policy; greeting30; editor48–168 + toolbar56 + refs36 | app.cpp:398–419 |
| Markdown16/26, headings22/20/18, code14, gap12/radius8; right user bubble max576/radius12/padding16×12; cardless assistant | app.cpp:422–469 |
| Whole-message copy/quote actions30×28; composer radius16, dark #2b2b2b, reference pill176×28 | app.cpp:473–507 |
| Send/newline and composition guard; real mode/model/context/send controls | app.cpp:509–549 |
| Reference popover width≤320/height≤352, bounded scroll text/source role/messageID/individual remove; honest raw bytes and unavailable tokens | app.cpp:550–623 |
| At most4 refs/8KiB, encoded submission≤16KiB, monotonic ref instance IDs and ACK protection | chat_model.hpp:45–72; chat_model.cpp:104–148 |
| Public EUI DSL CJK gap adapter; parser/wrapping/height budget retained | markdown_adapter.hpp, EUI-20261004-004 |
| Settings still use max800 column; sidebar/appearance/model values preserved | app.cpp:278–389, 645–769; window_controls.cpp |

Removed the obsolete native fixed160px composer, 96px input, 34px greeting, 18px conversation body/input and suggestion-list baseline. Any remaining 18px is the real Markdown H3 or helper line height; 800px describes settings only. Canonical frontmatter includes actual palette/typography/radius/spacing/component primitives. Sidecar stores native layout/behavior/limits/provenance and ten self-contained HTML/CSS panel translations; previews are not a second native implementation or evidence of IPC functionality.

The quote-count pill now opens actual text/source inspection, and icon-only remove/copy/quote/toolbar buttons clear default text. The initial finish verdict was `fix`; the later verdict pass scored both listed corrections resolved with `disposition: ship`, limited to those two corrections. No whole-surface pixel-equivalence claim is added.

## Validation

- `python3 -m json.tool apps/native/.impeccable/design.json`: passed.
- YAML frontmatter parsed; eight canonical headings retain order; component properties use the allowed schema keys and token references resolve: passed.
- Sidecar narrative copied from canonical DESIGN; ten preview components contain inline styles and no external icon package/assets: passed.
- Sidebar/model layout, service/lifecycle semantics, entry links and Mira asset provenance retained. No new/generated raster assets.
- Documentation-only scope does not warrant a duplicate build/test run; the parent task owns code verification, recaptures and fresh finish scoring.

## Existing drift and limits

Missing all system CJK candidates still triggers EUI's decorative fallback font; retain this as known drift, not a normative typeface. EUI conservative Markdown wrapping/height estimates remain; continuous-CJK gap correction is temporary and scoped to one public DSL Adapter. Fine text selection/link opening, Windows acceptance, real Chinese IME acceptance, live ZCode Wayland pixel comparison and Dock pixel display remain unverified or unsupported as already documented. No repair outside the assigned scope.

## 最终评分同步

主任务核对2026-10-04最终verdict：两项修正均resolved，disposition=ship，
只覆盖修正列表；未将源码对齐写成ZCode运行中像素比对。提取后代码未再变更。
