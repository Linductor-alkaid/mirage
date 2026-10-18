#pragma once
// The Bridge's only EUI call. Native builds also compile against EUI's real
// public declaration; this fixture substitutes the window wakeup boundary.
namespace app {
void requestUpdate();
}
