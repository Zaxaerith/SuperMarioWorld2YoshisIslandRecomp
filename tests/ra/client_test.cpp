#include "client.h"
#include "mock_services.h"
#include "rc_consoles.h"
#include "rc_hash.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

static uint8_t ram[0x20000]{}, sram[0x10000]{};
static uint32_t RC_CCONV memory(uint32_t a, uint8_t *b, uint32_t n,
                                rc_client_t *) {
  return yira::read_memory(a, b, n, ram, sram, sizeof(sram));
}
template <class P> void wait(yira::Client &c, P predicate) {
  for (int i = 0; i < 1500; ++i) {
    c.pump();
    if (predicate())
      return;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  std::fprintf(stderr, "Mock wait failed: %s\n", c.status().message.c_str());
  assert(false);
}
int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  assert(argc == 4);
  uint8_t out[8]{};
  ram[0x1ffff] = 17;
  sram[0] = 18;
  sram[0xffff] = 19;
  assert(memory(0x1ffff, out, 2, nullptr) == 2 && out[0] == 17 && out[1] == 18);
  assert(memory(0x2ffff, out, 8, nullptr) == 1 && out[0] == 19);
  assert(memory(0x30000, out, 1, nullptr) == 0);
  assert(memory(0x7e0000, out, 1, nullptr) ==
         0); // canonical RA space, no native aliases
  assert(memory(0xffffffff, out, 8, nullptr) == 0);
  assert(yira::read_memory(0x20000, out, 1, ram, nullptr, 0) == 0);
  std::string hash;
  assert(yira::hash_rom(argv[1], hash) &&
         hash == "cb472164c5a71ccd3739963390ec6a50");
  assert(yira::Client::can_query_hash(hash));
  assert(yira::hash_rom(argv[2], hash) &&
         hash == "9f235e4c17cf9762a49b83e82794d278");
  assert(!yira::Client::can_query_hash(hash));
  assert(yira::hash_rom(argv[3], hash) &&
         hash == "8de2ee0b0bdbdde16aefedecfb837971");
  assert(yira::Client::can_query_hash(
      hash)); // Allowed to query, not approved to award.
  {
    Fixture fixture;
    yira::Client c(std::make_unique<Mock>(fixture), memory);
    assert(c.observer() && !c.hardcore());
    c.select_rom(argv[1]);
    c.login("Fixture", "MockPassword");
    wait(c, [&] { return c.status().ready; });
    assert(c.status().total == 2 && c.achievements().size() == 2);
    ram[0x100] = 0;
    c.frame();
    ram[0x100] = 1;
    c.frame();
    c.pump();
    assert(c.diagnostics().triggered == 1 && fixture.awards == 0);
    assert(c.diagnostics().award_requests == 0); // observer never submits
  }
  {
    Fixture fixture;
    ram[0x100] = 0;
    sram[0x10] = 0;
    yira::Client c(std::make_unique<Mock>(fixture), memory);
    c.set_observer(false);
    assert(!c.observer() && !c.hardcore());
    c.select_rom(argv[1]);
    c.login("Fixture", "MockPassword");
    wait(c, [&] { return c.status().ready; });
    assert(c.diagnostics().active == 2 && c.diagnostics().unsupported == 0);
    c.frame();
    ram[0x100] = 1;
    c.frame();
    wait(c, [&] { return c.diagnostics().award_accepted == 1; });
    sram[0x10] = 9;
    c.frame();
    wait(c, [&] { return c.diagnostics().award_accepted == 2; });
    assert(fixture.awards == 2 && c.status().unlocked == 2 && !c.hardcore());
    yira::Popup popup;
    bool ram_popup = false, sram_popup = false;
    while (c.take_popup(popup)) {
      ram_popup |= popup.title == "RAM achievement";
      sram_popup |= popup.title == "SRAM achievement";
    }
    assert(ram_popup && sram_popup);
    c.logout();
    assert(!c.status().logged_in && fixture.credential.token.empty());
    c.login("Fixture", "MockPassword");
    wait(c, [&] { return c.status().ready; });
    c.select_rom(argv[2]);
    wait(c, [&] {
      return c.status().message.find("not enabled") != std::string::npos;
    });
    assert(!c.status().ready && c.status().total == 0);
    auto count = fixture.awards.load();
    c.frame();
    assert(fixture.awards == count);
  }
  {
    Fixture fixture;
    fixture.tester = true;
    ram[0x100] = 0;
    sram[0x10] = 0;
    yira::Client c(std::make_unique<Mock>(fixture), memory);
    c.set_observer(false);
    c.select_rom(argv[3]);
    c.login("Fixture", "MockPassword");
    wait(c, [&] { return c.status().ready; });
    assert(c.observer() &&
           !c.hardcore()); // Untested overrides attempted Softcore.
    assert(c.status().hash == "8de2ee0b0bdbdde16aefedecfb837971");
    assert(fixture.requested_hash == c.status().hash);
    assert(c.diagnostics().game_id == 1100000558u);
    assert(c.status().message.find("Untested") != std::string::npos);
    c.frame();
    ram[0x100] = 1;
    c.frame();
    sram[0x10] = 9;
    c.frame();
    c.pump();
    assert(c.diagnostics().triggered == 2 && fixture.awards == 0 &&
           c.diagnostics().award_requests == 0);
  }
  {
    Fixture fixture;
    yira::Client c(std::make_unique<Mock>(fixture), memory);
    c.set_observer(false);
    c.select_rom(argv[3]);
    c.login("Fixture", "MockPassword");
    wait(c, [&] { return c.status().ready; });
    c.frame();
    c.pump();
    assert(c.observer() && fixture.awards == 0 &&
           c.diagnostics().award_requests == 0);
    assert(c.status().total <= 1 && c.diagnostics().game_id == 1100000558u);
  }
  {
    Fixture fixture;
    fixture.store_ok = false;
    yira::Client c(std::make_unique<Mock>(fixture), memory);
    c.select_rom(argv[1]);
    c.login("Fixture", "MockPassword");
    wait(c, [&] { return c.status().ready; });
    assert(c.status().message.find("storage unavailable") != std::string::npos);
  }
  {
    Fixture fixture;
    fixture.login_rejected = true;
    yira::Client c(std::make_unique<Mock>(fixture), memory);
    c.select_rom(argv[1]);
    c.login("Fixture", "MockPassword");
    wait(c, [&] { return !c.status().busy; });
    assert(!c.status().logged_in && !c.status().ready);
  }
  std::puts("PASS: SNES memory bounds, actual USA/EU hashes, observer "
            "isolation, mocked Softcore unlocks/popup/account, logout, region "
            "switch, secure-store failure, rejected login. No online calls; no "
            "game execution.");
}
