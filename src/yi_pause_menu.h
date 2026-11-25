#ifndef YI_PAUSE_MENU_H
#define YI_PAUSE_MENU_H
#include <SDL.h>
#include <stdbool.h>
#include <string.h>

/* Host overlay; native actions are queued separately at a frame boundary. */
typedef struct YiPauseMenu { bool open, confirm, quit, release_input, return_confirm, restart, exit_level, can_exit_level; int selected; } YiPauseMenu;
/* Machine reset is portable; window and renderer stay alive. */
static bool YiPauseMenuTitleSupported(void) { return true; }
static int YiPauseMenuButtonCount(const YiPauseMenu *m) {
  return m->confirm ? 2 : 4;
}
static void YiPauseMenuOpen(YiPauseMenu *m,bool confirm) {
  m->open=true;m->confirm=confirm;m->selected=0;m->quit=false;m->restart=false;m->return_confirm=false;m->exit_level=false;
}
static void YiPauseMenuActivate(YiPauseMenu *m) {
  if(!m->confirm && m->selected==2) {
    if(m->can_exit_level){m->exit_level=true;m->open=false;m->release_input=true;}
    return;
  }
  if(!m->selected) {
    if(m->confirm){m->confirm=false;m->selected=0;}
    else {m->open=false;m->release_input=true;}
  } else if(m->confirm) {if(m->return_confirm)m->restart=true;else m->quit=true;}
  else {m->return_confirm=YiPauseMenuTitleSupported() && m->selected==1;m->confirm=true;m->selected=0;}
}
static void YiPauseMenuLayout(const YiPauseMenu *m,SDL_Renderer *r,SDL_Rect *panel,SDL_Rect buttons[4],int *unit) {
  int w,h;SDL_RenderGetLogicalSize(r,&w,&h);
  if(w<=0 || h<=0)SDL_GetRendererOutputSize(r,&w,&h);
  int u=SDL_min(w/300,h/224);if(u<1)u=1;*unit=u;
  int pw=SDL_min((m->confirm&&m->return_confirm?180:140)*u,w-8*u),ph=(m->confirm?(m->return_confirm?103:79):121)*u;
  *panel=(SDL_Rect){(w-pw)/2,(h-ph)/2,pw,ph};
  for(int i=0;i<YiPauseMenuButtonCount(m);++i)buttons[i]=(SDL_Rect){panel->x+8*u,panel->y+((m->confirm&&m->return_confirm?51:27)+21*i)*u,pw-16*u,17*u};
}
static bool YiPauseMenuEvent(YiPauseMenu *m,SDL_Renderer *r,const SDL_Event *e) {
  if(e->type==SDL_QUIT){m->quit=true;m->restart=false;m->exit_level=false;return true;}
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
    if(key==SDLK_F9 || key==SDLK_F11 || (key==SDLK_RETURN && (e->key.keysym.mod&KMOD_ALT)))return false;
    if(key==SDLK_ESCAPE) {
      if(m->confirm){m->confirm=false;m->selected=0;}
      else {m->open=false;m->release_input=true;}
    } else if(key==SDLK_UP || key==SDLK_DOWN || key==SDLK_LEFT || key==SDLK_RIGHT)m->selected=(m->selected+((key==SDLK_UP||key==SDLK_LEFT)?(YiPauseMenuButtonCount(m)-1):1))%YiPauseMenuButtonCount(m);
    else if(key==SDLK_RETURN || key==SDLK_SPACE)YiPauseMenuActivate(m);
    return true;
  }
  if(e->type==SDL_KEYUP)return true;
  if(e->type==SDL_CONTROLLERBUTTONDOWN) {
    unsigned b=e->cbutton.button;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_UP || b==SDL_CONTROLLER_BUTTON_DPAD_DOWN)m->selected=(m->selected+(b==SDL_CONTROLLER_BUTTON_DPAD_UP?(YiPauseMenuButtonCount(m)-1):1))%YiPauseMenuButtonCount(m);
    else if(b==SDL_CONTROLLER_BUTTON_A)YiPauseMenuActivate(m);
    else if(b==SDL_CONTROLLER_BUTTON_B || b==SDL_CONTROLLER_BUTTON_START) {
      if(m->confirm){m->confirm=false;m->selected=0;}
      else {m->open=false;m->release_input=true;}
    }
    return true;
  }
  if(e->type==SDL_CONTROLLERBUTTONUP)return true;
  if(e->type==SDL_MOUSEMOTION || (e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT)) {
    SDL_Rect panel,buttons[4];int unit;YiPauseMenuLayout(m,r,&panel,buttons,&unit);
    SDL_Point point={e->type==SDL_MOUSEMOTION?e->motion.x:e->button.x,
                     e->type==SDL_MOUSEMOTION?e->motion.y:e->button.y};
    for(int i=0;i<YiPauseMenuButtonCount(m);++i)if(SDL_PointInRect(&point,&buttons[i])) {
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
                      keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_RIGHT]);
    if(pad)held|=SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_A) ||
        SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_B) || SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_START) ||
        SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_DPAD_UP) || SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_DPAD_DOWN) ||
        SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_DPAD_LEFT) || SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
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
  SDL_Rect panel,buttons[4];int u;YiPauseMenuLayout(m,r,&panel,buttons,&u);
  SDL_SetRenderDrawColor(r,105,68,34,255);SDL_RenderFillRect(r,&panel);
  SDL_Rect inside={panel.x+2*u,panel.y+2*u,panel.w-4*u,panel.h-4*u};
  SDL_SetRenderDrawColor(r,18,62,45,255);SDL_RenderFillRect(r,&inside);
  SDL_SetRenderDrawColor(r,245,243,209,255);
  YiPauseMenuText(r,m->confirm?(m->return_confirm?"RESTART GAME?":"QUIT GAME?"):"PAUSE",panel.x+panel.w/2,panel.y+8*u,m->confirm?u:SDL_max(1,3*u/2));
  if(m->confirm&&m->return_confirm) {
    YiPauseMenuText(r,"PROGRESS SINCE LAST SAVE",panel.x+panel.w/2,panel.y+24*u,u);
    YiPauseMenuText(r,"WILL BE LOST",panel.x+panel.w/2,panel.y+35*u,u);
  }
  for(int i=0;i<YiPauseMenuButtonCount(m);++i) {
    bool disabled=!m->confirm && i==2 && !m->can_exit_level;
    SDL_SetRenderDrawColor(r,disabled?35:i==m->selected?59:23,disabled?44:i==m->selected?108:73,disabled?39:i==m->selected?65:50,255);
    SDL_RenderFillRect(r,&buttons[i]);
    SDL_SetRenderDrawColor(r,i==m->selected?255:100,i==m->selected?220:128,i==m->selected?115:100,255);
    SDL_RenderDrawRect(r,&buttons[i]);
    SDL_SetRenderDrawColor(r,disabled?105:255,disabled?120:255,disabled?110:235,255);
    YiPauseMenuText(r,m->confirm?(i?(m->return_confirm?"RESTART GAME":"QUIT"):"CANCEL"):(i==0?"CONTINUE":i==1?"RESTART GAME":i==2?"EXIT LEVEL":"QUIT"),panel.x+panel.w/2,buttons[i].y+5*u,u);
  }
  SDL_SetRenderDrawColor(r,185,204,179,255);
  YiPauseMenuText(r,m->confirm?"ESC: CANCEL":"ESC: CONTINUE",panel.x+panel.w/2,panel.y+panel.h-9*u,SDL_max(1,u/2));
  SDL_SetRenderDrawBlendMode(r,blend);SDL_SetRenderDrawColor(r,red,green,blue,alpha);
}
#endif
