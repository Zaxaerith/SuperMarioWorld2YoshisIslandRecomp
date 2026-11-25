#include "yi_save_indicator.h"
#include <stdio.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);return 1;}}while(0)
static Uint8 ram[65536],before[65536];
static void checksums(void) {
 for(unsigned slot=0;slot<3;++slot){unsigned o=0x7c00+104*slot,sum=0;for(unsigned i=0;i<104;i+=2)sum+=ram[o+i]|((unsigned)ram[o+i+1]<<8);
 unsigned checksum=(0x7777u-sum)&65535u;ram[0x7e70+2*slot]=checksum;ram[0x7e71+2*slot]=checksum>>8;
 memcpy(ram+0x7d38+104*slot,ram+o,104);ram[0x7e76+2*slot]=checksum;ram[0x7e77+2*slot]=checksum>>8;
 }
}
int main(int argc,char **argv) {
 checksums();
  YiSaveIndicator s;YiSaveIndicatorLoaded(&s,ram,sizeof(ram));
  YiSaveIndicatorWritten(&s,ram,sizeof(ram),true,100);CHECK(!YiSaveIndicatorLabel(&s,101));
  ram[100]=77;YiSaveIndicatorWritten(&s,ram,sizeof(ram),true,200);CHECK(!YiSaveIndicatorLabel(&s,201));
  ram[0x7cd0+3+19]=0xe4;checksums();memcpy(before,ram,sizeof(ram));
  YiSaveIndicatorWritten(&s,ram,sizeof(ram),true,300);CHECK(!strcmp(YiSaveIndicatorLabel(&s,301),"SAVED"));
  CHECK(!YiSaveIndicatorLabel(&s,1700) && !memcmp(ram,before,sizeof(ram)));
  ram[0x7cd0+3+20]=0x81;checksums();YiSaveIndicatorWritten(&s,ram,sizeof(ram),false,2000);
  CHECK(!strcmp(YiSaveIndicatorLabel(&s,2001),"SAVE ERROR"));CHECK(s.saved[0x7cd0+3+20-YI_SAVE_BEGIN]==0);
  YiSaveIndicatorWritten(&s,ram,sizeof(ram),true,2200);CHECK(!s.failed && !strcmp(YiSaveIndicatorLabel(&s,2201),"SAVED"));
  /* Primary temporarily invalid, native backup valid: no second success. */
  YiSaveIndicatorLoaded(&s,ram,sizeof(ram));Uint8 old=ram[0x7cd0];ram[0x7cd0]^=1;
  YiSaveIndicatorWritten(&s,ram,sizeof(ram),true,4000);CHECK(!YiSaveIndicatorLabel(&s,4001));
  ram[0x7cd0]=old;YiSaveIndicatorWritten(&s,ram,sizeof(ram),true,5000);CHECK(!YiSaveIndicatorLabel(&s,5001));
  ram[0x7cd0]^=1;ram[0x7e08]^=1;YiSaveIndicatorWritten(&s,ram,sizeof(ram),true,6000);CHECK(!YiSaveIndicatorLabel(&s,6001) && !s.failed);
  ram[0x7cd0]=old;ram[0x7e08]=old;YiSaveIndicatorWritten(&s,ram,sizeof(ram),true,7000);CHECK(!YiSaveIndicatorLabel(&s,7001));
  /* A successful write of virgin SRAM is not a file error. */
  for(unsigned fill=0;fill<2;++fill) {
    memset(ram,fill?0xff:0,sizeof(ram));YiSaveIndicatorLoaded(&s,ram,sizeof(ram));
    CHECK(!s.valid_slots);
    YiSaveIndicatorWritten(&s,ram,sizeof(ram),true,8000);CHECK(!s.failed&&!YiSaveIndicatorLabel(&s,8001));
    YiSaveIndicatorWritten(&s,ram,sizeof(ram),false,8100);CHECK(s.failed&&!strcmp(YiSaveIndicatorLabel(&s,8101),"SAVE ERROR"));
    YiSaveIndicatorWritten(&s,ram,sizeof(ram),true,8200);CHECK(!s.failed&&!YiSaveIndicatorLabel(&s,8201));
  }
  /* Slots with no checksummed data must not hide changes in another slot. */
  memset(ram,0,sizeof(ram));checksums();
  ram[0x7e70]^=1;ram[0x7e76]^=1;
  YiSaveIndicatorLoaded(&s,ram,sizeof(ram));CHECK(s.valid_slots==6);
  ram[0x7cd0+3+21]=0x82;checksums();ram[0x7e70]^=1;ram[0x7e76]^=1;
  memcpy(before,ram,sizeof(ram));YiSaveIndicatorWritten(&s,ram,sizeof(ram),true,9000);
  CHECK(!s.failed&&!strcmp(YiSaveIndicatorLabel(&s,9001),"SAVED"));
  CHECK(!memcmp(before,ram,sizeof(ram))&&s.valid_slots==6);
  YiSaveIndicatorWritten(&s,ram,sizeof(ram),true,11000);CHECK(!YiSaveIndicatorLabel(&s,11001));
  /* Empty-slot initialization is real valid data, followed by no duplicates. */
  checksums();YiSaveIndicatorWritten(&s,ram,sizeof(ram),true,12000);
  CHECK(s.valid_slots==7&&!strcmp(YiSaveIndicatorLabel(&s,12001),"SAVED"));
  YiSaveIndicatorWritten(&s,ram,sizeof(ram),true,14000);CHECK(!YiSaveIndicatorLabel(&s,14001));
  YiSaveIndicatorWritten(&s,ram,sizeof(ram),false,15000);
  CHECK(!strcmp(YiSaveIndicatorLabel(&s,15001),"SAVE ERROR"));
  YiSaveIndicatorWritten(&s,ram,sizeof(ram),true,15100);
  CHECK(!s.failed&&!strcmp(YiSaveIndicatorLabel(&s,15101),"SAVED"));
  CHECK(SDL_Init(0)==0);SDL_Surface *image=SDL_CreateRGBSurfaceWithFormat(0,854,480,32,SDL_PIXELFORMAT_BGRA32);CHECK(image);
  SDL_Renderer *r=SDL_CreateSoftwareRenderer(image);CHECK(r);
  for(unsigned wide=0;wide<2;++wide) {
    CHECK(SDL_RenderSetLogicalSize(r,wide?2400:298,wide?1350:224)==0);
    SDL_SetRenderDrawColor(r,45,110,86,255);SDL_RenderClear(r);
    SDL_Rect a,b;float ax,ay,bx,by;SDL_RenderGetViewport(r,&a);SDL_RenderGetScale(r,&ax,&ay);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_ADD);SDL_SetRenderDrawColor(r,12,34,56,78);
    YiSaveIndicatorDraw(&s,r,15101);Uint8 rr,gg,bb,aa;SDL_BlendMode mode;
    SDL_GetRenderDrawColor(r,&rr,&gg,&bb,&aa);SDL_GetRenderDrawBlendMode(r,&mode);
    SDL_RenderGetViewport(r,&b);SDL_RenderGetScale(r,&bx,&by);
    CHECK(rr==12 && gg==34 && bb==56 && aa==78 && mode==SDL_BLENDMODE_ADD && ax==bx && ay==by && !memcmp(&a,&b,sizeof(a)));
    SDL_RenderPresent(r);if(argc>1 && wide)CHECK(SDL_SaveBMP(image,argv[1])==0);
  }
  SDL_DestroyRenderer(r);SDL_FreeSurface(image);SDL_Quit();
  puts("PASS save indicator: no volatile/unchanged flashes, persistence success/error/retry, expiry, unchanged SRAM and native/wide renderer state.");return 0;
}
