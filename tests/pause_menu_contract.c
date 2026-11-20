#include "yi_pause_menu.h"
#include <stdio.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);return 1;}}while(0)
static SDL_Event key(SDL_Keycode k,bool repeat) {
  SDL_Event e;memset(&e,0,sizeof(e));
  e.type=SDL_KEYDOWN;e.key.keysym.sym=k;e.key.keysym.mod=KMOD_NONE;e.key.repeat=repeat;return e;
}
int main(int argc,char **argv) {
  CHECK(SDL_Init(0)==0);
  SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,854,480,32,SDL_PIXELFORMAT_BGRA32);
  CHECK(surface);SDL_Renderer *r=SDL_CreateSoftwareRenderer(surface);CHECK(r);
  CHECK(SDL_RenderSetLogicalSize(r,342,224)==0);
  YiPauseMenu m={0};SDL_Event e=key(SDLK_ESCAPE,true);
  CHECK(YiPauseMenuEvent(&m,r,&e) && !m.open);
  e=key(SDLK_ESCAPE,false);CHECK(YiPauseMenuEvent(&m,r,&e) && m.open && !m.confirm && m.selected==0);
  e=key(SDLK_F11,false);CHECK(!YiPauseMenuEvent(&m,r,&e) && m.open);
  e=key(SDLK_RETURN,false);e.key.keysym.mod=KMOD_ALT;CHECK(!YiPauseMenuEvent(&m,r,&e) && m.open);
  Uint8 keys[SDL_NUM_SCANCODES]={0};CHECK(YiPauseMenuBlocksInput(&m,keys,NULL));
  e=key(SDLK_DOWN,false);YiPauseMenuEvent(&m,r,&e);CHECK(m.selected==1);
  e=key(SDLK_RETURN,false);CHECK(e.key.keysym.mod==KMOD_NONE);YiPauseMenuEvent(&m,r,&e);CHECK(m.open && m.confirm && !m.quit && m.selected==0);
  YiPauseMenuEvent(&m,r,&e);CHECK(m.open && !m.confirm && !m.quit && m.selected==0);
  e=key(SDLK_ESCAPE,false);YiPauseMenuEvent(&m,r,&e);CHECK(!m.open && !m.quit);
  keys[SDL_SCANCODE_ESCAPE]=1;CHECK(YiPauseMenuBlocksInput(&m,keys,NULL));
  keys[SDL_SCANCODE_ESCAPE]=0;keys[SDL_SCANCODE_LEFT]=1;CHECK(YiPauseMenuBlocksInput(&m,keys,NULL));
  keys[SDL_SCANCODE_LEFT]=0;CHECK(!YiPauseMenuBlocksInput(&m,keys,NULL));
  YiPauseMenuOpen(&m,false);e=key(SDLK_DOWN,false);YiPauseMenuEvent(&m,r,&e);
  e=key(SDLK_RETURN,false);YiPauseMenuEvent(&m,r,&e);
  e=key(SDLK_ESCAPE,false);YiPauseMenuEvent(&m,r,&e);CHECK(m.open && !m.confirm && m.selected==0);
  SDL_Event close={0};close.type=SDL_QUIT;YiPauseMenuEvent(&m,r,&close);CHECK(m.confirm && !m.quit && m.selected==0);
  e=key(SDLK_DOWN,false);YiPauseMenuEvent(&m,r,&e);e=key(SDLK_RETURN,false);YiPauseMenuEvent(&m,r,&e);CHECK(m.quit);
  YiPauseMenuOpen(&m,false);SDL_Rect panel,buttons[2];int unit;YiPauseMenuLayout(r,&panel,buttons,&unit);
  SDL_Event click={0};click.type=SDL_MOUSEBUTTONDOWN;click.button.button=SDL_BUTTON_LEFT;
  click.button.x=buttons[0].x+2;click.button.y=buttons[0].y+2;
  CHECK(YiPauseMenuEvent(&m,r,&click) && !m.open && !m.quit);
  YiPauseMenuOpen(&m,false);SDL_Event pad={0};pad.type=SDL_CONTROLLERBUTTONDOWN;pad.cbutton.button=SDL_CONTROLLER_BUTTON_DPAD_DOWN;
  YiPauseMenuEvent(&m,r,&pad);pad.cbutton.button=SDL_CONTROLLER_BUTTON_A;YiPauseMenuEvent(&m,r,&pad);CHECK(m.confirm && m.selected==0);
  pad.cbutton.button=SDL_CONTROLLER_BUTTON_B;YiPauseMenuEvent(&m,r,&pad);CHECK(m.open && !m.confirm);
  for(unsigned wide=0;wide<2;++wide) {
    CHECK(SDL_RenderSetLogicalSize(r,wide?2400:298,wide?1350:224)==0);
    SDL_SetRenderDrawColor(r,45,110,86,255);SDL_RenderClear(r);
    YiPauseMenuOpen(&m,false);
    SDL_Rect vp1,vp2;float x1,y1,x2,y2;SDL_RenderGetViewport(r,&vp1);SDL_RenderGetScale(r,&x1,&y1);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_ADD);SDL_SetRenderDrawColor(r,12,34,56,78);
    YiPauseMenuDraw(&m,r);
    Uint8 rr,gg,bb,aa;SDL_BlendMode blend;SDL_GetRenderDrawColor(r,&rr,&gg,&bb,&aa);SDL_GetRenderDrawBlendMode(r,&blend);
    SDL_RenderGetViewport(r,&vp2);SDL_RenderGetScale(r,&x2,&y2);
    CHECK(rr==12 && gg==34 && bb==56 && aa==78 && blend==SDL_BLENDMODE_ADD && !memcmp(&vp1,&vp2,sizeof(vp1)) && x1==x2 && y1==y2);
    SDL_RenderPresent(r);
    if(argc>1 && wide){char path[512];snprintf(path,sizeof(path),"%s-menu.bmp",argv[1]);CHECK(SDL_SaveBMP(surface,path)==0);
      m.confirm=true;SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);SDL_SetRenderDrawColor(r,45,110,86,255);SDL_RenderClear(r);
      YiPauseMenuDraw(&m,r);SDL_RenderPresent(r);snprintf(path,sizeof(path),"%s-confirm.bmp",argv[1]);CHECK(SDL_SaveBMP(surface,path)==0);}
  }
  SDL_DestroyRenderer(r);SDL_FreeSurface(surface);SDL_Quit();
  puts("PASS pause UI: keyboard/mouse/controller, safe defaults, repeat rejection, input release, confirm/cancel and native/wide renderer state.");return 0;
}
