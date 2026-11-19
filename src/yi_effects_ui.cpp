#include "imgui.h"
#include <cstdio>
extern "C" int YiEffectsLauncherMode(void);
extern "C" void YiEffectsLauncherSetMode(int);
extern "C" const char *YiEffectsLauncherHotkey(void);
extern "C" int YiEffectsLauncherSetHotkey(const char *);
extern "C" void YiEffectsDrawLauncher(const char *) {
  ImGui::Spacing();
  ImGui::Separator();
  bool effects_enabled = YiEffectsLauncherMode() != 0;
  if (ImGui::Checkbox("Display effects", &effects_enabled))
    YiEffectsLauncherSetMode(effects_enabled ? 4 : 0);
  const int effect_ids[] = {0, 4, 1, 2, 3};
  int effect_mode = 0;
  for (int i = 0; i < 5; ++i)
    if (effect_ids[i] == YiEffectsLauncherMode())
      effect_mode = i;
  ImGui::SetNextItemWidth(150);
  if (ImGui::Combo("##display-effect", &effect_mode,
                   "Original\0Sharp\0Scanlines\0CRT\0LCD grid\0"))
    YiEffectsLauncherSetMode(effect_ids[effect_mode]);
  ImGui::SameLine();
  ImGui::TextUnformatted("Cycle hotkey:");
  ImGui::SameLine();
  static bool capture_effect_key = false, invalid_effect_key = false;
  char effect_key_label[128];
  snprintf(effect_key_label, sizeof(effect_key_label), "%s###effects-cycle-key",
           capture_effect_key ? "Press a key..." : YiEffectsLauncherHotkey());
  if (ImGui::Button(effect_key_label, ImVec2(110, 0))) {
    capture_effect_key = true;
    invalid_effect_key = false;
  }
  if (capture_effect_key) {
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END;
         ++key) {
      if (!ImGui::IsKeyPressed((ImGuiKey)key, false))
        continue;
      if (key == ImGuiKey_Escape) {
        capture_effect_key = false;
        break;
      }
      if (key >= ImGuiKey_MouseLeft && key <= ImGuiKey_MouseWheelY)
        continue;
      if (YiEffectsLauncherSetHotkey(ImGui::GetKeyName((ImGuiKey)key))) {
        capture_effect_key = false;
        invalid_effect_key = false;
      } else
        invalid_effect_key = true;
      break;
    }
  }
  ImGui::TextWrapped(
      "Cycles Original / Sharp / Scanlines / CRT / LCD grid during play. Click "
      "the hotkey box to rebind; Esc cancels.");
  if (effect_mode == 1)
    ImGui::TextWrapped("Sharper pixel edges, original colors. Most visible "
                       "with Linear Filtering enabled.");
  if (invalid_effect_key)
    ImGui::TextWrapped("Key is reserved or assigned to game controls. Choose "
                       "another key, e.g. F2 or F7.");
}
