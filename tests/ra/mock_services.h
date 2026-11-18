#pragma once
#include "client.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#undef assert
#define assert(condition)                                                      \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__,    \
                   #condition);                                                \
      std::_Exit(1);                                                           \
    }                                                                          \
  } while (0)
struct Fixture {
  bool untested = false;
  bool tester = false;
  std::string requested_hash;
  yira::Credential credential;
  std::atomic<unsigned> awards{0}, logins{0}, password_logins{0}, earned{0};
  std::atomic<bool> offline{false};
  bool store_ok = true, pointer_case = false, remember_unlocks = false,
       login_rejected = false;
};
static const char *game =
    R"({"Success":true,"GameId":558,"Title":"Yoshi synthetic set","ConsoleId":3,"ImageIconUrl":"https://media.retroachievements.org/Images/1.png","RichPresenceGameId":558,"RichPresencePatch":"","Sets":[{"AchievementSetId":558,"GameId":558,"Title":null,"Type":"core","ImageIconUrl":"https://media.retroachievements.org/Images/1.png","Achievements":[{"ID":900001,"Title":"RAM achievement","Description":"Mock only","Flags":3,"Points":5,"MemAddr":"0xH000100=1","Author":"Fixture","BadgeName":"00001","Created":1367266583,"Modified":1376929305},{"ID":900002,"Title":"SRAM achievement","Description":"Mock only","Flags":3,"Points":2,"MemAddr":"0xH020010=9","Author":"Fixture","BadgeName":"00002","Created":1367266583,"Modified":1376929305}],"Leaderboards":[]}]})";
struct Mock : yira::Services {
  std::shared_ptr<Fixture> owned;
  Fixture &f;
  explicit Mock(Fixture &state) : f(state) {}
  explicit Mock(std::shared_ptr<Fixture> state)
      : owned(std::move(state)), f(*owned) {}
  yira::Response http(const std::string &url,
                      const std::string &body) override {
    if (url.rfind("https://media.retroachievements.org/", 0) == 0)
      return {404, {}};
    assert(url.rfind("https://retroachievements.org/", 0) == 0);
    if (f.offline)
      return {};
    if (body.find("r=login2") != std::string::npos) {
      ++f.logins;
      if (body.find("&p=") != std::string::npos)
        ++f.password_logins;
      if (f.login_rejected)
        return {
            200,
            R"({"Success":false,"Error":"Invalid credentials","Code":"invalid_credentials"})"};
      return {
          200,
          R"({"Success":true,"User":"Fixture","Token":"OnlyAMockToken","Score":0,"SoftcoreScore":0,"Messages":0})"};
    }
    if (body.find("r=achievementsets") != std::string::npos) {
      const auto start = body.find("m=");
      if (start != std::string::npos)
        f.requested_hash = body.substr(start + 2, 32);
      f.untested = f.requested_hash == "8de2ee0b0bdbdde16aefedecfb837971";
      std::string data = game;
      if (f.untested && !f.tester) {
        return {
            200,
            R"json({"Success":true,"GameId":1100000558,"Title":"Unsupported Game Version (synthetic warning)","ConsoleId":3,"ImageIconUrl":"https://media.retroachievements.org/Images/1.png","Sets":[{"AchievementSetId":558,"GameId":1100000558,"Title":null,"Type":"core","ImageIconUrl":"https://media.retroachievements.org/Images/1.png","Achievements":[{"ID":101000001,"Title":"Unsupported Game Version","Description":"Untested hash: synthetic server warning","Flags":3,"Points":0,"MemAddr":"1=1","Author":"Fixture","BadgeName":"00000","Created":1,"Modified":1}],"Leaderboards":[]}]})json"};
      }
      if (f.untested) {
        size_t pos = 0;
        while ((pos = data.find("558", pos)) != std::string::npos) {
          data.replace(pos, 3, "1100000558");
          pos += 10;
        }
      }
      return {200, std::move(data)};
    }
    if (body.find("r=startsession") != std::string::npos) {
      assert(body.find("&h=0") != std::string::npos);
      if (f.remember_unlocks && (f.earned & 1))
        return {
            200,
            R"({"Success":true,"Unlocks":[{"ID":900001,"When":1660000000}],"HardcoreUnlocks":[]})"};
      return {200, R"({"Success":true,"Unlocks":[],"HardcoreUnlocks":[]})"};
    }
    if (body.find("r=awardachievement") != std::string::npos) {
      assert(body.find("&h=0") != std::string::npos);
      const unsigned awards = ++f.awards;
      const auto id =
          body.find("a=900002") != std::string::npos ? 900002 : 900001;
      f.earned.fetch_or(id == 900001 ? 1u : 2u);
      return {200, "{\"Success\":true,\"Score\":0,\"SoftcoreScore\":7,"
                   "\"AchievementID\":" +
                       std::to_string(id) + ",\"AchievementsRemaining\":" +
                       std::to_string(awards < 2 ? 2 - awards : 0) + "}"};
    }
    if (body.find("r=ping") != std::string::npos)
      return {200, R"({"Success":true})"};
    assert(false && "Unexpected mock API request");
    return {};
  }
  bool restore(yira::Credential &out) override {
    out = f.credential;
    return !out.token.empty();
  }
  bool store(const yira::Credential &c) override {
    if (!f.store_ok)
      return false;
    f.credential = c;
    return true;
  }
  void forget() override { f.credential = {}; }
};
