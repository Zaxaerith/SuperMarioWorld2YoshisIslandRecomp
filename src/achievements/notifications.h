#pragma once
#include <cstdint>
#include <deque>
#include <string>
namespace yiach {
struct Notification {std::string title,description;uint64_t duration;};
class Notifications {
 std::deque<Notification> pending_;Notification current_;uint64_t until_=0;bool active_=false;
public:
 void clear(){pending_.clear();active_=false;until_=0;}
 void add(std::string title,std::string description,uint64_t duration=5000){pending_.push_back({std::move(title),std::move(description),duration});}
 const Notification* visible(uint64_t now){
  if(active_&&now>=until_)active_=false;
  if(!active_&&!pending_.empty()){current_=std::move(pending_.front());pending_.pop_front();until_=now+current_.duration;active_=true;}
  return active_?&current_:nullptr;
 }
};
}
