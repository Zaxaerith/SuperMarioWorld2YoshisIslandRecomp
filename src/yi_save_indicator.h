#ifndef YI_SAVE_INDICATOR_H
#define YI_SAVE_INDICATOR_H
#include <SDL.h>
#include <stdbool.h>
#include <string.h>
/* Yoshi's permanent files, backups and checksums. Exclude volatile GSU RAM. */
enum { YI_SAVE_BEGIN=0x7c00, YI_SAVE_END=0x7e7c, YI_SAVE_SLOT_SIZE=104, YI_SAVE_PAYLOAD_SIZE=3*YI_SAVE_SLOT_SIZE };

typedef struct YiSaveIndicator {
  Uint8 saved[YI_SAVE_PAYLOAD_SIZE];
  bool initialized,failed;
  Uint64 until;
} YiSaveIndicator;
static unsigned YiSaveIndicatorWord(const Uint8 *p) {return p[0]|((unsigned)p[1]<<8);}
static bool YiSaveIndicatorSlotValid(const Uint8 *ram,unsigned data,unsigned checksum) {
  unsigned sum=0;for(unsigned i=0;i<YI_SAVE_SLOT_SIZE;i+=2)sum+=YiSaveIndicatorWord(ram+data+i);
  return ((0x7777u-sum)&65535u)==YiSaveIndicatorWord(ram+checksum);
}
/* The game can temporarily invalidate a primary while its native backup is
 * valid. Compare recoverable slot contents, not scratch changes/checksums. */
static bool YiSaveIndicatorPayload(const Uint8 *ram,size_t size,Uint8 *out) {
  if(!ram || size<YI_SAVE_END)return false;
  for(unsigned slot=0;slot<3;++slot){
    unsigned primary=YI_SAVE_BEGIN+slot*YI_SAVE_SLOT_SIZE;
    unsigned backup=0x7d38+slot*YI_SAVE_SLOT_SIZE;
    unsigned source;
    if(YiSaveIndicatorSlotValid(ram,primary,0x7e70+slot*2))source=primary;
    else if(YiSaveIndicatorSlotValid(ram,backup,0x7e76+slot*2))source=backup;
    else return false;
    memcpy(out+slot*YI_SAVE_SLOT_SIZE,ram+source,YI_SAVE_SLOT_SIZE);
  }
  return true;
}
static void YiSaveIndicatorLoaded(YiSaveIndicator *s,const Uint8 *ram,size_t size) {
  memset(s,0,sizeof(*s));
  s->initialized=YiSaveIndicatorPayload(ram,size,s->saved);
}
static void YiSaveIndicatorWritten(YiSaveIndicator *s,const Uint8 *ram,size_t size,bool success,Uint64 now) {
  Uint8 payload[YI_SAVE_PAYLOAD_SIZE];
  if(!success || !YiSaveIndicatorPayload(ram,size,payload)){s->failed=true;s->until=now+5000;return;}
  bool changed=!s->initialized || memcmp(s->saved,payload,sizeof(payload));
  memcpy(s->saved,payload,sizeof(payload));s->initialized=true;
  if(changed || s->failed){s->failed=false;s->until=now+1400;}
}
static const char *YiSaveIndicatorLabel(const YiSaveIndicator *s,Uint64 now) {
  return now<s->until?(s->failed?"SAVE ERROR":"SAVING..."):NULL;
}
static void YiSaveIndicatorDraw(const YiSaveIndicator *s,SDL_Renderer *r,Uint64 now) {
  const char *label=YiSaveIndicatorLabel(s,now);if(!label)return;
  int w,h;SDL_RenderGetLogicalSize(r,&w,&h);if(w<=0 || h<=0)SDL_GetRendererOutputSize(r,&w,&h);
  int scale=SDL_max(1,SDL_min(w/320,h/224)/2),width=(int)strlen(label)*6*scale+6*scale;
  SDL_Rect box={w-width-4*scale,h-15*scale,width,11*scale};
  Uint8 rr,gg,bb,aa;SDL_BlendMode blend;SDL_GetRenderDrawColor(r,&rr,&gg,&bb,&aa);SDL_GetRenderDrawBlendMode(r,&blend);
  SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(r,0,0,0,210);SDL_RenderFillRect(r,&box);
  SDL_SetRenderDrawColor(r,s->failed?255:235,s->failed?100:245,s->failed?100:220,255);
  static const char letters[]="SAVINGERO.";
  static const Uint8 glyphs[10][5]={{38,73,73,73,50},{126,17,17,17,126},{31,32,64,32,31},
    {0,65,127,65,0},{127,4,8,16,127},{62,65,73,73,122},{127,73,73,73,65},
    {127,9,25,41,70},{62,65,65,65,62},{0,96,96,0,0}};
  int x=box.x+3*scale,y=box.y+2*scale;
  for(const char *p=label;*p;++p,x+=6*scale) {
    const char *g=strchr(letters,*p);if(!g)continue;
    for(int col=0;col<5;++col)for(int row=0;row<7;++row)if(glyphs[g-letters][col]&(1<<row)) {
      SDL_Rect dot={x+col*scale,y+row*scale,scale,scale};SDL_RenderFillRect(r,&dot);
    }
  }
  SDL_SetRenderDrawBlendMode(r,blend);SDL_SetRenderDrawColor(r,rr,gg,bb,aa);
}
#endif
