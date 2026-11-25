#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "runtime.h"
#include "rc_runtime.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
namespace yiach {
bool atomic_write(const std::filesystem::path& path,const std::string& text) {
 std::error_code ec;if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path(),ec);if(ec)return false;
 auto temp=path;temp+=L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
 HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
 DWORD count=0;bool ok=text.size()<MAXDWORD && WriteFile(file,text.data(),(DWORD)text.size(),&count,nullptr)&&count==text.size();
 ok=FlushFileBuffers(file)&&ok;ok=CloseHandle(file)&&ok;
 if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
 if(!ok)DeleteFileW(temp.c_str());return ok;
}
struct Runtime::Trigger {void* memory=nullptr;rc_trigger_t* trigger=nullptr;~Trigger(){std::free(memory);}};
Runtime::Runtime()=default;
Runtime::~Runtime(){flush();if(lock_)CloseHandle(lock_);}
bool Runtime::unlocked(unsigned slot,unsigned id)const{return slot<3&&slots_[slot].count(id);}
unsigned Runtime::count(unsigned slot)const{return slot<3?(unsigned)slots_[slot].size():0;}
unsigned Runtime::take_notification(){if(notifications_.empty())return 0;auto id=notifications_.front();notifications_.erase(notifications_.begin());return id;}
void Runtime::reset(){for(auto& t:triggers_)rc_reset_trigger(t->trigger);}
bool Runtime::read() {
 std::error_code ec;if(!std::filesystem::exists(path_,ec))return !ec;
 if(std::filesystem::file_size(path_,ec)>128*1024||ec)return false;
 std::ifstream in(path_);std::string key,rules;char token;unsigned format=0;int profile=-1;
 auto punct=[&](char c){return bool(in>>token)&&token==c;};
 auto field=[&](const char* name){return bool(in>>std::quoted(key))&&key==name&&punct(':');};
 if(!punct('{')||!field("format")||!(in>>format)||format!=1||!punct(',')||!field("rules")||!(in>>std::quoted(rules))||rules!=Rules||!punct(',')||!field("rom_profile")||!(in>>profile)||profile!=profile_||!punct(',')||!field("slots")||!punct('['))return false;
 std::array<std::set<unsigned>,3> candidate;
 for(unsigned slot=0;slot<3;++slot){if(slot&&!punct(','))return false;if(!punct('['))return false;
  in>>std::ws;if(in.peek()!=']'){for(;;){unsigned id;if(!(in>>id)||!candidate[slot].insert(id).second)return false;
   bool known=false;for(const auto& d:Catalog)known|=id==d.id;if(!known)return false;
   in>>std::ws;if(in.peek()==']')break;if(!punct(','))return false;}}
  if(!punct(']'))return false;
 }
 if(!punct(']')||!punct('}'))return false;in>>std::ws;if(!in.eof())return false;slots_=std::move(candidate);return true;
}
bool Runtime::inspect(int profile,const std::filesystem::path& save){profile_=profile;path_=save;path_.replace_extension(".achievements.json");return profile>=0&&profile<4&&read();}
bool Runtime::start(int profile,const std::filesystem::path& save) {
 if(profile<0||profile>=4)return false;profile_=profile;
 path_=save;path_.replace_extension(".achievements.json");
 std::error_code ec;if(!path_.parent_path().empty())std::filesystem::create_directories(path_.parent_path(),ec);if(ec){message_="Cannot create achievement folder.";return false;}
 auto lock_path=path_;lock_path+=L".lock";
 HANDLE handle=CreateFileW(lock_path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(handle==INVALID_HANDLE_VALUE){message_="Achievements file is in use; local tracking unavailable.";return false;}lock_=handle;
 if(!read()){message_="Achievements JSON is invalid or belongs to other rules/ROM; original preserved.";return false;}
 for(const auto& d:Catalog){int n=rc_trigger_size(d.condition);if(n<=0){message_="Invalid achievement rule.";return false;}
  auto t=std::make_unique<Trigger>();t->memory=std::malloc(n);if(!t->memory||!(t->trigger=rc_parse_trigger(t->memory,d.condition,nullptr,0))){message_="Cannot initialize achievement rule.";return false;}triggers_.push_back(std::move(t));}
 writable_=true;dirty_=true;return flush();
}
bool Runtime::flush(){if(!dirty_)return true;if(!writable_)return false;
 std::ostringstream out;out<<"{\n  \"format\": 1,\n  \"rules\": "<<std::quoted(Rules)<<",\n  \"rom_profile\": "<<profile_<<",\n  \"slots\": [";
 for(unsigned slot=0;slot<3;++slot){if(slot)out<<',';out<<"\n    [";bool first=true;for(auto id:slots_[slot]){if(!first)out<<',';out<<id;first=false;}out<<']';}out<<"\n  ]\n}\n";
 if(!atomic_write(path_,out.str())){message_="Achievements could not be saved; retrying locally.";return false;}
 dirty_=false;message_.clear();return true;
}
namespace {struct Memory{const uint8_t* ram;const uint8_t* sram;unsigned size;bool invalid=false;};
uint32_t RC_CCONV peek(uint32_t address,uint32_t count,void* context){auto& m=*static_cast<Memory*>(context);uint32_t value=0;
 if(count>4){m.invalid=true;return 0;}for(unsigned i=0;i<count;++i){uint32_t a=address+i;uint8_t byte=0;
 if(a<0x20000)byte=m.ram[a];else if(a>=0x20000&&a-0x20000<m.size)byte=m.sram[a-0x20000];else m.invalid=true;
 value|=uint32_t(byte)<<(8*i);}return value;}}
void Runtime::frame(const uint8_t* ram,const uint8_t* sram,unsigned size){if(!writable_||!ram||!sram)return;
 if(dirty_&&++retry_>=60){retry_=0;flush();}
 // Native slot index is a word-table offset: 0, 2, 4 (bank10 save_file_ptr).
 unsigned index=ram[0x30e];int slot=index<=4&&!(index&1)?int(index/2):-1;
 if(slot!=slot_){slot_=slot;reset();}if(slot_<0)return;
 Memory m{ram,sram,size};
 for(size_t i=0;i<triggers_.size();++i){const auto& d=Catalog[i];if(unlocked(slot_,d.id))continue;
  m.invalid=false;int state=rc_evaluate_trigger(triggers_[i]->trigger,peek,&m,nullptr);
  if(m.invalid){rc_reset_trigger(triggers_[i]->trigger);continue;}
  if(state==RC_TRIGGER_STATE_TRIGGERED){slots_[slot_].insert(d.id);notifications_.push_back(d.id);dirty_=true;flush();}
 }
}
}
