#pragma once

namespace mirage::native_ui {
// DEC-040 / EUI-20261005-007: the renderer scales the requested size by
// units_per_EM / (ascender - descender). Preserve design sizes in EM units
// for the pinned Noto Sans SC 2.004 face (1000, 1160, -288).
inline constexpr float ui_font_size(float em) { return em * 1.448f; }
inline constexpr float ui_input_line_height(float em) { return ui_font_size(em) * 1.2f; }
} // namespace mirage::native_ui
