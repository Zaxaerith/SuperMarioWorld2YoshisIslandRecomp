#include "client.h"
#include "rc_api_runtime.h"
#include "rc_consoles.h"
#include "rc_hash.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

namespace yira {
uint32_t read_memory(uint32_t address, uint8_t *out, uint32_t n,
                     const uint8_t *ram, const uint8_t *sram,
                     uint32_t sram_size) {
  if (!out || !ram || address >= 0xa0000u)
    return 0;
  n = std::min(n, 0xa0000u - address);
  uint32_t copied = 0;
  if (address < 0x20000u) {
    copied = std::min(n, 0x20000u - address);
    std::memcpy(out, ram + address, copied);
  }
  const uint32_t current = address + copied;
  if (copied < n && current >= 0x20000u && sram &&
      current - 0x20000u < sram_size) {
    uint32_t count = std::min(n - copied, sram_size - (current - 0x20000u));
    std::memcpy(out + copied, sram + current - 0x20000u, count);
    copied += count;
  }
  return copied;
}
bool hash_rom(const std::string &path, std::string &hash) {
  char value[33]{};
  if (!rc_hash_generate_from_file(value, RC_CONSOLE_SUPER_NINTENDO,
                                  path.c_str()))
    return false;
  hash = value;
  return true;
}
bool Client::can_query_hash(const std::string &hash) {
  return hash == "cb472164c5a71ccd3739963390ec6a50" ||
         hash == "8de2ee0b0bdbdde16aefedecfb837971";
}
struct Client::Impl {
  std::unique_ptr<Services> services;
  rc_client_t *client = nullptr;
  rc_client_async_handle_t *login_handle = nullptr;
  Status view;
  Diagnostics diagnostics;
  std::string path, loading_hash;
  bool notifications = true, stop = false, session_saved = true;
  std::atomic<bool> closing{false};
  uint64_t frames = 0, media_generation = 0;
  std::deque<Popup> popups;
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<std::function<void()>> jobs, completions;
  std::thread worker;
  Impl(std::unique_ptr<Services> s, rc_client_read_memory_func_t read)
      : services(std::move(s)) {
    client = rc_client_create(read, server);
    rc_client_set_userdata(client, this);
    rc_client_set_host(client, "https://retroachievements.org");
    rc_client_set_hardcore_enabled(
        client, 0); // rc_client defaults to hardcore: always opt out FIRST.
    rc_client_set_spectator_mode_enabled(client, 1);
    rc_client_set_event_handler(client, event);
    rc_client_set_allow_background_memory_reads(client, 0);
    worker = std::thread([this] {
      for (;;) {
        std::function<void()> job;
        {
          std::unique_lock<std::mutex> lock(mutex);
          wake.wait(lock, [&] { return stop || !jobs.empty(); });
          if (jobs.empty() && stop)
            break;
          job = std::move(jobs.front());
          jobs.pop_front();
        }
        job();
      }
    });
  }
  ~Impl() {
    closing = true;
    services->cancel();
    if (login_handle)
      rc_client_abort_async(client, login_handle);
    {
      std::lock_guard<std::mutex> lock(mutex);
      stop = true;
    }
    wake.notify_all();
    worker.join();
    // Finish callbacks while the session/game is still owned by rc_client.
    pump();
    rc_client_reset(client);
    rc_client_logout(client);
    rc_client_destroy(client);
  }
  void enqueue(std::function<void()> job) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      jobs.push_back(std::move(job));
    }
    wake.notify_one();
  }
  void complete(std::function<void()> done) {
    std::lock_guard<std::mutex> lock(mutex);
    completions.push_back(std::move(done));
  }
  void pump() {
    std::deque<std::function<void()>> ready;
    {
      std::lock_guard<std::mutex> lock(mutex);
      ready.swap(completions);
    }
    for (auto &f : ready)
      f();
    if (!closing)
      rc_client_idle(client);
  }
  static Impl &self(rc_client_t *c) {
    return *static_cast<Impl *>(rc_client_get_userdata(c));
  }
  static void RC_CCONV server(const rc_api_request_t *req,
                              rc_client_server_callback_t callback, void *data,
                              rc_client_t *c) {
    auto &s = self(c);
    if (s.closing) {
      rc_api_server_response_t r{};
      r.http_status_code = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
      callback(&r, data);
      return;
    }
    std::string url = req->url ? req->url : "",
                body = req->post_data ? req->post_data : "";
    const bool award = body.find("r=awardachievement") != std::string::npos;
    if (award)
      ++s.diagnostics.award_requests;
    s.enqueue([&s, url = std::move(url), body = std::move(body), callback, data,
               award]() mutable {
      Response response;
      try {
        if (!s.closing)
          response = s.services->http(url, body);
      } catch (...) {
        response = {};
      }
      std::fill(body.begin(), body.end(),
                '\0'); // Includes transient password/token POST bodies.
      s.complete([&s, response = std::move(response), callback, data, award] {
        rc_api_server_response_t r{};
        r.body = response.body.empty() ? nullptr : response.body.data();
        r.body_length = response.body.size();
        r.http_status_code =
            response.status ? response.status
                            : RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
        if (award) {
          s.diagnostics.last_award_http = r.http_status_code;
          rc_api_award_achievement_response_t parsed{};
          if (rc_api_process_award_achievement_server_response(&parsed, &r) ==
                  RC_OK &&
              parsed.response.succeeded) {
            ++s.diagnostics.award_accepted;
            s.diagnostics.last_award_id = parsed.awarded_achievement_id;
          }
          rc_api_destroy_award_achievement_response(&parsed);
        }
        callback(&r, data);
      });
    });
  }
  void load() {
    if (closing || !view.logged_in || !can_query_hash(view.hash) ||
        loading_hash == view.hash)
      return;
    if (view.hash == "8de2ee0b0bdbdde16aefedecfb837971")
      rc_client_set_spectator_mode_enabled(client,
                                           1); // RA marks Rev2 Untested.
    loading_hash = view.hash;
    view.busy = true;
    view.message = rc_client_get_spectator_mode_enabled(client)
                       ? "Loading achievements (Observer)..."
                       : "Loading achievements (Softcore)...";
    rc_client_set_hardcore_enabled(client, 0);
    rc_client_begin_load_game(client, loading_hash.c_str(), loaded, this);
  }
  static void RC_CCONV logged(int result, const char *, rc_client_t *c,
                              void *) {
    auto &s = self(c);
    s.login_handle = nullptr;
    s.view.busy = false;
    if (s.closing)
      return;
    const auto *user = rc_client_get_user_info(c);
    if (result != RC_OK || !user) {
      s.view.logged_in = false;
      s.view.ready = false;
      s.view.username.clear();
      s.view.message =
          "Sign-in failed. Check credentials and connection; try again.";
      return;
    }
    s.view.logged_in = true;
    s.view.username = user->display_name ? user->display_name : user->username;
    const bool stored = s.services->store({user->username, user->token});
    s.session_saved = stored;
    s.view.message =
        stored ? "Signed in (Softcore)"
               : "Signed in; Windows could not save the session securely.";
    s.load();
  }
  static void RC_CCONV loaded(int result, const char *, rc_client_t *c,
                              void *) {
    auto &s = self(c);
    s.view.busy = false;
    if (s.closing)
      return;
    const auto *game = rc_client_get_game_info(c);
    if (result != RC_OK || !game ||
        (game->id != 558 &&
         !(game->id == 1100000558u &&
           s.view.hash == "8de2ee0b0bdbdde16aefedecfb837971")) ||
        game->console_id != RC_CONSOLE_SUPER_NINTENDO) {
      rc_client_unload_game(c);
      s.loading_hash.clear();
      s.view.ready = false;
      if (result == RC_INVALID_CREDENTIALS || result == RC_ACCESS_DENIED) {
        s.view.logged_in = false;
        s.view.username.clear();
      }
      s.view.message =
          "RA did not provide a loadable set for this exact hash (result=" +
          std::to_string(result) + "). Untested hashes require RA approval.";
      std::printf("[RA] load declined result=%d hash=%s\n", result,
                  s.view.hash.c_str());
      return;
    }
    s.view.ready = true;
    s.view.game = game->title;
    s.view.message = rc_client_get_spectator_mode_enabled(c)
                         ? "Ready (Observer; no online submissions)"
                         : "Ready (Softcore)";
    if (!s.session_saved)
      s.view.message +=
          "; secure session storage unavailable. Sign in again next time.";
    if (s.view.hash == "8de2ee0b0bdbdde16aefedecfb837971")
      s.view.message =
          "Europe Rev 2: RA Untested; Observer only. No online unlocks.";
    s.summary();
    std::printf(
        "[RA] loaded exact hash=%s game=%u console=%u total=%u observer=%d\n",
        s.view.hash.c_str(), game->id, game->console_id, s.view.total,
        rc_client_get_spectator_mode_enabled(c));
    if (s.notifications)
      s.popups.push_back(
          {s.view.game,
           (rc_client_get_spectator_mode_enabled(c) ? "Observer: "
                                                    : "Softcore: ") +
               std::to_string(s.view.unlocked) + " / " +
               std::to_string(s.view.total) + " achievements unlocked",
           {},
           0});
  }
  void summary() {
    rc_client_user_game_summary_t v{};
    rc_client_get_user_game_summary(client, &v);
    view.unlocked = v.num_unlocked_achievements;
    view.total = v.num_core_achievements;
  }
  static void RC_CCONV event(const rc_client_event_t *e, rc_client_t *c) {
    auto &s = self(c);
    if (s.closing)
      return;
    if (e->type == RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED && e->achievement) {
      ++s.diagnostics.triggered;
      s.summary();
      if (s.notifications) {
        const auto &a = *e->achievement;
        char url[1024]{};
        rc_client_achievement_get_image_url(
            &a, RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED, url, sizeof(url));
        s.popups.push_back({a.title ? a.title : "Achievement unlocked",
                            a.description ? a.description : "", url, a.points});
      }
    } else if (e->type == RC_CLIENT_EVENT_DISCONNECTED)
      s.view.message =
          "Offline; pending unlocks will retry while the session is open.";
    else if (e->type == RC_CLIENT_EVENT_RECONNECTED)
      s.view.message = "Ready (Softcore)";
    else if (e->type == RC_CLIENT_EVENT_SERVER_ERROR) {
      s.view.message =
          "RetroAchievements request failed; check your account/connection.";
      if (s.view.hash == "8de2ee0b0bdbdde16aefedecfb837971")
        s.view.message = "Rev 2 is RA Untested. The server may return only a "
                         "warning set; tester approval is required.";
      if (e->server_error &&
          (e->server_error->result == RC_INVALID_CREDENTIALS ||
           e->server_error->result == RC_ACCESS_DENIED)) {
        s.view.logged_in = false;
        s.view.ready = false;
        s.view.username.clear();
        s.view.message = "Session expired. Sign in again in the launcher.";
      }
    }
  }
};
Client::Client(std::unique_ptr<Services> s, rc_client_read_memory_func_t read)
    : p(new Impl(std::move(s), read)) {}
