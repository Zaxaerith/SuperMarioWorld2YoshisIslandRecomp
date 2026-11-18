#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "yi_ra.h"
#include <windows.h>

#include "client/client.h"
#include "imgui.h"
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
std::unique_ptr<yira::Client> client;
bool enabled = true, overview = false, running = false;
unsigned client_initializations = 0;
/* Cache the exact SNES payload identity, invalidating on file/path changes.
 * Rehash at Play as well; launcher and command-line starts share the gate. */
bool rom_allowed(const char *path, bool force = false) {
  static std::string cached_path;
  static std::filesystem::file_time_type cached_time;
  static uintmax_t cached_size = 0;
  static bool allowed = false;
  std::string selected = path ? path : "";
  std::error_code error;
  auto file = std::filesystem::u8path(selected);
  auto time = std::filesystem::last_write_time(file, error);
  if (error) {
    cached_path.clear();
    allowed = false;
    return false;
  }
  auto size = std::filesystem::file_size(file, error);
  if (error) {
    cached_path.clear();
    allowed = false;
    return false;
  }
  if (force || selected != cached_path || time != cached_time ||
      size != cached_size) {
    std::string hash;
    allowed = yira::hash_rom(selected, hash) &&
              hash == "cb472164c5a71ccd3739963390ec6a50";
    cached_path = selected;
    cached_time = time;
    cached_size = size;
  }
  return allowed;
}
std::string preferences_path;
bool preferences_saved = true;
void save_preferences() {
  if (preferences_path.empty())
    return;
  auto temporary = preferences_path + ".tmp";
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  output << "[Mods]\nRetroAchievements=" << enabled << "\n";
  output.flush();
  bool ok = output.good();
  output.close();
  preferences_saved =
      ok && MoveFileExA(temporary.c_str(), preferences_path.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
const uint8_t *ram = nullptr;
const uint8_t *sram = nullptr;
unsigned sram_size = 0, overview_index = 0;
uint8_t preview_ram[0x20000]{}, preview_sram[0x10000]{};
char username[96]{}, password[256]{};
yira::Popup popup;
uint64_t popup_until = 0;
SDL_Texture *popup_texture = nullptr;
std::string texture_key;
uint32_t RC_CCONV read(uint32_t address, uint8_t *out, uint32_t bytes,
                       rc_client_t *) {
  // Before boot only address validation runs; no frame evaluation in launcher.
  return yira::read_memory(address, out, bytes, ram ? ram : preview_ram,
                           sram ? sram : preview_sram,
                           ram ? sram_size : sizeof(preview_sram));
}
class SoftcoreServices final : public yira::Services {
  std::unique_ptr<yira::Services> service;

public:
  explicit SoftcoreServices(
      std::unique_ptr<yira::Services> backend = yira::windows_services(
          L"YoshisIslandRecomp/RetroAchievements/Softcore"))
      : service(std::move(backend)) {}
  yira::Response http(const std::string &url,
                      const std::string &body) override {
    if (body.find("r=submitlbentry") != std::string::npos)
      return {403, "{\"Success\":false,\"Error\":\"Leaderboards disabled\"}"};
    return service->http(url, body);
  }
  bool restore(yira::Credential &c) override { return service->restore(c); }
  bool store(const yira::Credential &c) override { return service->store(c); }
  void forget() override { service->forget(); }
  void cancel() override { service->cancel(); }
};
void ensure() {
  if (client)
    return;
  ++client_initializations;
  client = std::make_unique<yira::Client>(std::make_unique<SoftcoreServices>(),
                                          read);
  client->set_observer(false);
  client->restore();
}
std::wstring wide(const std::string &text) {
  int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring out(n ? n : 1, L'\0');
  if (n)
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), n);
  return out;
}
// Host-only overlay: never changes the SNES framebuffer or guest memory.
SDL_Texture *make_texture(SDL_Renderer *renderer, const std::string &title,
                          const std::string &description) {
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = 900;
  info.bmiHeader.biHeight = -112;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  void *pixels = nullptr;
  HDC dc = CreateCompatibleDC(nullptr);
  HBITMAP bitmap =
      CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  if (!dc || !bitmap || !pixels) {
    if (bitmap)
      DeleteObject(bitmap);
    if (dc)
      DeleteDC(dc);
    return nullptr;
  }
  auto old = SelectObject(dc, bitmap);
  RECT bounds{0, 0, 900, 112};
  HBRUSH brush = CreateSolidBrush(RGB(20, 27, 38));
  FillRect(dc, &bounds, brush);
  DeleteObject(brush);
  SetBkMode(dc, TRANSPARENT);
  auto draw = [&](const std::string &text, int size, int y, COLORREF color) {
    HFONT font =
        CreateFontW(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
    auto previous = SelectObject(dc, font);
    SetTextColor(dc, color);
    RECT area{18, y, 880, y + 42};
    auto value = wide(text);
    DrawTextW(dc, value.c_str(), -1, &area,
              DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, previous);
    DeleteObject(font);
  };
  draw(title, 30, 12, RGB(245, 208, 86));
  draw(description, 24, 61, RGB(238, 240, 245));
  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
      pixels, 900, 112, 32, 900 * 4, SDL_PIXELFORMAT_RGB888);
  SDL_Texture *result =
      surface ? SDL_CreateTextureFromSurface(renderer, surface) : nullptr;
  if (surface)
    SDL_FreeSurface(surface);
  if (result)
    SDL_SetTextureBlendMode(result, SDL_BLENDMODE_NONE);
  SelectObject(dc, old);
  DeleteObject(bitmap);
  DeleteDC(dc);
  return result;
}
} // namespace

