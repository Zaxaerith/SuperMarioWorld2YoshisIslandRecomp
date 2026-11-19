#include "preferences.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)
int main(int argc, char **argv) {
  CHECK(argc == 2);
  namespace fs = std::filesystem;
  auto root = fs::u8path(argv[1]) / (std::to_wstring(GetCurrentProcessId()) +
                                     L"-" + std::to_wstring(GetTickCount64()));
  CHECK(!fs::exists(root));
  fs::create_directories(root);
  std::string original =
      "\xef\xbb\xbf; Keep "
      "comment\r\n[Graphics]\r\nVSync=1\r\nRetroAchievements=77\r\n[ Mods "
      "]\r\nWidescreen=1\r\n retroachievements = "
      "0\r\nOther=hello\r\n[Audio]\r\nVolume=42";
  auto expected = original;
  expected.replace(expected.find(" retroachievements = 0"), 22,
                   "RetroAchievements=1");
  CHECK(yira::update_ra_preference(original, true) == expected);
  CHECK(yira::update_ra_preference(expected, true) == expected);
  CHECK(yira::update_ra_preference("[Mods]\nWidescreen=1\n[Other]\nX=2\n",
                                   false) ==
        "[Mods]\nWidescreen=1\nRetroAchievements=0\n[Other]\nX=2\n");
  CHECK(yira::update_ra_preference("[Graphics]\nVSync=0", true) ==
        "[Graphics]\nVSync=0\n[Mods]\nRetroAchievements=1\n");
  CHECK(yira::update_ra_preference(
            "[Mods]\nRetroAchievements=0\nRetroAchievements=1", false) ==
        "[Mods]\nRetroAchievements=0\nRetroAchievements=0");
  auto path = root / L"mods.ini";
  CHECK(yira::save_ra_preference(path, false));
  {
    std::ofstream f(path, std::ios::binary);
    f << original;
  }
  CHECK(yira::save_ra_preference(path, true));
  {
    std::ifstream f(path, std::ios::binary);
    std::string value((std::istreambuf_iterator<char>(f)), {});
    CHECK(value == expected);
  }
  CHECK(!yira::save_ra_preference(root, true));
  CHECK(!yira::save_ra_preference(root / L"missing/mods.ini", true));
  HANDLE lock = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(lock != INVALID_HANDLE_VALUE);
  CHECK(!yira::save_ra_preference(path, false));
  CloseHandle(lock);
  {
    std::ifstream f(path, std::ios::binary);
    std::string value((std::istreambuf_iterator<char>(f)), {});
    CHECK(value == expected);
  }
  std::puts("PASS section/key preservation, comments, BOM/CRLF, missing "
            "section/file, duplicate keys, locked file and write errors.");
}
