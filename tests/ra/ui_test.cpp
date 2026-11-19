#include "imgui.h"
#include "yi_ra.h"
#include <SDL.h>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
extern "C" int YiRaTestSoftcoreTransport();
extern "C" int YiRaTestRomAllowed(const char *);
extern "C" int YiRaTestBlockedStart(const char *);
extern "C" int YiRaTestSavePreferences();
extern "C" int YiRaTestPopup(SDL_Renderer *);
extern "C" void YiRaTestCapture(SDL_Renderer*,uint64_t);
int main(int argc, char **argv) {
  assert(argc == 2 || argc == 6);
  if (argc == 6) {
    assert(YiRaTestRomAllowed(argv[2]));
    for (int i = 3; i < 6; ++i) {
      assert(!YiRaTestRomAllowed(argv[i]));
      assert(YiRaTestBlockedStart(argv[i]));
    }
    assert(!YiRaTestRomAllowed("missing-rom.sfc"));
    assert(YiRaTestBlockedStart("missing-rom.sfc"));
  }
  std::string prefs = std::string(argv[1]) + ".ini";
  {
    std::ofstream file(prefs);
    file << "[Mods]\nRetroAchievements=0\nUnrelated=1\n";
  }
  YiRaInit(prefs.c_str());
  assert(YiRaTestSoftcoreTransport());
  assert(YiRaTestSavePreferences());
  {
    std::ifstream file(prefs);
    std::string data((std::istreambuf_iterator<char>(file)), {});
    assert(data.find("Unrelated=1") != std::string::npos &&
           data.find("RetroAchievements=0") != std::string::npos);
  }
  SDL_Surface *surface =
      SDL_CreateRGBSurfaceWithFormat(0, 900, 200, 32, SDL_PIXELFORMAT_BGRA32);
  assert(surface);
  SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
  assert(renderer);
  assert(YiRaTestPopup(renderer));
  YiRaTestCapture(renderer,900001);
  {
    std::ifstream file("ra-900001-info.json");std::string data((std::istreambuf_iterator<char>(file)),{});
    assert(data.find("\"fixture\": true")!=std::string::npos && data.find("\"image_saved\": true")!=std::string::npos && data.find("\"server_confirmed_for_popup\": false")!=std::string::npos);
  }
  SDL_RenderPresent(renderer);
  unsigned varied = 0;
  auto *pixels = (uint32_t *)surface->pixels;
  for (unsigned i = 0; i < 900 * 112; ++i)
    if ((pixels[i] & 0xffffff) != (pixels[0] & 0xffffff))
      ++varied;
  assert(varied > 1000);
  assert(SDL_SaveBMP(surface, argv[1]) == 0);
  SDL_DestroyRenderer(renderer);
  SDL_FreeSurface(surface);
  ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(900, 700);
  io.DeltaTime = 1.0f / 60;
  io.IniFilename = nullptr;
  unsigned char *data;
  int w, h;
  io.Fonts->GetTexDataAsRGBA32(&data, &w, &h);
  for (int frame = 0; frame < 2; ++frame) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(900, 700));
    ImGui::Begin("Synthetic RA settings");
    YiRaDrawLauncher("", nullptr);
    ImGui::End();
    ImGui::Render();
  }
  assert(ImGui::GetDrawData()->TotalVtxCount > 0);
  ImGui::DestroyContext();
  YiRaShutdown();
  std::remove(prefs.c_str());
  std::puts("PASS USA-only ROM gate blocks EU revisions and invalid starts "
            "before client creation; Softcore mock transport, popup and "
            "preferences pass. No live submissions.");
}
