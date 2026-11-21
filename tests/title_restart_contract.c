#include "yi_title_restart.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char **argv){
 if(argc==3&&!strcmp(argv[1],"--child")){HANDLE e=OpenEventA(EVENT_MODIFY_STATE,FALSE,argv[2]);if(!e)return 2;SetEvent(e);CloseHandle(e);return 0;}
 char event_name[128];snprintf(event_name,sizeof(event_name),"Local\\YoshiRestartFixture%lu",GetCurrentProcessId());
 HANDLE event=CreateEventA(NULL,TRUE,FALSE,event_name);CHECK(event);
 wchar_t exe[32768],command[65536];CHECK(GetModuleFileNameW(NULL,exe,32768));
 swprintf(command,65536,L"\"%ls\" --child %hs",exe,event_name);
 YiTitleRestart restart={0};CHECK(YiTitleRestartPrepare(&restart,exe,command));
 CHECK(WaitForSingleObject(event,50)==WAIT_TIMEOUT);
 HANDLE child=NULL;CHECK(DuplicateHandle(GetCurrentProcess(),restart.process.hProcess,GetCurrentProcess(),&child,0,FALSE,DUPLICATE_SAME_ACCESS));
 CHECK(YiTitleRestartFinish(&restart));CHECK(WaitForSingleObject(event,5000)==WAIT_OBJECT_0);
 CHECK(WaitForSingleObject(child,5000)==WAIT_OBJECT_0);DWORD result;CHECK(GetExitCodeProcess(child,&result)&&result==0);
 CHECK(!restart.process.hThread&&!restart.process.hProcess);CloseHandle(child);CloseHandle(event);
 wchar_t bad[]=L"missing.exe";CHECK(!YiTitleRestartPrepare(&restart,L"Z:\\nonexistent-restart-fixture.exe",bad));
 CHECK(!YiTitleRestartReady(&restart,"invalid\"rom.sfc"));
 puts("PASS: synthetic child stays suspended until teardown boundary; restart handles close; invalid launch fails safely. No game/ROM execution.");return 0;
}
