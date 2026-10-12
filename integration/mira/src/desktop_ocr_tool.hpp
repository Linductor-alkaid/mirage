#pragma once
#include <mirage/integration/desktop_atom_toolset.hpp>
namespace mirage::integration::detail {
void register_ocr_tool(mira::BuiltinToolRegistry &, const DesktopToolContext &,
                       const desktop::CancelToken &, const DesktopAtomToolset::Decorator &);
}
