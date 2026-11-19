#pragma once
#include "rc_client.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace yira {
struct Response {
  int status = 0;
  std::string body;
};
struct Credential {
  std::string username, token;
};
struct Services {
  virtual ~Services() = default;
  virtual Response http(const std::string &url, const std::string &body) = 0;
  virtual bool restore(Credential &) = 0;
  virtual bool store(const Credential &) = 0;
  virtual bool forget() = 0; // False means secure storage could not be cleared.
  virtual void cancel() {
  } // Called on shutdown; implementations may interrupt HTTP.
};
std::unique_ptr<Services>
windows_services(const std::wstring &target =
                     L"YoshisIslandRecomp/RetroAchievements/Softcore");
struct Status {
  bool logged_in = false, busy = false, ready = false;
  std::string username, message = "Not signed in", game, hash;
  unsigned unlocked = 0, total = 0;
};
struct Achievement {
  unsigned id = 0;
  bool unlocked = false;
  std::string title, description, badge_url;
};
struct Popup {
  std::string title, description, badge_url;
  unsigned points = 0;
  unsigned achievement_id = 0;
};
/* Read-only counters; never contain account names, credentials or HTTP bodies.
 */
struct Diagnostics {
  unsigned game_id = 0, active = 0, unsupported = 0, triggered = 0;
  unsigned award_requests = 0, award_accepted = 0, last_award_id = 0;
  int last_award_http = 0;
  unsigned first_event_state = 0;
};
/* The owner thread alone accesses rc_client and guest RAM. The owned worker
 * performs HTTP/disc reads only, delivering completions through pump(). */
class Client {
public:
  explicit Client(std::unique_ptr<Services> services,
                  rc_client_read_memory_func_t read);
  ~Client();
  Client(const Client &) = delete;
  void login(const char *username, const char *password);
  void restore();
  void set_observer(bool enabled);
  bool observer() const;
  void logout();
  void select_rom(const std::string &path);
  void pump();
  void frame();
  void reset();
  void set_notifications(bool on);
  Status status() const;
  bool take_popup(Popup &popup);
  void download_badge(const std::string &url,
                      std::function<void(Response)> callback);
  uint64_t evaluated_frames() const;
  bool hardcore() const;
  Diagnostics diagnostics() const;
  bool confirmed_award(unsigned achievement_id) const;
  std::vector<Achievement>
  achievements() const; // read-only official account state
  static bool can_query_hash(const std::string &);

private:
  struct Impl;
  std::unique_ptr<Impl> p;
};
/* Canonical RA SNES space: 128 KiB WRAM followed by cartridge RAM.
 * Never expose aliases, hardware registers or enhancement memory. */
uint32_t read_memory(uint32_t address, uint8_t *out, uint32_t size,
                     const uint8_t *ram, const uint8_t *sram,
                     uint32_t sram_size);
bool hash_rom(const std::string &path, std::string &hash);
} // namespace yira
