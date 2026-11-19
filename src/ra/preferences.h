#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <windows.h>

namespace yira {
inline std::string ini_name(std::string value) {
  const auto first = value.find_first_not_of(" \t\r");
  if (first == std::string::npos)
    return {};
  value = value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
  for (auto &c : value)
    if (c >= 'A' && c <= 'Z')
      c += ('a' - 'A');
  return value;
}
// Change only Mods/RetroAchievements. Keep all unrelated bytes, including BOM,
// comments, line endings and identically named keys in other sections.
inline std::string update_ra_preference(const std::string &input,
                                        bool enabled) {
  const std::string bom =
      input.compare(0, 3, "\xef\xbb\xbf") == 0 ? input.substr(0, 3) : "";
  const std::string newline =
      input.find("\r\n") != std::string::npos ? "\r\n" : "\n";
  const std::string setting = "RetroAchievements=" + std::to_string(enabled);
  std::string output = bom;
  bool mods = false, found = false, had_mods = false;
  auto append_setting = [&] {
    if (!output.empty() && output.back() != '\n')
      output += newline;
    output += setting + newline;
    found = true;
  };
  for (size_t start = bom.size(); start < input.size();) {
    auto end = input.find('\n', start);
    if (end == std::string::npos)
      end = input.size();
    auto line = input.substr(start, end - start);
    auto name = ini_name(line);
    if (!name.empty() && name[0] == '[') {
      const auto close = name.find(']');
      if (close != std::string::npos) {
        if (mods && !found)
          append_setting();
        mods = ini_name(name.substr(1, close - 1)) == "mods";
        had_mods |= mods;
      }
    }
    const auto equals = name.find('=');
    if (mods && equals != std::string::npos &&
        ini_name(name.substr(0, equals)) == "retroachievements") {
      output += setting;
      if (!line.empty() && line.back() == '\r')
        output += '\r';
      if (end < input.size())
        output += '\n';
      found = true;
    } else
      output += input.substr(start, end - start + (end < input.size() ? 1 : 0));
    start = end + (end < input.size() ? 1 : 0);
  }
  if (!found) {
    if (!had_mods) {
      if (!output.empty() && output.back() != '\n')
        output += newline;
      output += "[Mods]" + newline;
    }
    append_setting();
  }
  return output;
}
inline bool save_ra_preference(const std::filesystem::path &path,
                               bool enabled) {
  std::error_code error;
  std::string input;
  if (std::filesystem::exists(path, error)) {
    if (!std::filesystem::is_regular_file(path, error) || error)
      return false;
    auto size = std::filesystem::file_size(path, error);
    if (error || size > 1024 * 1024)
      return false;
    std::ifstream file(path, std::ios::binary);
    if (!file)
      return false;
    input.assign(std::istreambuf_iterator<char>(file), {});
    if (file.bad())
      return false;
  } else if (error)
    return false;
  const auto output = update_ra_preference(input, enabled);
  static std::atomic<unsigned> serial{0};
  auto temporary = path;
  temporary += L".ra-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
               std::to_wstring(++serial) + L".tmp";
  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return false;
  DWORD written = 0;
  bool ok =
      WriteFile(file, output.data(), (DWORD)output.size(), &written, nullptr) &&
      written == output.size();
  ok = FlushFileBuffers(file) && ok;
  CloseHandle(file);
  if (ok)
    ok = MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  if (!ok)
    DeleteFileW(temporary.c_str());
  return ok;
}
} // namespace yira
