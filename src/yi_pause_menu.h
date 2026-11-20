#ifndef YI_PAUSE_MENU_H
#define YI_PAUSE_MENU_H
#include <SDL.h>
#include <stdbool.h>
#include <string.h>

/* Host UI only: no ROM, save-state, renderer aspect or guest input binding changes. */
typedef struct YiPauseMenu { bool open, confirm, quit, release_input; int selected; } YiPauseMenu;
static void YiPauseMenuOpen(YiPauseMenu *m,bool confirm) {
  m->open=true;m->confirm=confirm;m->selected=0;m->quit=false;
}
static void YiPauseMenuActivate(YiPauseMenu *m) {
  if(!m->selected) {
    if(m->confirm){m->confirm=false;m->selected=0;}
    else {m->open=false;m->release_input=true;}
  } else if(m->confirm) m->quit=true;
  else {m->confirm=true;m->selected=0;}
}
static void YiPauseMenuLayout(SDL_Renderer *r,SDL_Rect *panel,SDL_Rect buttons[2],int *unit) {
  int w,h;SDL_RenderGetLogicalSize(r,&w,&h);
  if(w<=0 || h<=0)SDL_GetRendererOutputSize(r,&w,&h);
  int u=SDL_min(w/300,h/224);if(u<1)u=1;*unit=u;
  int pw=SDL_min(248*u,w-16*u),ph=158*u;
  *panel=(SDL_Rect){(w-pw)/2,(h-ph)/2,pw,ph};
  for(int i=0;i<2;++i)buttons[i]=(SDL_Rect){panel->x+16*u,panel->y+(55+42*i)*u,pw-32*u,34*u};
}
static bool YiPauseMenuEvent(YiPauseMenu *m,SDL_Renderer *r,const SDL_Event *e) {
  if(e->type==SDL_QUIT){YiPauseMenuOpen(m,true);return true;}
  if(!m->open) {
    if(e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_ESCAPE) {
      if(!e->key.repeat)YiPauseMenuOpen(m,false);
      return true;
    }
    return false;
  }
  if(e->type==SDL_KEYDOWN) {
    if(e->key.repeat)return true;
    SDL_Keycode key=e->key.keysym.sym;
    if(key==SDLK_ESCAPE) {
      if(m->confirm){m->confirm=false;m->selected=0;}
      else {m->open=false;m->release_input=true;}
    } else if(key==SDLK_UP || key==SDLK_DOWN || key==SDLK_LEFT || key==SDLK_RIGHT)m->selected^=1;
    else if(key==SDLK_RETURN || key==SDLK_SPACE)YiPauseMenuActivate(m);
    return true;
  }
  if(e->type==SDL_KEYUP)return true;
  if(e->type==SDL_CONTROLLERBUTTONDOWN) {
    unsigned b=e->cbutton.button;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_UP || b==SDL_CONTROLLER_BUTTON_DPAD_DOWN)m->selected^=1;
    else if(b==SDL_CONTROLLER_BUTTON_A)YiPauseMenuActivate(m);
    else if(b==SDL_CONTROLLER_BUTTON_B || b==SDL_CONTROLLER_BUTTON_START) {
      if(m->confirm){m->confirm=false;m->selected=0;}
      else {m->open=false;m->release_input=true;}
    }
    return true;
  }
  if(e->type==SDL_CONTROLLERBUTTONUP)return true;
  if(e->type==SDL_MOUSEMOTION || (e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT)) {
    SDL_Rect panel,buttons[2];int unit;YiPauseMenuLayout(r,&panel,buttons,&unit);
    SDL_Point point={e->type==SDL_MOUSEMOTION?e->motion.x:e->button.x,
                     e->type==SDL_MOUSEMOTION?e->motion.y:e->button.y};
    for(int i=0;i<2;++i)if(SDL_PointInRect(&point,&buttons[i])) {
      m->selected=i;if(e->type==SDL_MOUSEBUTTONDOWN)YiPauseMenuActivate(m);break;
    }
    return true;
  }
  return false;
}
static bool YiPauseMenuBlocksInput(YiPauseMenu *m,const Uint8 *keys,SDL_GameController *pad) {
  if(m->open)return true;
  if(m->release_input) {
    bool held=keys && (keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE] ||
                      keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_DOWN]);
    if(pad)held|=SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_A) ||
        SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_B) || SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_START);
    if(!held)m->release_input=false;
  }
  return m->release_input;
}
static void YiPauseMenuText(SDL_Renderer *r,const char *text,int center,int y,int scale) {
  /* Original small bitmap alphabet; no external fonts/assets or UI context. */
  static const Uint8 letters[26][5]={
    {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},
    {127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},
    {0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
    {127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},
    {62,65,81,33,94},{127,9,25,41,70},{38,73,73,73,50},{1,1,127,1,1},
    {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},
    {7,8,112,8,7},{97,81,73,69,67}};
  static const Uint8 question[5]={2,1,81,9,6},colon[5]={0,54,54,0,0};
  int x=center-(int)strlen(text)*6*scale/2;
  for(;*text;++text,x+=6*scale) {
    const Uint8 *glyph=*text>='A' && *text<='Z'?letters[*text-'A']:*text=='?'?question:*text==':'?colon:NULL;
    if(!glyph)continue;
    for(int col=0;col<5;++col)for(int row=0;row<7;++row)if(glyph[col]&(1<<row)) {
      SDL_Rect dot={x+col*scale,y+row*scale,scale,scale};SDL_RenderFillRect(r,&dot);
    }
  }
}
static void YiPauseMenuDraw(const YiPauseMenu *m,SDL_Renderer *r) {
  if(!m->open)return;
  Uint8 red,green,blue,alpha;SDL_BlendMode blend;
  SDL_GetRenderDrawColor(r,&red,&green,&blue,&alpha);SDL_GetRenderDrawBlendMode(r,&blend);
  SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(r,0,0,0,150);SDL_RenderFillRect(r,NULL);
  SDL_Rect panel,buttons[2];int u;YiPauseMenuLayout(r,&panel,buttons,&u);
  SDL_SetRenderDrawColor(r,105,68,34,255);SDL_RenderFillRect(r,&panel);
  SDL_Rect inside={panel.x+4*u,panel.y+4*u,panel.w-8*u,panel.h-8*u};
  SDL_SetRenderDrawColor(r,18,62,45,255);SDL_RenderFillRect(r,&inside);
  SDL_SetRenderDrawColor(r,245,243,209,255);
  YiPauseMenuText(r,m->confirm?"SPIEL BEENDEN?":"PAUSE",panel.x+panel.w/2,panel.y+16*u,(m->confirm?2:3)*u);
  for(int i=0;i<2;++i) {
    SDL_SetRenderDrawColor(r,i==m->selected?59:23,i==m->selected?108:73,i==m->selected?65:50,255);
    SDL_RenderFillRect(r,&buttons[i]);
    SDL_SetRenderDrawColor(r,i==m->selected?255:100,i==m->selected?220:128,i==m->selected?115:100,255);
    SDL_RenderDrawRect(r,&buttons[i]);SDL_SetRenderDrawColor(r,255,255,235,255);
    YiPauseMenuText(r,i?"BEENDEN":m->confirm?"ABBRECHEN":"FORTSETZEN",panel.x+panel.w/2,buttons[i].y+10*u,2*u);
  }
  SDL_SetRenderDrawColor(r,185,204,179,255);
  YiPauseMenuText(r,m->confirm?"ESC: ABBRECHEN":"ESC: FORTSETZEN",panel.x+panel.w/2,panel.y+141*u,u);
  SDL_SetRenderDrawBlendMode(r,blend);SDL_SetRenderDrawColor(r,red,green,blue,alpha);
}
#endif
