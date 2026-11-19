#include "client.h"
#include "mock_services.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
static uint8_t ram[0x20000]{}, sram[0x10000]{};
static std::thread::id owner;
static uint32_t RC_CCONV memory(uint32_t a, uint8_t *b, uint32_t n,
                                rc_client_t *) {
  assert(std::this_thread::get_id() == owner);
  return yira::read_memory(a, b, n, ram, sram, sizeof(sram));
}
template <class P> void wait(yira::Client &c, P done) {
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(12);
  do {
    c.pump();
    if (done())
      return;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  } while (std::chrono::steady_clock::now() < end);
  std::fprintf(stderr, "Timeout: %s\n", c.status().message.c_str());
  assert(false);
}
struct Block {
  std::mutex mutex;
  std::condition_variable wake;
  bool started = false, cancelled = false;
};
struct BlockingMock final : Mock {
  Block &block;
  BlockingMock(Fixture &f, Block &b) : Mock(f), block(b) {}
  yira::Response http(const std::string &, const std::string &) override {
    std::unique_lock<std::mutex> lock(block.mutex);
    block.started = true;
    block.wake.notify_all();
    block.wake.wait(lock, [&] { return block.cancelled; });
    return {};
  }
  void cancel() override {
    std::lock_guard<std::mutex> lock(block.mutex);
    block.cancelled = true;
    block.wake.notify_all();
  }
};
int main(int argc, char **argv) {
  assert(argc == 2);
  owner = std::this_thread::get_id();
  Fixture f;
  f.remember_unlocks = true;
  {
    yira::Client c(std::make_unique<Mock>(f), memory);
    c.set_observer(false);
    c.select_rom(argv[1]);
    c.login("Fixture", "MockPassword");
    wait(c, [&] { return c.status().ready; });
    c.frame();
    ram[0x100] = 1;
    c.frame();
    wait(c, [&] { return c.diagnostics().award_accepted == 1; });
  }
  auto passwords = f.password_logins.load();
  auto awards = f.awards.load();
  {
    yira::Client c(std::make_unique<Mock>(f), memory);
    c.set_observer(false);
    c.select_rom(argv[1]);
    c.restore();
    wait(c, [&] { return c.status().ready; });
    assert(f.password_logins == passwords && c.status().unlocked == 1);
    c.frame();
    c.pump();
    assert(f.awards == awards);
    f.offline = true;
    sram[0x10] = 9;
    c.frame();
    wait(c, [&] {
      return c.status().message.find("Offline") != std::string::npos;
    });
    assert(c.diagnostics().award_accepted == 0);
    f.offline = false;
    wait(c, [&] { return c.diagnostics().award_accepted == 1; });
    assert(f.awards == awards + 1);
    c.logout();
    assert(f.credential.token.empty() && !c.status().logged_in);
  }
  {
    yira::Client c(std::make_unique<Mock>(f), memory);
    c.restore();
    c.pump();
    assert(!c.status().logged_in && !c.status().busy);
  }
  {
    Fixture failed;
    failed.forget_ok = false;
    yira::Client c(std::make_unique<Mock>(failed), memory);
    c.logout();
    assert(!c.status().logged_in &&
           c.status().message.find("could not be removed") !=
               std::string::npos);
  }
  {
    Fixture pending;
    Block block;
    auto c = std::make_unique<yira::Client>(
        std::make_unique<BlockingMock>(pending, block), memory);
    c->login("Fixture", "MockPassword");
    {
      std::unique_lock<std::mutex> lock(block.mutex);
      assert(block.wake.wait_for(lock, std::chrono::seconds(2),
                                 [&] { return block.started; }));
    }
    auto start = std::chrono::steady_clock::now();
    c.reset();
    assert(block.cancelled &&
           std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
  }
  std::puts(
      "PASS mocked token restore, unlock deduplication, offline/reconnect "
      "retry, logout/no restore, owner-thread memory and cancellation during "
      "blocked HTTP. No live account/network/game.");
}
