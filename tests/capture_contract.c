#include "yi_capture.h"
#include <stdio.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s (%s)\n", __LINE__, #c, SDL_GetError()); return 1; } } while (0)
static int check(int w, int h, int left, int top, int scale) {
  SDL_Surface *target=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_BGRA32);
  CHECK(target);
  SDL_Renderer *renderer=SDL_CreateSoftwareRenderer(target);
  CHECK(renderer);
  CHECK(SDL_RenderSetLogicalSize(renderer,8,6)==0);
  SDL_SetRenderDrawColor(renderer,0,0,0,255); SDL_RenderClear(renderer);
  SDL_SetRenderDrawColor(renderer,255,0,0,255); SDL_Rect scene={0,0,8,6}; SDL_RenderFillRect(renderer,&scene);
  SDL_SetRenderDrawColor(renderer,0,255,0,255); SDL_Rect marker={1,1,1,1}; SDL_RenderFillRect(renderer,&marker);
  SDL_Rect before,after; float sx,sy,ax,ay;
  SDL_RenderGetViewport(renderer,&before); SDL_RenderGetScale(renderer,&sx,&sy);
  SDL_Surface *shot=YiCaptureRenderer(renderer); CHECK(shot && shot->w==w && shot->h==h);
  for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
    Uint8 *p=(Uint8*)shot->pixels+y*shot->pitch+x*4;
    int inside=x>=left && x<left+8*scale && y>=top && y<top+6*scale;
    int green=inside && (x-left)/scale==1 && (y-top)/scale==1;
    CHECK(p[0]==0 && p[1]==(green?255:0) && p[2]==(inside&&!green?255:0) && p[3]==255);
  }
  SDL_RenderGetViewport(renderer,&after); SDL_RenderGetScale(renderer,&ax,&ay);
  CHECK(before.x==after.x && before.y==after.y && before.w==after.w && before.h==after.h && sx==ax && sy==ay);
  int lw,lh; SDL_RenderGetLogicalSize(renderer,&lw,&lh); CHECK(lw==8 && lh==6);
  CHECK(SDL_GetRenderTarget(renderer)==NULL);
  Uint8 red,green,blue,alpha; SDL_GetRenderDrawColor(renderer,&red,&green,&blue,&alpha);
  CHECK(red==0 && green==255 && blue==0 && alpha==255);
  SDL_FreeSurface(shot); SDL_DestroyRenderer(renderer); SDL_FreeSurface(target); return 0;
}
int main(void) {
  CHECK(SDL_Init(0)==0);
  CHECK(YiCaptureRenderer(NULL)==NULL);
  CHECK(check(12,6,2,0,1)==0); CHECK(check(8,10,0,2,1)==0);
  CHECK(check(8,6,0,0,1)==0); CHECK(check(16,12,0,0,2)==0);
  SDL_Quit(); puts("capture contract: horizontal/vertical letterbox, scale, opaque RGB and renderer state passed"); return 0;
}
