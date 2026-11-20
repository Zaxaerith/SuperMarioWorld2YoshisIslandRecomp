#pragma once
#include "catalog.h"
#include "rc_runtime_types.h"
#include <array>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>
namespace yiach {
class Runtime {
public:
 Runtime();~Runtime();
 bool inspect(int profile,const std::filesystem::path& save);
 bool start(int profile,const std::filesystem::path& save);
 void frame(const uint8_t* ram,const uint8_t* sram,unsigned size);
 bool flush();bool unlocked(unsigned slot,unsigned id)const;
 unsigned count(unsigned slot)const;
 const std::string& message()const{return message_;}
 const std::filesystem::path& path()const{return path_;}
 int slot()const{return slot_;}
 unsigned take_notification();
private:
 struct Trigger;std::vector<std::unique_ptr<Trigger>> triggers_;
 std::array<std::set<unsigned>,3> slots_;
 std::vector<unsigned> notifications_;
 std::filesystem::path path_;void* lock_=nullptr;
 int profile_=-1,slot_=-1;bool dirty_=false,writable_=false;
 unsigned retry_=0;std::string message_;
 void reset();bool read();
};
bool atomic_write(const std::filesystem::path&,const std::string&);
}
