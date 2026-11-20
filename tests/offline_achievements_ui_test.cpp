#include "yi_achievements.h"
#include "notifications.h"
#include "imgui.h"
#include <SDL.h>
#include <filesystem>
#include <fstream>
#include <cassert>
#include <vector>
#include <iostream>
int main(int argc,char**argv){assert(argc==2);std::filesystem::path root=argv[1];std::filesystem::create_directories(root);auto ini=root/"achievements.ini",save=root/"save.srm",json=root/"save.achievements.json";std::filesystem::remove(ini);std::filesystem::remove(json);
 yiach::Notifications queue;assert(!queue.visible(0));
 queue.add("Achievements active","Offline",4000);queue.add("First unlock","First",5000);queue.add("Second unlock","Second",5000);
 assert(queue.visible(100)->title=="Achievements active");assert(queue.visible(4099)->title=="Achievements active");
 assert(queue.visible(4100)->title=="First unlock");assert(queue.visible(9099)->title=="First unlock");
 assert(queue.visible(9100)->title=="Second unlock");assert(!queue.visible(14100));queue.clear();assert(!queue.visible(15000));
 assert(SDL_Init(0)==0);ImGui::CreateContext();auto& io=ImGui::GetIO();io.DisplaySize=ImVec2(1100,850);io.DeltaTime=1.f/60;io.IniFilename=nullptr;unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
 YiAchievementsInit(ini.string().c_str());ImGui::NewFrame();ImGui::Begin("Launcher");YiAchievementsDrawLauncher("");ImGui::End();ImGui::Render();
 std::vector<uint8_t> ram(0x20000),sram(0x10000);YiAchievementsStart(0,save.string().c_str(),ram.data(),sram.data(),sram.size());YiAchievementsFrame();YiAchievementsShutdown();assert(!std::filesystem::exists(json));assert(!std::filesystem::exists(ini));
 {std::ofstream out(ini);out<<"[Achievements]\nEnabled=1\n";}YiAchievementsInit(ini.string().c_str());YiAchievementsStart(0,save.string().c_str(),ram.data(),sram.data(),sram.size());assert(std::filesystem::exists(json));
 SDL_Surface* image=SDL_CreateRGBSurfaceWithFormat(0,960,540,32,SDL_PIXELFORMAT_RGB888);assert(image);SDL_Renderer* renderer=SDL_CreateSoftwareRenderer(image);assert(renderer);
 SDL_SetRenderDrawColor(renderer,70,90,100,255);SDL_RenderClear(renderer);YiAchievementsPresent(renderer);
 assert(SDL_SaveBMP(image,(root/"startup-overlay.bmp").string().c_str())==0);
 YiAchievementsShutdown();SDL_DestroyRenderer(renderer);SDL_FreeSurface(image);ImGui::DestroyContext();SDL_Quit();std::cout<<"Launcher default off: no progress/settings file. Explicit opt-in creates JSON without account or ROM execution.\n";
}