void YiRaInit(const char *path) {
  preferences_path = path ? path : "";
  std::ifstream input(preferences_path);
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line == "RetroAchievements=1")
      enabled = true;
    else if (line == "RetroAchievements=0")
      enabled = false;
  }
}
void YiRaDrawLauncher(const char *path, void *) {
  ImGui::SeparatorText("RetroAchievements");
  if (!rom_allowed(path)) {
    client.reset();
    running = false;
    overview = false;
    ram = nullptr;
    sram = nullptr;
    sram_size = 0;
    SecureZeroMemory(password, sizeof(password));
    ImGui::BeginDisabled();
    bool unavailable = false;
    ImGui::Checkbox("Enable RetroAchievements", &unavailable);
    char empty[1] = {0};
    ImGui::InputText("RA username", empty, sizeof(empty),
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::InputText("RA password", empty, sizeof(empty),
                     ImGuiInputTextFlags_ReadOnly |
                         ImGuiInputTextFlags_Password);
    ImGui::Button("Sign in");
    ImGui::EndDisabled();
    ImGui::TextWrapped("RetroAchievements supports the USA ROM only. "
                       "Unavailable for European ROMs.");
    return;
  }
  ImGui::TextUnformatted("USA: online Softcore achievements enabled");
  bool choice = enabled;
  if (ImGui::Checkbox("Enable RetroAchievements", &choice)) {
    enabled = choice;
    if (enabled)
      ensure();
    else {
      client.reset();
      SecureZeroMemory(password, sizeof(password));
    }
    save_preferences();
  }
  ImGui::TextDisabled("Hardcore unavailable. Credentials are never stored in "
                      "settings or logs.");
  if (!preferences_saved)
    ImGui::TextWrapped("Mod settings could not be saved; current choices still "
                       "apply this session.");
  if (!enabled) {
    ImGui::Spacing();
    return;
  }
  ensure();
  client->select_rom(path ? path : "");
  client->pump();
  const auto status = client->status();
  ImGui::TextWrapped("%s", status.message.c_str());
  ImGui::TextUnformatted("Mode: Softcore - online unlocks");
  ImGui::TextDisabled("F6: overview; F10 / Shift+F10: browse achievements");
  if (!status.hash.empty())
    ImGui::TextDisabled("ROM hash: %s", status.hash.c_str());
  if (!status.hash.empty())
    ImGui::TextDisabled("Server game ID: %u", client->diagnostics().game_id);
  if (!status.logged_in) {
    ImGui::BeginDisabled(status.busy);
    ImGui::InputText("RA username", username, sizeof(username));
    ImGui::InputText("RA password", password, sizeof(password),
                     ImGuiInputTextFlags_Password);
    if (ImGui::Button("Sign in")) {
      client->login(username, password);
      SecureZeroMemory(password, sizeof(password));
    }
    ImGui::EndDisabled();
  } else {
    ImGui::Text("Signed in: %s", status.username.c_str());
    if (ImGui::Button("Sign out")) {
      client->logout();
      SecureZeroMemory(password, sizeof(password));
    }
  }

  if (status.ready) {
    ImGui::Text("%s: %u / %u unlocked", status.game.c_str(), status.unlocked,
                status.total);
    if (ImGui::TreeNode("Achievements")) {
      for (const auto &entry : client->achievements()) {
        ImGui::TextWrapped("%s %s", entry.unlocked ? "[Unlocked]" : "[Locked]",
                           entry.title.c_str());
        ImGui::TextWrapped("%s", entry.description.c_str());
      }
      ImGui::TreePop();
    }
  }
  ImGui::Spacing();
}
void YiRaStartGame(const char *path, const uint8_t *wram, const uint8_t *cart,
                   unsigned size) {
  if (!rom_allowed(path, true)) {
    client.reset();
    running = false;
    overview = false;
    ram = nullptr;
    sram = nullptr;
    sram_size = 0;
    SecureZeroMemory(password, sizeof(password));
    std::printf("[RA] disabled: USA ROM required\n");
    return;
  }
  ram = wram;
  sram = cart;
  sram_size = size;
  running = true;

  if (enabled) {
    ensure();
    client->select_rom(path ? path : "");
    client->reset();
  }
  std::printf("[RA] enabled=%d mode=Softcore; canonical WRAM=131072 SRAM=%u; "
              "hardcore=off\n",
              enabled, size);
}
void YiRaFrame() {
  if (!enabled || !client || !running)
    return;
  client->frame();
  if (SDL_GetTicks64() >= popup_until && client->take_popup(popup)) {
    popup_until = SDL_GetTicks64() + 7000;
    if (popup.points)
      popup.title += " - " + std::to_string(popup.points) + " points";
    // Only identifiers/counters, never credentials or HTTP payloads.
    auto d = client->diagnostics();
    std::printf("[RA] popup; set=%u frames=%llu triggered=%u submitted=%u "
                "accepted=%u\n",
                d.game_id, (unsigned long long)client->evaluated_frames(),
                d.triggered, d.award_requests, d.award_accepted);
  }
}
void YiRaOverview(int action) {
  if (action == 0)
    overview = !overview;
  else {
    overview = true;
    if (action > 0)
      ++overview_index;
    else if (overview_index)
      --overview_index;
    else if (client) {
      auto entries = client->achievements();
      overview_index = entries.empty() ? 0 : (unsigned)entries.size() - 1;
    }
  }
}
void YiRaOverlay(SDL_Renderer *renderer) {
  if (!enabled || !client)
    return;
  std::string title, description;
  if (overview) {
    auto entries = client->achievements();
    if (entries.empty()) {
      title = "RetroAchievements";
      description = client->status().message;
    } else {
      overview_index %= entries.size();
      const auto &entry = entries[overview_index];
      title = std::to_string(overview_index + 1) + " / " +
              std::to_string(entries.size()) + " " +
              (entry.unlocked ? "[Unlocked] " : "[Locked] ") + entry.title;
      description = entry.description;
    }
  } else if (SDL_GetTicks64() < popup_until) {
    title = popup.title;
    description = popup.description;
  } else
    return;
  auto key = title + "\n" + description;
  if (key != texture_key || !popup_texture) {
    if (popup_texture)
      SDL_DestroyTexture(popup_texture);
    popup_texture = make_texture(renderer, title, description);
    texture_key = key;
  }
  int w = 0, h = 0;
  SDL_RenderGetLogicalSize(renderer, &w, &h);
  if (w <= 0 || h <= 0)
    SDL_GetRendererOutputSize(renderer, &w, &h);
  SDL_Rect box{w / 30, h / 30, w * 14 / 15, w * 14 / 15 * 112 / 900};
  if (popup_texture)
    SDL_RenderCopy(renderer, popup_texture, nullptr, &box);
}
void YiRaShutdown() {
  client.reset();
  if (popup_texture)
    SDL_DestroyTexture(popup_texture);
  popup_texture = nullptr;
  SecureZeroMemory(password, sizeof(password));
}
#ifdef YI_RA_TEST_UI
extern "C" int YiRaTestSoftcoreTransport() {
  class RecordingServices final : public yira::Services {
  public:
    unsigned calls = 0;
    yira::Response http(const std::string &, const std::string &) override {
      ++calls;
      return {200, "{}"};
    }
    bool restore(yira::Credential &) override { return false; }
    bool store(const yira::Credential &) override { return true; }
    void forget() override {}
  };
  auto backend = std::make_unique<RecordingServices>();
  auto *recording = backend.get();
  SoftcoreServices transport(std::move(backend));
  bool ok = transport.http("https://invalid.invalid", "r=awardachievement&a=1")
                    .status == 200 &&
            recording->calls == 1;
  ok =
      ok &&
      transport.http("https://invalid.invalid", "r=submitlbentry&i=1").status ==
          403 &&
      recording->calls == 1;
  yira::Client test_client(std::make_unique<RecordingServices>(), read);
  test_client.set_observer(false);
  return ok && !test_client.observer() && !test_client.hardcore();
}
extern "C" int YiRaTestRomAllowed(const char *path) {
  return rom_allowed(path, true);
}
extern "C" int YiRaTestBlockedStart(const char *path) {
  bool saved = enabled;
  enabled = true;
  unsigned before = client_initializations;
  YiRaStartGame(path, nullptr, nullptr, 0);
  bool blocked =
      !client && !running && !ram && !sram && client_initializations == before;
  enabled = saved;
  return blocked;
}
extern "C" int YiRaTestSavePreferences() {
  save_preferences();
  return preferences_saved ? 1 : 0;
}
// Synthetic software-renderer fixture only; absent from the player build.
extern "C" int YiRaTestPopup(SDL_Renderer *renderer) {
  SDL_Texture *texture =
      make_texture(renderer, "Synthetic achievement - 5 points",
                   "Readable notification; no game or account data.");
  if (!texture)
    return 0;
  SDL_Rect rect{0, 0, 900, 112};
  int result = SDL_RenderCopy(renderer, texture, nullptr, &rect);
  SDL_DestroyTexture(texture);
  return result == 0;
}
#endif

extern "C" void YiRaDrawPanel(const char *path) {
  YiRaDrawLauncher(path, nullptr);
}