Client::~Client() = default;
void Client::login(const char *username, const char *password) {
  if (p->view.busy || !username || !*username || !password || !*password)
    return;
  if (!p->view.logged_in && rc_client_get_user_info(p->client)) {
    rc_client_logout(p->client);
    p->loading_hash.clear();
    p->view.ready = false;
  }
  p->view.busy = true;
  p->view.message = "Signing in...";
  p->login_handle = rc_client_begin_login_with_password(
      p->client, username, password, Impl::logged, p.get());
}
void Client::set_observer(bool enabled) {
  if (!p->view.logged_in && !p->view.busy)
    rc_client_set_spectator_mode_enabled(p->client, enabled);
}
bool Client::observer() const {
  return rc_client_get_spectator_mode_enabled(p->client) != 0;
}
void Client::restore() {
  Credential c;
  if (!p->services->restore(c))
    return;
  p->view.busy = true;
  p->view.message = "Restoring secure session...";
  p->login_handle = rc_client_begin_login_with_token(
      p->client, c.username.c_str(), c.token.c_str(), Impl::logged, p.get());
  std::fill(c.token.begin(), c.token.end(), '\0');
}
void Client::logout() {
  if (p->login_handle)
    rc_client_abort_async(p->client, p->login_handle);
  p->login_handle = nullptr;
  rc_client_reset(p->client);
  rc_client_logout(p->client);
  p->services->forget();
  auto hash = p->view.hash;
  p->view = {};
  p->view.hash = hash;
  p->loading_hash.clear();
  p->popups.clear();
}
void Client::select_rom(const std::string &path) {
  if (path == p->path)
    return;
  p->path = path;
  p->loading_hash.clear();
  p->view.hash.clear();
  p->view.ready = false;
  p->view.game.clear();
  p->view.total = 0;
  p->view.unlocked = 0;
  p->view.busy = p->login_handle != nullptr;
  p->popups.clear();
  rc_client_reset(p->client);
  rc_client_unload_game(p->client);
  const auto generation = ++p->media_generation;
  if (path.empty()) {
    p->view.message = "Select a valid game ROM to load achievements.";
    return;
  }
  p->view.message = "Identifying original game ROM...";
  p->enqueue([s = p.get(), path, generation] {
    std::string hash;
    const bool ok = !s->closing && hash_rom(path, hash);
    s->complete([s, hash = std::move(hash), ok, generation] {
      if (s->closing || generation != s->media_generation)
        return;
      if (!ok || !can_query_hash(hash)) {
        s->view.hash = hash;
        s->view.message = "Hash not enabled in this test build (USA v1.0 / "
                          "Europe Rev 2 only).";
        return;
      }
      s->view.hash = hash;
      s->load();
    });
  });
}
void Client::pump() { p->pump(); }
void Client::frame() {
  p->pump();
  if (p->view.ready) {
    rc_client_do_frame(p->client);
    ++p->frames;
  }
}
void Client::reset() { rc_client_reset(p->client); }
void Client::set_notifications(bool v) {
  p->notifications = v;
  if (!v)
    p->popups.clear();
}
Status Client::status() const { return p->view; }
bool Client::take_popup(Popup &out) {
  if (p->popups.empty())
    return false;
  out = std::move(p->popups.front());
  p->popups.pop_front();
  return true;
}
void Client::download_badge(const std::string &url,
                            std::function<void(Response)> callback) {
  p->enqueue([s = p.get(), url, callback = std::move(callback)] {
    Response response;
    try {
      if (!s->closing)
        response = s->services->http(url, {});
    } catch (...) {
    }
    s->complete([s, response = std::move(response), callback] {
      if (!s->closing)
        callback(response);
    });
  });
}
uint64_t Client::evaluated_frames() const { return p->frames; }
bool Client::hardcore() const {
  return rc_client_get_hardcore_enabled(p->client) != 0;
}
Diagnostics Client::diagnostics() const {
  auto result = p->diagnostics;
  if (const auto *game = rc_client_get_game_info(p->client))
    result.game_id = game->id;
  if (const auto *a = rc_client_get_achievement_info(p->client, 83254))
    result.first_event_state = a->state;
  auto *list = rc_client_create_achievement_list(
      p->client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
      RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
  if (list) {
    for (unsigned i = 0; i < list->num_buckets; ++i)
      for (unsigned j = 0; j < list->buckets[i].num_achievements; ++j) {
        const auto *a = list->buckets[i].achievements[j];
        if (a->state == RC_CLIENT_ACHIEVEMENT_STATE_ACTIVE)
          ++result.active;
        if (a->state == RC_CLIENT_ACHIEVEMENT_STATE_DISABLED)
          ++result.unsupported;
      }
    rc_client_destroy_achievement_list(list);
  }
  return result;
}
std::vector<Achievement> Client::achievements() const {
  std::vector<Achievement> result;
  if (!p->view.logged_in || !p->view.ready)
    return result;
  auto *list = rc_client_create_achievement_list(
      p->client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
      RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
  if (list) {
    for (unsigned i = 0; i < list->num_buckets; ++i)
      for (unsigned j = 0; j < list->buckets[i].num_achievements; ++j) {
        const auto *a = list->buckets[i].achievements[j];
        char url[1024]{};
        const bool unlocked =
            a->unlocked != RC_CLIENT_ACHIEVEMENT_UNLOCKED_NONE;
        rc_client_achievement_get_image_url(
            a,
            unlocked ? RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED
                     : RC_CLIENT_ACHIEVEMENT_STATE_ACTIVE,
            url, sizeof(url));
        result.push_back({a->id, unlocked, a->title ? a->title : "",
                          a->description ? a->description : "", url});
      }
    rc_client_destroy_achievement_list(list);
  }
  std::sort(result.begin(), result.end(),
            [](const auto &a, const auto &b) { return a.id < b.id; });
  return result;
}

} // namespace yira
