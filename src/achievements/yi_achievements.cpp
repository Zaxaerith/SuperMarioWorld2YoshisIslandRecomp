#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "yi_achievements.h"
#include "runtime.h"
#include "notifications.h"
extern "C" {
#include "../rom_profile.h"
}
#include "imgui.h"
#include <SDL.h>
#include <fstream>
#include <sstream>
namespace {
bool enabled=false,overview=false;unsigned selected=0,display_slot=0;int profile=-1;
std::filesystem::path preferences;std::unique_ptr<yiach::Runtime> runtime;
const uint8_t* ram=nullptr;const uint8_t* sram=nullptr;unsigned size=0;
std::string status,texture_key;yiach::Notifications notifications;SDL_Texture* popup_texture=nullptr;
std::wstring wide(const std::string &text) {
  int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring out(n ? n : 1, L'\0');
  if (n)
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), n);
  return out;
}
// Host-only overlay: never changes the SNES framebuffer or guest memory.
SDL_Texture *make_texture(SDL_Renderer *renderer, const std::string &title,
                          const std::string &description) {
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = 900;
  info.bmiHeader.biHeight = -112;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  void *pixels = nullptr;
  HDC dc = CreateCompatibleDC(nullptr);
  HBITMAP bitmap =
      CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  if (!dc || !bitmap || !pixels) {
    if (bitmap)
      DeleteObject(bitmap);
    if (dc)
      DeleteDC(dc);
    return nullptr;
  }
  auto old = SelectObject(dc, bitmap);
  RECT bounds{0, 0, 900, 112};
  HBRUSH brush = CreateSolidBrush(RGB(20, 27, 38));
  FillRect(dc, &bounds, brush);
  DeleteObject(brush);
  SetBkMode(dc, TRANSPARENT);
  auto draw = [&](const std::string &text, int size, int y, COLORREF color) {
    HFONT font =
        CreateFontW(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
    auto previous = SelectObject(dc, font);
    SetTextColor(dc, color);
    RECT area{18, y, 880, y + 42};
    auto value = wide(text);
    DrawTextW(dc, value.c_str(), -1, &area,
              DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, previous);
    DeleteObject(font);
  };
  draw(title, 30, 12, RGB(245, 208, 86));
  draw(description, 24, 61, RGB(238, 240, 245));
  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
      pixels, 900, 112, 32, 900 * 4, SDL_PIXELFORMAT_RGB888);
  SDL_Texture *result =
      surface ? SDL_CreateTextureFromSurface(renderer, surface) : nullptr;
  if (surface)
    SDL_FreeSurface(surface);
  if (result)
    SDL_SetTextureBlendMode(result, SDL_BLENDMODE_NONE);
  SelectObject(dc, old);
  DeleteObject(bitmap);
  DeleteDC(dc);
  return result;
}

}
extern "C" void YiAchievementsInit(const char* path){
 notifications.clear();status.clear();overview=false;preferences=std::filesystem::u8path(path?path:"");enabled=false;std::ifstream in(preferences);std::string value;
 while(std::getline(in,value))if(value=="Enabled=1"||value=="Enabled=1\r")enabled=true;
}
extern "C" void YiAchievementsDrawLauncher(const char* rom){
 ImGui::SeparatorText("Achievements");
 if(ImGui::Checkbox("Enable Achievements",&enabled)){
  if(!yiach::atomic_write(preferences,std::string("[Achievements]\nEnabled=")+(enabled?"1\n":"0\n")))status="Setting could not be saved.";
 }
 if(enabled){
  ImGui::TextWrapped("Local achievements create saves/save.achievements.json beside your save. Copy both files to keep your progress. No account or internet required.");
  ImGui::TextDisabled("63 local challenges. F6: overview; F10 / Shift+F10: next / previous.");
  ImGui::TextWrapped("USA rules; European ROM compatibility is experimental. Existing special challenges cannot be recovered from an imported SRAM save.");
  static std::string cached_rom;static int cached_profile=-1;static std::filesystem::file_time_type cached_stamp{};
  std::error_code ec;auto rom_path=std::filesystem::u8path(rom?rom:"");auto stamp=std::filesystem::last_write_time(rom_path,ec);
  if(cached_rom!=(rom?rom:"")||(!ec&&stamp!=cached_stamp)){
   cached_rom=rom?rom:"";cached_stamp=stamp;cached_profile=-1;
   std::ifstream file(rom_path,std::ios::binary|std::ios::ate);if(file){auto length=file.tellg();if(length==YI_ROM_SIZE||length==YI_ROM_SIZE+512){file.seekg(length==YI_ROM_SIZE?0:512);std::vector<uint8_t> payload(YI_ROM_SIZE);if(file.read((char*)payload.data(),payload.size()))cached_profile=YiIdentifyRom(payload.data(),payload.size());}}
  }
  if(cached_profile<0)ImGui::TextWrapped("Select a supported ROM to view local progress.");
  else if(ImGui::TreeNode("Local achievement list")){
   for(unsigned slot=0;slot<3;++slot){if(slot)ImGui::SameLine();std::string label="Slot "+std::to_string(slot+1);if(ImGui::RadioButton(label.c_str(),display_slot==slot))display_slot=slot;}
   yiach::Runtime view;bool ok=view.inspect(cached_profile,preferences.parent_path()/"saves/save.srm");
   if(!ok)ImGui::TextWrapped("Local progress cannot be read; file left unchanged.");
   else {ImGui::Text("Unlocked: %u / 63",view.count(display_slot));
    if(ImGui::BeginChild("achievement-list",ImVec2(0,180),true))for(const auto& d:yiach::Catalog){ImGui::TextWrapped("%s %s",view.unlocked(display_slot,d.id)?"[Unlocked]":"[Locked]",d.title);ImGui::TextWrapped("%s",d.description);ImGui::Separator();}ImGui::EndChild();}
   ImGui::TreePop();
  }

 }
 if(!status.empty())ImGui::TextWrapped("%s",status.c_str());
}
extern "C" void YiAchievementsStart(int p,const char* save,const uint8_t* r,const uint8_t* cart,unsigned n){
 if(!enabled)return;profile=p;ram=r;sram=cart;size=n;runtime=std::make_unique<yiach::Runtime>();
 if(!runtime->start(p,std::filesystem::u8path(save))){status=runtime->message();runtime.reset();}
 else notifications.add("Achievements active","63 local challenges | F6: overview",4000);
}
extern "C" void YiAchievementsFrame(){
 if(!enabled||!runtime)return;runtime->frame(ram,sram,size);
 while(unsigned id=runtime->take_notification())
  for(const auto& d:yiach::Catalog)if(d.id==id)notifications.add(std::string("Unlocked: ")+d.title,d.description);
}
extern "C" void YiAchievementsOverview(int action){if(!enabled)return;if(!action)overview=!overview;else{overview=true;selected=(selected+(action>0?1:62))%63;}}
extern "C" void YiAchievementsPresent(SDL_Renderer* renderer){
 if(enabled){std::string title,description;
  if(!status.empty()){title="Achievements unavailable";description=status;}
  else if(runtime&&!runtime->message().empty()){title="Achievements save error";description=runtime->message();}
  else if(const auto* notification=notifications.visible(SDL_GetTicks64())){title=notification->title;description=notification->description;}
  else if(overview&&runtime){const auto& d=yiach::Catalog[selected];int slot=runtime->slot();
   title="Slot "+std::to_string(slot+1)+" | "+std::to_string(selected+1)+"/63 "+(slot>=0&&runtime->unlocked(slot,d.id)?"[Unlocked] ":"[Locked] ")+d.title;description=d.description;
  }
  if(!title.empty()){
   auto key=title+'\n'+description;if(key!=texture_key||!popup_texture){if(popup_texture)SDL_DestroyTexture(popup_texture);popup_texture=make_texture(renderer,title,description);texture_key=key;}
   int w=0,h=0;SDL_RenderGetLogicalSize(renderer,&w,&h);if(w<=0||h<=0)SDL_GetRendererOutputSize(renderer,&w,&h);
   const int width=w*3/5;
   SDL_Rect box{w/30,h/30,width,width*112/900};if(popup_texture)SDL_RenderCopy(renderer,popup_texture,nullptr,&box);
  }
 }
 SDL_RenderPresent(renderer);
}
extern "C" void YiAchievementsShutdown(){notifications.clear();if(runtime)runtime->flush();runtime.reset();if(popup_texture)SDL_DestroyTexture(popup_texture);popup_texture=nullptr;}
