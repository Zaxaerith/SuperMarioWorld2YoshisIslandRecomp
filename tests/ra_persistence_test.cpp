#include "client.h"
#include <cstdio>
#include <cstring>
int main(int argc,char**argv){auto s=yira::windows_services(L"YoshisIslandRecomp/RetroAchievements/PersistenceTest");if(argc>1&&strcmp(argv[1],"write")==0)return !s->store({"SyntheticUser","SyntheticToken"});yira::Credential c;bool r=s->restore(c);bool match=r&&c.username=="SyntheticUser"&&c.token=="SyntheticToken";s->forget();yira::Credential gone;bool removed=!s->restore(gone);printf("fresh_process_restore=%d logout_removed=%d\n",match,removed);return !(match&&removed);}
