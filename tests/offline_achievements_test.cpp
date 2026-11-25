#include "runtime.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
using namespace yiach;
int main(int argc,char**argv){assert(argc==2);std::filesystem::path root=argv[1];std::filesystem::create_directories(root);
 auto save=root/"save.srm";auto json=save;json.replace_extension(".achievements.json");std::filesystem::remove(json);
 std::vector<uint8_t> ram(0x20000),sram(0x10000);ram[0x5b99]=64;ram[0x379]=3;
 // All recognized ROM profiles can own independent local JSON; this is not
 // evidence that every USA-origin condition is correct for PAL gameplay.
 for(int profile=0;profile<4;++profile){auto file=root/("profile-"+std::to_string(profile)+".srm");auto side=file;side.replace_extension(".achievements.json");std::filesystem::remove(side);
  Runtime r;assert(r.start(profile,file));auto before_ram=ram,before_sram=sram;r.frame(ram.data(),sram.data(),sram.size());assert(ram==before_ram&&sram==before_sram);}
 {Runtime unsupported;assert(!unsupported.start(-1,root/"unsupported.srm"));assert(!unsupported.start(4,root/"unsupported.srm"));}

 {Runtime a;assert(a.start(0,save));assert(std::filesystem::exists(json));Runtime competing;assert(!competing.start(0,save));
 a.frame(ram.data(),sram.data(),sram.size());ram[0x3b8]=4;a.frame(ram.data(),sram.data(),sram.size());ram[0x3b8]=5;a.frame(ram.data(),sram.data(),sram.size());assert(a.unlocked(0,1300));assert(a.count(0)==1);assert(a.take_notification()==1300);a.frame(ram.data(),sram.data(),sram.size());assert(!a.take_notification());
 ram[0x30e]=2;ram[0x3b8]=0;a.frame(ram.data(),sram.data(),sram.size());assert(!a.unlocked(1,1300));ram[0x3b8]=4;a.frame(ram.data(),sram.data(),sram.size());ram[0x3b8]=5;a.frame(ram.data(),sram.data(),sram.size());assert(a.unlocked(1,1300));assert(!a.unlocked(2,1300));}
 {Runtime a;assert(a.start(0,save));assert(a.unlocked(0,1300)&&a.unlocked(1,1300));assert(!a.unlocked(2,1300));}
 auto original=std::filesystem::file_size(json);
 {auto absent=root/"inspect-only.srm";auto side=absent;side.replace_extension(".achievements.json");std::filesystem::remove(side);Runtime view;assert(view.inspect(0,absent));assert(!std::filesystem::exists(side));}{Runtime wrong;assert(!wrong.start(1,save));}assert(std::filesystem::file_size(json)==original);
 {std::ofstream out(json);out<<"broken";} {Runtime bad;assert(!bad.start(0,save));}assert(std::filesystem::file_size(json)==6);
 // Actual Simple Pudding rule: five egg-use decreases succeeds; six must not.
 for(unsigned eggs:{5u,6u}){auto p=root/("pudding-"+std::to_string(eggs)+".srm");auto j=p;j.replace_extension(".achievements.json");std::filesystem::remove(j);
 Runtime a;assert(a.start(0,p));std::fill(ram.begin(),ram.end(),0);std::fill(sram.begin(),sram.end(),0);ram[0x5b99]=64;ram[0x379]=3;ram[0x21a]=7;ram[0x32]=230;sram[0xde]=6;sram[0x1df6]=30;
 for(int n=0;n<3;++n)a.frame(ram.data(),sram.data(),sram.size());
 for(unsigned egg=0;egg<eggs;++egg){sram[0xde]=6;a.frame(ram.data(),sram.data(),sram.size());sram[0xde]=5;--sram[0x1df6];a.frame(ram.data(),sram.data(),sram.size());a.frame(ram.data(),sram.data(),sram.size());}
 ram[0xb7b]=255;a.frame(ram.data(),sram.data(),sram.size());a.frame(ram.data(),sram.data(),sram.size());assert(a.unlocked(0,186926)==(eggs<=5));auto before_ram=ram,before_sram=sram;a.frame(ram.data(),sram.data(),sram.size());assert(before_ram==ram&&before_sram==sram);}
 
 // Failed writes retain unlocked state and retry without another unlock.
 {auto p=root/"write-failure.srm";auto j=p;j.replace_extension(".achievements.json");std::filesystem::remove(j);Runtime a;assert(a.start(0,p));std::filesystem::remove(j);std::filesystem::create_directory(j);
 std::fill(ram.begin(),ram.end(),0);ram[0x5b99]=64;ram[0x3b8]=0;a.frame(ram.data(),sram.data(),sram.size());ram[0x3b8]=4;a.frame(ram.data(),sram.data(),sram.size());ram[0x3b8]=5;a.frame(ram.data(),sram.data(),sram.size());assert(a.unlocked(0,1300));assert(!a.message().empty());std::filesystem::remove(j);assert(a.flush());assert(a.message().empty());}
 std::cout<<"63 rules parsed; slot isolation, restart, duplicate suppression, lock, corruption, ROM gate and Simple Pudding 5/6 eggs passed.\n";
}
