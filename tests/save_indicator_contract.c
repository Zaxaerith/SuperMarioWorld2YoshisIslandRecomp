#include "yi_save_indicator.h"
#include <stdio.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);return 1;}}while(0)
static Uint8 ram[65536],before[65536];
int main(int argc,char **argv) {
  YiSaveIndicator s;YiSaveIndicatorLoaded(&s,ram,sizeof(ram));
  YiSaveIndicatorWritten(&s,ram,sizeof(ram),true,100);CHECK(!YiSaveIndicatorLabel(&s,101));
  ram[100]=77;YiSaveIndicatorWritten(&s,ram,sizeof(ram),true,200);CHECK(!YiSaveIndicatorLabel(&s,201));
  ram[0x7cd0+3+19]=0xe4;memcpy(before,ram,sizeof(ram));
  YiSaveIndicatorWritten(&s,ram,sizeof(ram),true,300);CHECK(!strcmp(YiSaveIndicatorLabel(&s,301),"SAVING..."));
  CHECK(!YiSaveIndicatorLabel(&s,1700) && !memcmp(ram,before,sizeof(ram)));
  ram[0x7cd0+3+20]=0x81;YiSaveIndicatorWritten(&s,ram,sizeof(ram),false,2000);
  CHECK(!strcmp(YiSaveIndicatorLabel(&s,2001),"SAVE ERROR"));CHECK(s.saved[0x7cd0+3+20-YI_SAVE_BEGIN]==0);
  YiSaveIndicatorWritten(&s,ram,sizeof(ram),true,2200);CHECK(!s.failed && !strcmp(YiSaveIndicatorLabel(&s,2201),"SAVING..."));
  CHECK(SDL_Init(0)==0);SDL_Surface *image=SDL_CreateRGBSurfaceWithFormat(0,854,480,32,SDL_PIXELFORMAT_BGRA32);CHECK(image);
  SDL_Renderer *r=SDL_CreateSoftwareRenderer(image);CHECK(r);
  for(unsigned wide=0;wide<2;++wide) {
    CHECK(SDL_RenderSetLogicalSize(r,wide?2400:298,wide?1350:224)==0);
    SDL_SetRenderDrawColor(r,45,110,86,255);SDL_RenderClear(r);
    SDL_Rect a,b;float ax,ay,bx,by;SDL_RenderGetViewport(r,&a);SDL_RenderGetScale(r,&ax,&ay);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_ADD);SDL_SetRenderDrawColor(r,12,34,56,78);
    YiSaveIndicatorDraw(&s,r,2201);Uint8 rr,gg,bb,aa;SDL_BlendMode mode;
    SDL_GetRenderDrawColor(r,&rr,&gg,&bb,&aa);SDL_GetRenderDrawBlendMode(r,&mode);
    SDL_RenderGetViewport(r,&b);SDL_RenderGetScale(r,&bx,&by);
    CHECK(rr==12 && gg==34 && bb==56 && aa==78 && mode==SDL_BLENDMODE_ADD && ax==bx && ay==by && !memcmp(&a,&b,sizeof(a)));
    SDL_RenderPresent(r);if(argc>1 && wide)CHECK(SDL_SaveBMP(image,argv[1])==0);
  }
  SDL_DestroyRenderer(r);SDL_FreeSurface(image);SDL_Quit();
  puts("PASS save indicator: no volatile/unchanged flashes, persistence success/error/retry, expiry, unchanged SRAM and native/wide renderer state.");return 0;
}
