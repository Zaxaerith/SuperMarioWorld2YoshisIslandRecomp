#include "imgui.h"
extern "C" int YiWsLauncherEnabled(void);
extern "C" void YiWsLauncherSetEnabled(int enabled);
extern "C" void YiWsDrawLauncher(const char *path) {
  (void)path;
  ImGui::Separator();
  bool enabled=YiWsLauncherEnabled()!=0;
  if(ImGui::Checkbox("16:9 Widescreen",&enabled))YiWsLauncherSetEnabled(enabled);
  ImGui::TextDisabled("F8: compare with original 4:3. F9: save local graphics diagnostics.");
}
